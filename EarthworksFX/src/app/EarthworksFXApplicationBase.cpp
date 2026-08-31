#include "EarthworksFXApplicationBase.hpp"

#include <algorithm>
#include <cmath>      // fabsf
#include <cstring>
#include <filesystem>

#include "Errors.hpp"
#include "CommandLineParser.hpp"
#include "GraphicsAccessories.hpp"

#include "imgui.h"
#include "ImGuiImplDiligent.hpp"
#include "ImGuiUtils.hpp"

#include "EarthworksDebug.h"
#include "TestFlightData.h"
#include "TestFlightController.hpp"

#include "ots/Log.hpp"
#include "ots/CrashGuard.hpp"

#if D3D11_SUPPORTED
#    include "EngineFactoryD3D11.h"
#endif

#if D3D12_SUPPORTED
#    include "EngineFactoryD3D12.h"
#endif

#if GL_SUPPORTED || GLES_SUPPORTED
#    include "EngineFactoryOpenGL.h"
#endif

#if VULKAN_SUPPORTED
#    include "EngineFactoryVk.h"
#endif

#if PLATFORM_WIN32
#    include "ImGuiImplWin32.hpp"
#elif PLATFORM_LINUX
#    include "ImGuiImplLinuxX11.hpp"
#    if VULKAN_SUPPORTED
#        include "ImGuiImplLinuxXCB.hpp"
#    endif
#endif

namespace Diligent
{

EarthworksFXApplicationBase::EarthworksFXApplicationBase(const std::string& Title,
                                                           const std::string& AppDataFolder,
                                                           overthinking::Env::Stage Stage)
    : m_AppTitle{Title}
{
    overthinking::Env::init(Title, AppDataFolder, Stage);
    UpdateAppSettings(true);
}

EarthworksFXApplicationBase::~EarthworksFXApplicationBase()
{
    // A testflight writes holes.txt into its run folder and disarms the hole
    // detector in Finalize(); if it is still armed here this was an interactive
    // session - dump the trace so a manual repro (rotate until tiles vanish,
    // quit) leaves the same hard data.
    TestFlightController::DumpInteractiveHoleStats();

    // Tear the Earthworks scene down (and its GPU resources) while the device is
    // still alive, before the swap chain / device are released below.
    if (m_Initialized)
        m_Earthworks->onShutdown();
    m_TargetFbo.reset();
    m_Earthworks.reset();
    m_GpuContext.reset();

    m_pImGuiOwner.reset();
    m_pImGui = nullptr;

    if (!m_pDeviceContexts.empty())
    {
        for (Uint32 q = 0; q < m_NumImmediateContexts; ++q)
            m_pDeviceContexts[q]->Flush();
        m_pDeviceContexts.clear();
    }
    m_NumImmediateContexts = 0;
    m_pSwapChain.Release();
    m_pDevice.Release();
}

void EarthworksFXApplicationBase::ModifyEngineInitInfo(const ModifyEngineInitInfoAttribs& Attribs)
{
    Attribs.EngineCI.Features = DeviceFeatures{DEVICE_FEATURE_STATE_OPTIONAL};
    Attribs.EngineCI.Features.TransferQueueTimestampQueries = DEVICE_FEATURE_STATE_DISABLED;

    // Features the Earthworks renderer relies on.
    Attribs.EngineCI.Features.ComputeShaders = DEVICE_FEATURE_STATE_ENABLED;
    Attribs.EngineCI.Features.DepthClamp     = DEVICE_FEATURE_STATE_OPTIONAL;
    Attribs.SCDesc.ColorBufferFormat         = TEX_FORMAT_BGRA8_UNORM_SRGB;

    // vsync-off must actually uncap the FPS on Vulkan. Diligent recreates the
    // VkSwapchain when Present(0) is first called and prefers MAILBOX as the
    // non-vsync mode, but with the default BufferCount = 2 mailbox degenerates
    // to vsync cadence: one image is on the display, the other waits in the
    // mailbox slot, so vkAcquireNextImageKHR blocks until vblank. A third image
    // keeps one always free to render into. (D3D12 is unaffected either way:
    // DXGI flip-model + sync interval 0 uncaps with any count.)
    if (Attribs.DeviceType == RENDER_DEVICE_TYPE_VULKAN)
        Attribs.SCDesc.BufferCount = 3;

    OnModifyEngineInitInfo(Attribs);
}

EarthworksFXAppSettings EarthworksFXApplicationBase::GetAppSettings(bool IsInitialization)
{
    EarthworksFXAppSettings Settings;
    if (IsInitialization)
    {
        Settings.DeviceType        = RENDER_DEVICE_TYPE_VULKAN;
        Settings.VSync             = true;
        Settings.ShowUI            = true;
        Settings.FirstPersonCamera = true;
        Settings.WindowWidth       = 1280;
        Settings.WindowHeight = 768;
    }
    OnConfigureSettings(Settings);
    return Settings;
}

void EarthworksFXApplicationBase::UpdateAppSettings(bool IsInitialization)
{
    const EarthworksFXAppSettings Settings = GetAppSettings(IsInitialization);

    if (IsInitialization)
    {
        m_DeviceType  = Settings.DeviceType;
        m_CreateScene = Settings.CreateScene;
        m_Window.SetInitialSize(Settings.WindowWidth, Settings.WindowHeight);
    }

    m_Window.SetVSync(Settings.VSync);
    m_Window.SetShowUI(Settings.ShowUI);
    m_Window.SetFirstPersonCameraEnabled(Settings.FirstPersonCamera);
}

AppBase::CommandLineStatus EarthworksFXApplicationBase::ProcessCommandLine(int argc, const char* const* argv)
{
    // First chance to call into the fully-constructed derived object (a virtual
    // call from the constructor would only ever reach the base). Run the one-time
    // Initialize() hook, then (re-)apply settings so an OnConfigureSettings()
    // override actually takes effect. Command-line flags below then override these.
    Initialize();
    UpdateAppSettings(true);

    if (argc == 0)
        return CommandLineStatus::OK;

    if (argv == nullptr)
    {
        UNEXPECTED("argv is null when argc (", argc, ") is not zero");
        return CommandLineStatus::Error;
    }

    CommandLineParser ArgsParser{argc, argv};

    ArgsParser.Parse("mode", 'm',
                     [&](const char* ArgVal) {
                         const std::vector<std::pair<const char*, RENDER_DEVICE_TYPE>> DeviceTypeEnumVals =
                             {
#if D3D11_SUPPORTED
                                 {"d3d11", RENDER_DEVICE_TYPE_D3D11},
#endif
#if D3D12_SUPPORTED
                                 {"d3d12", RENDER_DEVICE_TYPE_D3D12},
#endif
#if GL_SUPPORTED
                                 {"gl", RENDER_DEVICE_TYPE_GL},
#endif
#if VULKAN_SUPPORTED
                                 {"vk", RENDER_DEVICE_TYPE_VULKAN},
#endif
                             };
                         return ArgsParser.ParseEnum("mode", 'm', DeviceTypeEnumVals, m_DeviceType);
                     });

    // Explicit terrain to load (directory or *.terrainSettings.json); bypasses
    // lastFile.xml.
    ArgsParser.Parse("terrain", 't',
                     [&](const char* ArgVal) {
                         if (ArgVal != nullptr && ArgVal[0] != '\0')
                             Earthworks_4::setTerrainOverride(ArgVal);
                         return true;
                     });

    int Width = 0;
    int Height = 0;
    m_Window.GetInitialSize(Width, Height);
    ArgsParser.Parse("width", 'w', Width);
    ArgsParser.Parse("height", 'h', Height);
    if (Width > 0 && Height > 0)
        m_Window.SetInitialSize(Width, Height);

    bool VSync = m_Window.GetVSync();
    ArgsParser.Parse("vsync", VSync);
    m_Window.SetVSync(VSync);

    ArgsParser.Parse("validation", m_ValidationLevel);

    bool ShowUI = m_Window.GetShowUI();
    ArgsParser.Parse("show_ui", ShowUI);
    m_Window.SetShowUI(ShowUI);

    bool FirstPersonCamera = m_Window.GetFirstPersonCameraEnabled();
    ArgsParser.Parse("first_person_camera", FirstPersonCamera);
    m_Window.SetFirstPersonCameraEnabled(FirstPersonCamera);

    // --- testflight mode -----------------------------------------------------
    std::string TestFlightArg;
    ArgsParser.Parse("testflight", TestFlightArg);
    if (!TestFlightArg.empty())
    {
        TestFlightController::Options TFOpts;
        TFOpts.FlightArg = TestFlightArg;
        ArgsParser.Parse("tf_camera", TFOpts.OnlyCamera);
        ArgsParser.Parse("tf_lossless", TFOpts.Lossless);
        ArgsParser.Parse("tf_settle_ms", TFOpts.SettleMsOverride);
        ArgsParser.Parse("tf_jpg_quality", TFOpts.JpgQuality);

        m_TestFlight = std::make_unique<TestFlightController>(TFOpts);
        if (!m_TestFlight->LoadFlight())
        {
            m_TestFlight.reset();
            return CommandLineStatus::Error;
        }

        // A testflight enforces its environment: exact (windowed) window size,
        // vsync off for honest timings, camera under script control. ShowUI stays
        // on so the badge is burned into the captures; the regular UI is
        // suppressed in Update() instead.
        const ew::TestFlight& Flight = m_TestFlight->GetFlight();
        if (Flight.windowWidth > 0 && Flight.windowHeight > 0)
            m_Window.SetInitialSize(Flight.windowWidth, Flight.windowHeight);
        m_Window.SetVSync(false);
        m_Window.SetShowUI(true);
        m_Window.SetFirstPersonCameraEnabled(true);
    }

#if !VULKAN_SUPPORTED
    if (m_DeviceType == RENDER_DEVICE_TYPE_VULKAN)
    {
        LOG_ERROR_MESSAGE("Vulkan is not supported in this build.");
        return CommandLineStatus::Error;
    }
#endif

    if (m_DeviceType == RENDER_DEVICE_TYPE_UNDEFINED)
        m_DeviceType = RENDER_DEVICE_TYPE_VULKAN;

    return ProcessSampleCommandLine(ArgsParser.ArgC(), ArgsParser.ArgV());
}

// Diligent's errors/warnings (failed SetArray, vkAllocateDescriptorSets /
// descriptor-pool exhaustion, PSO validation, ...) go to its DEFAULT callback -
// printf + the debugger Output window - and never reached the spdlog file.
// Several of those failures render silently on Vulkan (unbound descriptors
// sample as white on NV) instead of crashing, so route them into the log.
static void DILIGENT_CALL_TYPE DiligentMessageToSpdlog(DEBUG_MESSAGE_SEVERITY Severity,
                                                       const Char*            Message,
                                                       const Char*            Function,
                                                       const Char*            File,
                                                       int                    Line)
{
    const char* Msg = Message != nullptr ? Message : "";
    switch (Severity)
    {
        case DEBUG_MESSAGE_SEVERITY_INFO: spdlog::info("[Diligent] {}", Msg); break;
        case DEBUG_MESSAGE_SEVERITY_WARNING: spdlog::warn("[Diligent] {}", Msg); break;
        default:
            spdlog::error("[Diligent] {} ({}, {}:{})", Msg,
                          Function != nullptr ? Function : "?",
                          File != nullptr ? File : "?", Line);
            break;
    }
}

void EarthworksFXApplicationBase::InitializeDiligentEngine(const NativeWindow* pWindow)
{
    Uint32 NumImmediateContexts = 0;
    std::vector<IDeviceContext*> ppContexts;

#if D3D11_SUPPORTED || D3D12_SUPPORTED || VULKAN_SUPPORTED
    auto FindAdapter = [&](auto* pFactory, Version GraphicsAPIVersion) -> Uint32 {
        Uint32 NumAdapters = 0;
        pFactory->EnumerateAdapters(GraphicsAPIVersion, NumAdapters, nullptr);
        std::vector<GraphicsAdapterInfo> Adapters(NumAdapters);
        if (NumAdapters == 0)
            LOG_ERROR_AND_THROW("Failed to find compatible hardware adapters");

        pFactory->EnumerateAdapters(GraphicsAPIVersion, NumAdapters, Adapters.data());

        Uint32 AdapterId      = DEFAULT_ADAPTER_ID;
        ADAPTER_TYPE BestType = ADAPTER_TYPE_UNKNOWN;
        for (Uint32 i = 0; i < Adapters.size(); ++i)
        {
            if (Adapters[i].Type > BestType)
            {
                BestType  = Adapters[i].Type;
                AdapterId = i;
            }
        }
        if (AdapterId != DEFAULT_ADAPTER_ID)
            LOG_INFO_MESSAGE("Using adapter ", AdapterId, ": '", Adapters[AdapterId].Description, "'");
        return AdapterId;
    };
#endif

    switch (m_DeviceType)
    {
#if D3D11_SUPPORTED
        case RENDER_DEVICE_TYPE_D3D11:
        {
            IEngineFactoryD3D11* pFactoryD3D11 = LoadAndGetEngineFactoryD3D11();
            m_pEngineFactory                   = pFactoryD3D11;

            EngineD3D11CreateInfo EngineCI;
            EngineCI.GraphicsAPIVersion = {11, 0};
            if (m_ValidationLevel >= 0)
                EngineCI.SetValidationLevel(static_cast<VALIDATION_LEVEL>(m_ValidationLevel));

            EngineCI.AdapterId = FindAdapter(pFactoryD3D11, EngineCI.GraphicsAPIVersion);
            ModifyEngineInitInfo({pFactoryD3D11, m_DeviceType, EngineCI, m_SwapChainInitDesc});

            NumImmediateContexts = std::max(1u, EngineCI.NumImmediateContexts);
            ppContexts.resize(size_t{NumImmediateContexts} + size_t{EngineCI.NumDeferredContexts});
            pFactoryD3D11->CreateDeviceAndContextsD3D11(EngineCI, &m_pDevice, ppContexts.data());
            if (!m_pDevice)
                LOG_ERROR_AND_THROW("Unable to initialize Diligent Engine in Direct3D11 mode.");

            if (pWindow != nullptr)
                pFactoryD3D11->CreateSwapChainD3D11(m_pDevice, ppContexts[0], m_SwapChainInitDesc, FullScreenModeDesc{}, *pWindow, &m_pSwapChain);
        }
        break;
#endif

#if D3D12_SUPPORTED
        case RENDER_DEVICE_TYPE_D3D12:
        {
            IEngineFactoryD3D12* pFactoryD3D12 = LoadAndGetEngineFactoryD3D12();
            if (!pFactoryD3D12->LoadD3D12())
                LOG_ERROR_AND_THROW("Failed to load Direct3D12");
            m_pEngineFactory = pFactoryD3D12;
            pFactoryD3D12->SetMessageCallback(&DiligentMessageToSpdlog); // before device creation, so init-time messages land in the log too

            // Same rationale as the Vulkan branch below: in Debug builds
            // Diligent turns validation failures into a MODAL MessageBox on the
            // render thread, which looks like a freeze. Log instead.
            pFactoryD3D12->SetBreakOnError(false);

            EngineD3D12CreateInfo EngineCI;
            EngineCI.GraphicsAPIVersion = {11, 0};
            if (m_ValidationLevel >= 0)
                EngineCI.SetValidationLevel(static_cast<VALIDATION_LEVEL>(m_ValidationLevel));

            // The 8-MRT terrafector bake blend (RT0 elevation =
            // One/InvSrcAlpha, RT1-7 SrcAlpha/InvSrcAlpha) needs
            // IndependentBlendEnable; Diligent's feature default is DISABLED.
            // Trivially supported on D3D12 - request it for parity with the
            // Vulkan branch (where it maps to a real VkPhysicalDeviceFeature).
            EngineCI.Features.IndependentBlend = DEVICE_FEATURE_STATE_ENABLED;

            // Every shader variable in the ew:: shader layer is DYNAMIC, so
            // EVERY CommitShaderResources (i.e. every draw) allocates fresh
            // GPU-visible descriptors from the DYNAMIC region of the descriptor
            // heap, and the sprite/ribbon/vegetation shaders declare
            // Texture2D textures_T[4096] -> ~4100 CBV/SRV/UAV descriptors PER
            // DRAW. Unlike Vulkan (whose descriptor
            // pools grow on demand), the D3D12 shader-visible heap is ONE fixed
            // heap sized here; the default dynamic region (8192) is exhausted by
            // the second big-array draw of the first frame. When that happens in
            // a Release build, CommitRootTables copies into a null descriptor
            // handle and crashes inside the driver (the only hint is a
            // "Dynamic space in ... GPU descriptor heap is exhausted" log line).
            // Budget: dynamic descriptors are recycled only when the GPU finishes
            // the frame, so this must cover (per-frame demand) x (frames in
            // flight). Total heap (static + dynamic) must stay below the D3D12
            // limit of 1,000,000. Tune with the end-of-run
            // "GPU heap max allocated size (static|dynamic)" log statistics.
            EngineCI.GPUDescriptorHeapDynamicSize[0] = 786432; // CBV/SRV/UAV (default 8192)
            // The sampler heap is capped at 2048 TOTAL by D3D12. All samplers in
            // this app are DYNAMIC variables too, so give the dynamic region
            // nearly everything (defaults are 1024/1024).
            EngineCI.GPUDescriptorHeapSize[1]        = 128;
            EngineCI.GPUDescriptorHeapDynamicSize[1] = 1920;

            EngineCI.AdapterId = FindAdapter(pFactoryD3D12, EngineCI.GraphicsAPIVersion);
            ModifyEngineInitInfo({pFactoryD3D12, m_DeviceType, EngineCI, m_SwapChainInitDesc});

            NumImmediateContexts = std::max(1u, EngineCI.NumImmediateContexts);
            ppContexts.resize(NumImmediateContexts + EngineCI.NumDeferredContexts);
            pFactoryD3D12->CreateDeviceAndContextsD3D12(EngineCI, &m_pDevice, ppContexts.data());
            if (!m_pDevice)
                LOG_ERROR_AND_THROW("Unable to initialize Diligent Engine in Direct3D12 mode.");

            if (!m_pSwapChain && pWindow != nullptr)
                pFactoryD3D12->CreateSwapChainD3D12(m_pDevice, ppContexts[0], m_SwapChainInitDesc, FullScreenModeDesc{}, *pWindow, &m_pSwapChain);
        }
        break;
#endif

#if GL_SUPPORTED || GLES_SUPPORTED
        case RENDER_DEVICE_TYPE_GL:
        case RENDER_DEVICE_TYPE_GLES:
        {
            IEngineFactoryOpenGL* pFactoryOpenGL = LoadAndGetEngineFactoryOpenGL();
            m_pEngineFactory                     = pFactoryOpenGL;

            EngineGLCreateInfo EngineCI;
            EngineCI.Window = *pWindow;
            if (m_ValidationLevel >= 0)
                EngineCI.SetValidationLevel(static_cast<VALIDATION_LEVEL>(m_ValidationLevel));

            ModifyEngineInitInfo({pFactoryOpenGL, m_DeviceType, EngineCI, m_SwapChainInitDesc});

            NumImmediateContexts = 1;
            ppContexts.resize(NumImmediateContexts + EngineCI.NumDeferredContexts);
            pFactoryOpenGL->CreateDeviceAndSwapChainGL(EngineCI, &m_pDevice, ppContexts.data(), m_SwapChainInitDesc, &m_pSwapChain);
            if (!m_pDevice)
                LOG_ERROR_AND_THROW("Unable to initialize Diligent Engine in OpenGL mode.");
        }
        break;
#endif

#if VULKAN_SUPPORTED
        case RENDER_DEVICE_TYPE_VULKAN:
        {
            IEngineFactoryVk* pFactoryVk = LoadAndGetEngineFactoryVk();
            m_pEngineFactory             = pFactoryVk;
            pFactoryVk->SetMessageCallback(&DiligentMessageToSpdlog); // before device creation, so init-time messages land in the log too

            // In Debug builds Diligent turns validation failures (UNEXPECTED)
            // into a MODAL Abort/Retry/Ignore MessageBox on the render thread.
            // That is the mysterious "freeze ~1s after load": the window stops
            // updating while the (sometimes hidden) dialog waits for input,
            // and each further frame re-asserts. Log the error instead.
            pFactoryVk->SetBreakOnError(false);

            EngineVkCreateInfo EngineCI;
            if (m_ValidationLevel >= 0)
                EngineCI.SetValidationLevel(static_cast<VALIDATION_LEVEL>(m_ValidationLevel));

            const char* const ppIgnoreDebugMessages[] = {
                "UNASSIGNED-CoreValidation-Shader-OutputNotConsumed",
                "WARNING-Shader-OutputNotConsumed",
                "VUID-VkShaderModuleCreateInfo-pCode-08737",
            };
            EngineCI.ppIgnoreDebugMessageNames = ppIgnoreDebugMessages;
            EngineCI.IgnoreDebugMessageCount   = _countof(ppIgnoreDebugMessages);

            // Several Earthworks shaders declare Texture2D arrays of 4096
            // (render_tile_sprite, render_ribbons, render_*Terrafector, ...).
            // One descriptor set for such a PSO needs >4096 sampled-image
            // descriptors, but Diligent's default DYNAMIC pool only holds 2048
            // in total -> vkAllocateDescriptorSets fails as soon as those
            // passes start drawing.
            EngineCI.DynamicDescriptorPoolSize.NumSampledImageDescriptors = 32768;
            EngineCI.MainDescriptorPoolSize.NumSampledImageDescriptors    = 16384;

            // DeviceFeatures::IndependentBlend defaults to DISABLED, and
            // EngineFactoryVk only enables the Vulkan `independentBlend` device
            // feature when the state is ENABLED. Without it the 8-MRT
            // terrafector bake blend (RT0 elevation = One/InvSrcAlpha override
            // vs SrcAlpha/InvSrcAlpha on RT1-7) is invalid Vulkan and the
            // terrafectors break. Universally supported on desktop; ENABLED
            // (hard failure) beats OPTIONAL (silent brokenness on the day it
            // is missing).
            EngineCI.Features.IndependentBlend = DEVICE_FEATURE_STATE_ENABLED;

            // The terrafector shaders index gmyTextures_T[4096] with per-pixel
            // material indices, decorated NonUniformResourceIndex
            // (materials.hlsli TF_TEX). On Vulkan that SPIR-V NonUniform
            // decoration is only legal when the descriptor-indexing feature
            // chain is enabled, which EngineFactoryVk ties to
            // ShaderResourceRuntimeArrays (default DISABLED); without it the
            // terrafectors render white. D3D12 needs nothing - its factory
            // force-enables this feature unconditionally.
            EngineCI.Features.ShaderResourceRuntimeArrays = DEVICE_FEATURE_STATE_ENABLED;

            EngineCI.AdapterId = FindAdapter(pFactoryVk, EngineCI.GraphicsAPIVersion);
            ModifyEngineInitInfo({pFactoryVk, m_DeviceType, EngineCI, m_SwapChainInitDesc});

            NumImmediateContexts = std::max(1u, EngineCI.NumImmediateContexts);
            ppContexts.resize(NumImmediateContexts + EngineCI.NumDeferredContexts);
            pFactoryVk->CreateDeviceAndContextsVk(EngineCI, &m_pDevice, ppContexts.data());
            if (!m_pDevice)
                LOG_ERROR_AND_THROW("Unable to initialize Diligent Engine in Vulkan mode.");

            if (!m_pSwapChain && pWindow != nullptr)
                pFactoryVk->CreateSwapChainVk(m_pDevice, ppContexts[0], m_SwapChainInitDesc, *pWindow, &m_pSwapChain);
        }
        break;
#endif

        default:
            LOG_ERROR_AND_THROW("Unsupported device type");
    }

    // After the factory exists, before anything can fail silently. The
    // callback is global to the engine module, one call covers all backends.
    m_pEngineFactory->SetMessageCallback(&DiligentMessageToSpdlog);

    m_AppTitle.append(" (");
    m_AppTitle.append(GetRenderDeviceTypeString(m_DeviceType));
    m_AppTitle.append(", API ");
    m_AppTitle.append(std::to_string(DILIGENT_API_VERSION));
    m_AppTitle.push_back(')');

    m_NumImmediateContexts = NumImmediateContexts;
    m_pDeviceContexts.resize(ppContexts.size());
    for (size_t i = 0; i < ppContexts.size(); ++i)
        m_pDeviceContexts[i].Attach(ppContexts[i]);

    m_pImmediateContext = m_pDeviceContexts[0];
    const Uint32 NumDeferredCtx = static_cast<Uint32>(m_pDeviceContexts.size()) - m_NumImmediateContexts;
    m_pDeferredContexts.resize(NumDeferredCtx);
    for (Uint32 ctx = 0; ctx < NumDeferredCtx; ++ctx)
        m_pDeferredContexts[ctx] = m_pDeviceContexts[m_NumImmediateContexts + ctx];
}

void EarthworksFXApplicationBase::CreateImGui()
{
    const auto& SCDesc = m_pSwapChain->GetDesc();
#if PLATFORM_WIN32
    m_pImGuiOwner = ImGuiImplWin32::Create(ImGuiDiligentCreateInfo{m_pDevice, SCDesc}, m_Window.GetHWND());
#elif PLATFORM_LINUX
#    if VULKAN_SUPPORTED
    if (m_DeviceType == RENDER_DEVICE_TYPE_VULKAN)
    {
        m_pImGuiOwner = ImGuiImplLinuxXCB::Create(ImGuiDiligentCreateInfo{m_pDevice, SCDesc},
                                                  m_Window.GetXCBConnection(),
                                                  SCDesc.Width, SCDesc.Height);
        m_InputController.InitXCBKeysms(m_Window.GetXCBConnection());
    }
    else
#    endif
    {
        m_pImGuiOwner = ImGuiImplLinuxX11::Create(ImGuiDiligentCreateInfo{m_pDevice, SCDesc}, SCDesc.Width, SCDesc.Height);
    }
#endif
    m_pImGui = m_pImGuiOwner.get();

#if PLATFORM_WIN32
    spdlog::info("EarthworksFX: ImGui init (impl={}, hwnd={}, ctx={})",
                 static_cast<const void*>(m_pImGui),
                 static_cast<const void*>(m_Window.GetHWND()),
                 static_cast<const void*>(ImGui::GetCurrentContext()));
#endif
    if (!m_pImGui)
        LOG_ERROR_AND_THROW("Failed to create ImGui implementation");
}

void EarthworksFXApplicationBase::InitializeEnvironment()
{
    // The full Earthworks rendering environment minus the terrain scene: the
    // ew:: GPU context (device pointers + shader/data search paths) and the
    // swap chain target FBO. Always created, so manual-rendering apps still
    // get the Earthworks shaders and rendering behaviour.
    m_GpuContext = std::make_unique<ew::GpuContext>(m_pDevice, m_pImmediateContext, m_pSwapChain, m_pEngineFactory);

    m_GpuContext->addDataDirectory(std::filesystem::current_path(), true);
    m_GpuContext->addDataDirectory(std::filesystem::current_path() / "terrains", false);
    m_GpuContext->addDataDirectory(std::filesystem::current_path() / "EarthworksFX", true);

    m_TargetFbo = ew::Fbo::createFromSwapChain(m_pSwapChain);
}

void EarthworksFXApplicationBase::InitializeScene()
{
    m_Earthworks = std::make_unique<Earthworks_4>();
    m_Earthworks->onLoad(m_GpuContext.get());

    const SwapChainDesc& SCDesc = m_pSwapChain->GetDesc();
    m_Earthworks->onResizeSwapChain(SCDesc.Width, SCDesc.Height);

    if (const auto& cam = m_Earthworks->getCamera())
    {
        m_FirstPersonCamera.SetPos(ew::toDiligent(cam->getPosition()));
        m_FirstPersonCamera.SetLookAt(ew::toDiligent(cam->getTarget()));
        m_FirstPersonCamera.SetMoveSpeed(50.f);
        m_FirstPersonCamera.Update(m_InputController, 0.f);
    }

    m_Initialized = true;

    // Arm the hole detector for interactive sessions from the start (cost is
    // one read-only pass over the used tiles per terrain update). A testflight
    // re-arms it under the flight's name when its run folder is created.
    ew::gDebug.holeStats.start("interactive");
}

void EarthworksFXApplicationBase::InitializeGraphicsResources()
{
    ImGui::StyleColorsDiligent();

    InitializeEnvironment();
    if (m_CreateScene)
        InitializeScene();

    const SwapChainDesc& SCDesc = m_pSwapChain->GetDesc();
    OnWindowResized(SCDesc.Width, SCDesc.Height);
    UpdateFirstPersonCameraProjAttribs();

    if (m_TestFlight)
    {
        std::string DeviceString{GetRenderDeviceTypeString(m_DeviceType)};
        DeviceString += " / ";
        DeviceString += m_pDevice->GetAdapterInfo().Description;
        m_TestFlight->OnGraphicsReady(m_pDevice, m_pSwapChain, m_pImmediateContext, DeviceString);
    }

    OnGraphicsReady();
}

bool EarthworksFXApplicationBase::InitializeGraphics(const NativeWindow* pWindow)
{
    LOG_INFO_MESSAGE("EarthworksFXApplicationBase::InitializeGraphics");
    spdlog::info("EarthworksFXApplicationBase::InitializeGraphics");
    InitializeDiligentEngine(pWindow);
    CreateImGui();
    InitializeGraphicsResources();
    return true;
}

void EarthworksFXApplicationBase::SetFullscreenMode(const DisplayModeAttribs& DisplayMode)
{
#if PLATFORM_WIN32
    m_Window.ExitBorderlessFullscreenIfActive();
#endif
    ReleaseSwapChainBuffers();
    m_bFullScreenMode = true;
    m_pSwapChain->SetFullscreenMode(DisplayMode);
}

void EarthworksFXApplicationBase::SetWindowedMode()
{
#if PLATFORM_WIN32
    m_Window.ExitBorderlessFullscreenIfActive();
#endif
    ReleaseSwapChainBuffers();
    m_bFullScreenMode = false;
    m_pSwapChain->SetWindowedMode();
}

void EarthworksFXApplicationBase::WindowResize(int width, int height)
{
    if (m_pSwapChain)
    {
        ReleaseSwapChainBuffers();
        m_pSwapChain->Resize(width, height);
        const SwapChainDesc& SCDesc = m_pSwapChain->GetDesc();
        OnWindowResized(SCDesc.Width, SCDesc.Height);
        UpdateFirstPersonCameraProjAttribs();
    }
}

void EarthworksFXApplicationBase::UpdateFirstPersonCamera(float ElapsedTime)
{
    m_FirstPersonCamera.Update(m_InputController, ElapsedTime);
}

void EarthworksFXApplicationBase::UpdateFirstPersonCameraProjAttribs()
{
    if (!m_pSwapChain)
        return;

    const SwapChainDesc& SCDesc = m_pSwapChain->GetDesc();
    if (SCDesc.Width == 0 || SCDesc.Height == 0)
        return;

    const float AspectRatio = static_cast<float>(SCDesc.Width) / static_cast<float>(SCDesc.Height);
    const bool  IsGL        = m_DeviceType == RENDER_DEVICE_TYPE_GL || m_DeviceType == RENDER_DEVICE_TYPE_GLES;
    m_FirstPersonCamera.SetProjAttribs(0.1f, 40000.f, AspectRatio, PI_F / 4.f, SCDesc.PreTransform, IsGL);
}

void EarthworksFXApplicationBase::Update(double CurrTime, double ElapsedTime)
{
    ++m_NumFramesRendered;
    static const double FPSInterval = 0.5;
    if (CurrTime - m_LastFPSTime > FPSInterval)
    {
        m_fSmoothFPS        = static_cast<float>(m_NumFramesRendered / (CurrTime - m_LastFPSTime));
        m_NumFramesRendered = 0;
        m_LastFPSTime       = CurrTime;
    }

    if (m_pImGui)
    {
        const SwapChainDesc& SCDesc = m_pSwapChain->GetDesc();
        m_pImGui->NewFrame(SCDesc.Width, SCDesc.Height, SCDesc.PreTransform);
    }

    if (m_pDevice)
    {
        const bool TestFlightActive = m_TestFlight != nullptr;
        const bool DoUpdateUI       = m_Window.GetShowUI() && !TestFlightActive;

        // In testflight mode user input is ignored; the controller owns the camera.
        if (UseFirstPersonCamera() && !TestFlightActive)
            UpdateFirstPersonCamera(static_cast<float>(ElapsedTime));

        if (TestFlightActive)
        {
            ew::Camera* pSceneCamera = m_Earthworks ? m_Earthworks->getCamera().get() : nullptr;
            const bool  SceneReady   = m_CreateScene ? m_Initialized : true;
            m_TestFlight->Update(CurrTime, ElapsedTime, m_FirstPersonCamera, pSceneCamera, SceneReady);
        }

        OnUpdate(CurrTime, ElapsedTime, DoUpdateUI);
        if (DoUpdateUI)
            UpdateUI();
        else if (TestFlightActive && m_Window.GetShowUI())
            m_TestFlight->DrawBadgeUI();
        m_InputController.ClearState();

        if (TestFlightActive && m_TestFlight->IsFinished() && !m_TestFlightExitPosted)
        {
            m_TestFlightExitPosted = true;
            m_ExitCode             = m_TestFlight->GetExitCode();
#if PLATFORM_WIN32
            PostQuitMessage(m_ExitCode);
#else
            // No quit path on this platform yet (testflights are Windows-first);
            // the run folder is complete at this point.
            LOG_WARNING_MESSAGE("Testflight finished; automatic exit is not implemented on this platform");
#endif
        }
    }
}

void EarthworksFXApplicationBase::DrawCommonUI()
{
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("EarthworksFX", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("%s  (API %d)", GetRenderDeviceTypeString(m_DeviceType), DILIGENT_API_VERSION);
        const float FrameMs = m_fSmoothFPS > 0.f ? 1000.0f / m_fSmoothFPS : 0.f;
        ImGui::Text("%.1f ms  (%.1f fps)", FrameMs, m_fSmoothFPS);
        ImGui::Separator();
        m_Window.DrawImGuiControls(m_CreateScene);
    }
    ImGui::End();

    DrawEarthworksDebugUI();
    DrawTestFlightsUI();
}

void EarthworksFXApplicationBase::DrawTestFlightsUI()
{
    // Deliberately self-contained (static locals, no class members): ImGui code
    // will be rehomed during a later base-class cleanup, this keeps the move cheap.
    namespace fs = std::filesystem;

    static std::vector<std::string> s_Flights;   // flight names (json stems)
    static bool                     s_Scanned  = false;
    static int                      s_Selected = -1;
    static ew::TestFlight           s_Flight;
    static bool                     s_Loaded = false;
    static bool                     s_Dirty  = false;
    static char                     s_NewName[64] = "";
    static std::string              s_Status;

    const auto Rescan = []() {
        s_Flights.clear();
        std::error_code ec;
        for (const fs::directory_entry& Entry : fs::directory_iterator(TestFlightController::GetFlightsDir(), ec))
        {
            if (Entry.is_regular_file(ec) && Entry.path().extension() == ".json")
                s_Flights.push_back(Entry.path().stem().string());
        }
        std::sort(s_Flights.begin(), s_Flights.end());
    };

    const auto LoadSelected = []() {
        s_Loaded = false;
        s_Dirty  = false;
        if (s_Selected < 0 || s_Selected >= static_cast<int>(s_Flights.size()))
            return;
        const fs::path Path = TestFlightController::GetFlightsDir() / (s_Flights[static_cast<size_t>(s_Selected)] + ".json");
        std::string    Error;
        if (ew::LoadTestFlight(Path.string(), s_Flight, Error))
        {
            s_Loaded = true;
            s_Status = "loaded " + Path.filename().string();
        }
        else
        {
            s_Status = Error;
        }
    };

    ImGui::SetNextWindowPos(ImVec2(350, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(430, 380), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Testflights"))
    {
        if (!s_Scanned)
        {
            Rescan();
            s_Scanned = true;
        }

        ew::Camera* pSceneCamera = HasEarthworksScene() ? GetEarthworks().getCamera().get() : nullptr;

        // --- flight selection ------------------------------------------------
        const char* Preview = (s_Selected >= 0 && s_Selected < static_cast<int>(s_Flights.size()))
            ? s_Flights[static_cast<size_t>(s_Selected)].c_str()
            : "<select flight>";
        ImGui::SetNextItemWidth(220);
        if (ImGui::BeginCombo("##flight", Preview))
        {
            for (int i = 0; i < static_cast<int>(s_Flights.size()); ++i)
            {
                if (ImGui::Selectable(s_Flights[static_cast<size_t>(i)].c_str(), i == s_Selected))
                {
                    s_Selected = i;
                    LoadSelected();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("rescan"))
            Rescan();

        ImGui::SetNextItemWidth(220);
        ImGui::InputTextWithHint("##newflight", "new_name", s_NewName, sizeof(s_NewName));
        ImGui::SameLine();
        if (ImGui::SmallButton("create") && s_NewName[0] != '\0')
        {
            ew::TestFlight NewFlight;
            NewFlight.name = s_NewName;
            if (HasEarthworksScene())
                NewFlight.terrain = GetEarthworks().getTerrainName();
            if (m_pSwapChain)
            {
                const SwapChainDesc& SCDesc = m_pSwapChain->GetDesc();
                NewFlight.windowWidth       = static_cast<int>(SCDesc.Width);
                NewFlight.windowHeight      = static_cast<int>(SCDesc.Height);
            }
            const fs::path Path = TestFlightController::GetFlightsDir() / (NewFlight.name + ".json");
            std::string    Error;
            if (ew::SaveTestFlight(Path.string(), NewFlight, Error))
            {
                Rescan();
                const auto it = std::find(s_Flights.begin(), s_Flights.end(), std::string{s_NewName});
                s_Selected    = it != s_Flights.end() ? static_cast<int>(it - s_Flights.begin()) : -1;
                LoadSelected();
                s_NewName[0] = '\0';
            }
            else
            {
                s_Status = Error;
            }
        }

        // --- selected flight --------------------------------------------------
        if (s_Loaded)
        {
            ImGui::SeparatorText(s_Flight.name.c_str());
            if (!s_Flight.terrain.empty())
                ImGui::TextDisabled("terrain: %s", s_Flight.terrain.c_str());
            ImGui::Text("window %dx%d", s_Flight.windowWidth, s_Flight.windowHeight);
            ImGui::SameLine();
            if (ImGui::SmallButton("set from current") && m_pSwapChain)
            {
                const SwapChainDesc& SCDesc = m_pSwapChain->GetDesc();
                s_Flight.windowWidth        = static_cast<int>(SCDesc.Width);
                s_Flight.windowHeight       = static_cast<int>(SCDesc.Height);
                s_Dirty                     = true;
            }
            ImGui::SetNextItemWidth(100);
            if (ImGui::InputInt("settle ms", &s_Flight.settleMs, 0))
                s_Dirty = true;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100);
            if (ImGui::InputInt("timeout ms", &s_Flight.settleTimeoutMs, 0))
                s_Dirty = true;

            // --- camera list --------------------------------------------------
            int MoveUp = -1, MoveDown = -1, Remove = -1;
            for (int i = 0; i < static_cast<int>(s_Flight.shots.size()); ++i)
            {
                ew::TestFlightShot& Shot = s_Flight.shots[static_cast<size_t>(i)];
                ImGui::PushID(i);

                ImGui::Text("%02d", i);
                ImGui::SameLine();
                char NameBuf[64];
                std::snprintf(NameBuf, sizeof(NameBuf), "%s", Shot.name.c_str());
                ImGui::SetNextItemWidth(140);
                if (ImGui::InputText("##name", NameBuf, sizeof(NameBuf)))
                {
                    Shot.name = NameBuf;
                    s_Dirty   = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("view"))
                    TestFlightController::ApplyShotToCamera(Shot, m_FirstPersonCamera, pSceneCamera);
                ImGui::SameLine();
                if (ImGui::SmallButton("up"))
                    MoveUp = i;
                ImGui::SameLine();
                if (ImGui::SmallButton("dn"))
                    MoveDown = i;
                ImGui::SameLine();
                if (ImGui::SmallButton("del"))
                    Remove = i;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("pos (%.1f  %.1f  %.1f)  yaw %.2f  pitch %.2f  fov %.1f",
                                      Shot.posX, Shot.posY, Shot.posZ, Shot.yaw, Shot.pitch, Shot.fovYDeg);

                ImGui::PopID();
            }
            if (MoveUp > 0)
            {
                std::swap(s_Flight.shots[static_cast<size_t>(MoveUp)], s_Flight.shots[static_cast<size_t>(MoveUp) - 1]);
                s_Dirty = true;
            }
            if (MoveDown >= 0 && MoveDown + 1 < static_cast<int>(s_Flight.shots.size()))
            {
                std::swap(s_Flight.shots[static_cast<size_t>(MoveDown)], s_Flight.shots[static_cast<size_t>(MoveDown) + 1]);
                s_Dirty = true;
            }
            if (Remove >= 0)
            {
                s_Flight.shots.erase(s_Flight.shots.begin() + Remove);
                s_Dirty = true;
            }

            // --- actions ------------------------------------------------------
            if (ImGui::Button("add current"))
            {
                ew::TestFlightShot Shot = TestFlightController::CaptureShotFromCamera(m_FirstPersonCamera, pSceneCamera);
                Shot.name               = "cam_" + std::to_string(s_Flight.shots.size());
                s_Flight.shots.push_back(std::move(Shot));
                s_Dirty = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("save"))
            {
                // Stamp the currently loaded terrain so the flight is traceable
                // to the world it was authored on.
                if (HasEarthworksScene())
                    s_Flight.terrain = GetEarthworks().getTerrainName();
                const fs::path Path = TestFlightController::GetFlightsDir() / (s_Flights[static_cast<size_t>(s_Selected)] + ".json");
                std::string    Error;
                if (ew::SaveTestFlight(Path.string(), s_Flight, Error))
                {
                    s_Dirty  = false;
                    s_Status = "saved " + Path.filename().string();
                }
                else
                {
                    s_Status = Error;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("reload"))
                LoadSelected();
            if (s_Dirty)
            {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.f, 0.6f, 0.2f, 1.f), "unsaved");
            }

            ImGui::TextDisabled("run: --testflight %s", s_Flight.name.c_str());
        }
        else
        {
            ImGui::TextDisabled("no flight selected");
            ImGui::TextDisabled("folder: %s", TestFlightController::GetFlightsDir().string().c_str());
        }

        if (!s_Status.empty())
            ImGui::TextDisabled("%s", s_Status.c_str());
    }
    ImGui::End();
}

void EarthworksFXApplicationBase::DrawEarthworksDebugUI()
{
    ew::DebugState&   dbg   = ew::gDebug;
    ew::DebugToggles& t     = dbg.toggles;
    const ew::DebugMetrics& m = dbg.shown;

    ImGui::SetNextWindowPos(ImVec2(10, 320), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(330, 470), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Earthworks Features & Metrics"))
    {
        const bool hasScene = HasEarthworksScene();
        if (!hasScene)
            ImGui::TextDisabled("No terrain scene (CreateScene == false).");

        // --- Terrain mode --------------------------------------------------
        ImGui::SeparatorText("Terrain mode");
        ImGui::Text("current: %s", ew::TerrainModeName(m.terrainMode));
        if (m.vegetationEarlyOut)
            ImGui::TextColored(ImVec4(1.f, 0.5f, 0.2f, 1.f),
                               "vegetation mode: terrain tiles NOT drawn\n(renderer returns after skydome + plants)");
        if (m.updateEarlyOut)
            ImGui::TextColored(ImVec4(1.f, 0.5f, 0.2f, 1.f),
                               "this mode skips tile streaming\n(terrain.update() early-returns; no split/LOD)\nuse ecotope / terrafector / roads to stream terrain");

        int curMode = m.terrainMode >= 0 ? m.terrainMode : 0;
        const char* modeNames[] = {"vegetation", "ecotope", "terrafector", "roads",
                                    "glider", "terrainBuilder", "textureTool"};
        if (ImGui::Combo("set mode", &curMode, modeNames, IM_ARRAYSIZE(modeNames)))
            t.requestTerrainMode = curMode;
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("The desktop host does not forward the 1..7 mode keys;\nuse this to switch modes at runtime.");

        // --- Passes --------------------------------------------------------
        ImGui::SeparatorText("Render passes");
        ImGui::Checkbox("skydome", &t.skydome);
        ImGui::Checkbox("terrain tiles", &t.terrainTiles);
        ImGui::Checkbox("buildings", &t.buildings);
        ImGui::Checkbox("billboards", &t.billboards);
        ImGui::Checkbox("plants", &t.plants);
        ImGui::Checkbox("ribbons (glider)", &t.ribbons);
        ImGui::Checkbox("splines (roads/terrafector)", &t.splines);
        ImGui::Checkbox("terrain debug colour", &t.terrainConstColor);
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Replace the terrain shading (sun/shadow/atmosphere) with a\nworld-position pattern. Pattern visible -> geometry + draw path OK,\nproblem is the shading inputs. Nothing -> tile pipeline/camera.");

        ImGui::SeparatorText("Load-time options");
        ImGui::Checkbox("building shadows", &dbg.loadOptions.buildingShadows);
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Bake buildings into the terrain shadow heightfield as casters.\nSampled once while a terrain loads - changing it has no effect\non the current scene, only on the next terrain (re)load.");

        ImGui::SeparatorText("Compute / update");
        ImGui::Checkbox("terrain update (stream/clip/lod)", &t.terrainUpdate);
        ImGui::Checkbox("atmosphere (sun + volumetric)", &t.atmosphere);

        // CPU terrain-shadow solver (_shadowEdges): sun elevation + re-solve.
        ImGui::SetNextItemWidth(200);
        ImGui::DragFloat("shadow sun angle", &t.shadowSunAngle, 0.01f, 0.f, 3.14f, "%1.2f");
        ImGui::SameLine();
        if (ImGui::Button("re-solve"))
            t.shadowResolve = true;
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Re-runs the background CPU shadow solve at this sun elevation\n(radians, east-west path). Takes a few seconds; the sun direction\nand shadow texture swap together when it finishes.");

        ImGui::SeparatorText("Terrafectors / roads");
        ImGui::Checkbox("bake roads into tiles", &t.tfBakeRoads);
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("bSplineAsTerrafector: rasterize the road network into the tile bake\n"
                              "(flattens terrain under roads via the RT0 elevation blend).\n"
                              "Defaults ON so the live bake is exercised.\n"
                              "Affects newly split tiles - use 'rebake all tiles' to apply everywhere.");
        ImGui::SameLine();
        if (ImGui::SmallButton("rebake all tiles"))
            t.tfRebake = true;
        // TODO: the tooltip below says the overlay is off while roads are baked, but
        // tfShowRoadSpline defaults to true alongside tfBakeRoads = true and the
        // gating lives in the renderer, so this shows a checked box with no effect.
        ImGui::Checkbox("3D road overlay", &t.tfShowRoadSpline);
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("render_spline translucent ribbons over the terrain - the fastest\n"
                              "'did the road network load' check. Only draws while roads are\n"
                              "NOT baked as terrafectors ('bake roads' off).");

        if (ImGui::TreeNode("bake stages (draw order = priority)"))
        {
            ImGui::Checkbox("1 mesh bakeLow", &t.tfStageBakeLow);
            ImGui::Checkbox("2 road bakeOnly (flatten)", &t.tfStageRoadBakeOnly);
            ImGui::Checkbox("3 mesh bakeHigh", &t.tfStageBakeHigh);
            ImGui::Checkbox("4 mesh terrafectors", &t.tfStageMeshes);
            ImGui::Checkbox("5 GIS overlay", &t.tfStageOverlay);
            ImGui::Checkbox("6 road LOD bins", &t.tfStageRoadBins);
            ImGui::Checkbox("7 stamps", &t.tfStageStamps);
            ImGui::Checkbox("8 _top combiners", &t.tfStageTop);
            ImGui::TextDisabled("stages apply to newly split tiles; rebake to apply everywhere");
            ImGui::TreePop();
        }

        {
            if (ImGui::Button("probe next 8 bakes"))
                t.tfBakeElevationStatsLeft = 8;
            ImGui::SameLine();
            ImGui::Text("left: %d", t.tfBakeElevationStatsLeft);
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Reads the elevation-centre texel back before/after each of the\n"
                                  "next N tile bakes (full GPU stall, debug only) and logs it.\n"
                                  "A good height collapsing to ~0 = the old y=0 bug signature.");
            ImGui::Checkbox("A/B: disable RT0 elevation blend", &t.tfBakeNoElevationBlend);
            const bool y0sig = fabsf(m.tfProbeBefore) > 50.f && fabsf(m.tfProbeAfter) < 1.f;
            ImGui::TextColored(y0sig ? ImVec4(1.f, 0.3f, 0.2f, 1.f) : ImVec4(0.7f, 0.7f, 0.7f, 1.f),
                               "last probe: lod %u  centre %.1f -> %.1f%s",
                               m.tfProbeLod, m.tfProbeBefore, m.tfProbeAfter,
                               y0sig ? "  y=0 SIGNATURE" : "");
        }

        ImGui::SeparatorText("Post / overlay");
        ImGui::Checkbox("bypass HDR (scene direct to swapchain)", &t.bypassHdr);
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("ON: terrain/globe render straight into the swap chain; the HDR\nbuffer and tonemapper are out of the loop entirely.\nOFF: normal path (scene -> hdrFbo -> tonemapper -> swapchain).");
        if (t.bypassHdr) ImGui::BeginDisabled();
        ImGui::Checkbox("tonemapper", &t.tonemapper);
        {
            const char* views[] = { "normal (ACES+LUT)", "raw HDR", "solid test colour" };
            ImGui::SetNextItemWidth(200);
            ImGui::Combo("tonemap view", &t.tonemapperView, views, IM_ARRAYSIZE(views));
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("The HDR FBO only reaches the screen through the tonemapper.\n"
                                  "'raw HDR' shows whether anything rendered into it;\n"
                                  "'solid test colour' shows whether the pass writes the swapchain at all.");
        }
        if (t.bypassHdr) ImGui::EndDisabled();
        ImGui::Checkbox("overlay (thumbnail blit)", &t.overlay);

        ImGui::SeparatorText("Debug aids");
        ImGui::Checkbox("globe (occluded by terrain)", &t.debugGlobe);
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Camera-centred compass globe (radius = 40x40 km area half-diagonal),\ndrawn with depth testing so terrain occludes it. North meridian red,\nsouth black, east/west grey; horizon + faint 45-degree markers.");
        ImGui::Checkbox("ground grid (quadtree tiles)", &t.debugGroundGrid);
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Live terrain quadtree: one outline per leaf tile (slightly inset so\nneighbours stay separate), colour-coded by LOD - grey lod0, blue,\ncyan, green, yellow, orange, red, magenta. Drawn on top of everything.");

        ImGui::SeparatorText("ImGui");
        ImGui::Checkbox("Earthworks editor GUI", &t.earthworksGui);
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("The terrain/vegetation editor windows.\nIn vegetation mode this draws a full-screen HUD +\n'vegetation builder' panel (the static rectangle).");

        if (ImGui::SmallButton("enable all"))
            t = ew::DebugToggles{};
        ImGui::SameLine();
        if (ImGui::SmallButton("isolate terrain"))
        {
            t = ew::DebugToggles{};
            t.skydome = t.buildings = t.billboards = t.plants = t.ribbons = t.splines = t.overlay = false;
            t.earthworksGui = false;
        }

        // --- Metrics (last completed frame) --------------------------------
        ImGui::SeparatorText("Draws this frame");
        ImGui::Text("skydome      %u", m.skydomeDraws);
        ImGui::Text("terrainTiles %u", m.terrainTileDraws);
        ImGui::Text("buildings    %u", m.buildingDraws);
        ImGui::Text("billboards   %u", m.billboardDraws);
        ImGui::Text("plants       %u", m.plantDraws);
        ImGui::Text("ribbons      %u", m.ribbonDraws);
        ImGui::Text("splines      %u", m.splineDraws);
        ImGui::Text("tonemapper   %u", m.tonemapperDraws);
        ImGui::Text("overlay      %u", m.overlayDraws);
        ImGui::Text("debugGlobe   %u", m.debugGlobeDraws);
        ImGui::Text("debugGrid    %u", m.debugGridDraws);

        ImGui::SeparatorText("Scene counts");
        ImGui::Text("tiles used / free : %u / %u", m.tilesUsed, m.tilesFree);
        ImGui::Text("ribbons loaded    : %u", m.ribbonsLoaded);
        ImGui::Text("splines static/dyn: %u / %u", m.staticSplines, m.dynamicSplines);

        // What the GPU actually packed into the indirect draw args for the main
        // view (GC_feedback readback). If blocks is 0 the terrain draw runs with
        // instanceCount 0 and CANNOT produce pixels regardless of the draw
        // counters above.
        ImGui::SeparatorText("GPU main view (feedback)");
        ImGui::TextColored(m.gpuTerrainBlocks > 0 ? ImVec4(0.5f, 1.f, 0.5f, 1.f) : ImVec4(1.f, 0.4f, 0.2f, 1.f),
                           "terrain tiles/blocks: %u / %u", m.gpuTerrainTiles, m.gpuTerrainBlocks);
        ImGui::Text("terrain tris      : %u", m.gpuTerrainTris);
        ImGui::Text("billboard quads   : %u", m.gpuQuads);

        // With no plant data in the scene these read 0 while the dispatch
        // chain still runs - dormant, not broken.
        ImGui::SeparatorText("Vegetation (feedback)");
        ImGui::Text("instances / blocks: %u / %u", m.vegInstances, m.vegBlocks);
        ImGui::Text("frustum discards  : %u", m.vegFrustDiscard);
        ImGui::Text("billboards (13=sentinel): %u", m.vegBillboards);
        ImGui::Text("feedback age      : %u frames", m.vegFeedbackAge);

        ImGui::SeparatorText("Tile split");
        ImGui::TextColored(m.cameraMainInUse ? ImVec4(0.5f, 1.f, 0.5f, 1.f) : ImVec4(1.f, 0.4f, 0.2f, 1.f),
                           "main camera in use: %s", m.cameraMainInUse ? "yes" : "NO");
        ImGui::TextColored(m.splitAnyInFrust ? ImVec4(0.5f, 1.f, 0.5f, 1.f) : ImVec4(1.f, 0.4f, 0.2f, 1.f),
                           "any tile in frustum: %s", m.splitAnyInFrust ? "yes" : "NO");
        ImGui::Text("max lod_Pix       : %.1f  (split needs > 150)", m.splitMaxLodPix);
        ImGui::Text("split candidates  : %u", m.splitCandidates);
        ImGui::Text("splits performed  : %u", m.splitsPerformed);
        ImGui::Text("blocked (no data) : %u", m.splitBlockedData);
        ImGui::Text("blocked (<8 free) : %s", m.splitBlockedFree ? "yes" : "no");
    }
    ImGui::End();
}

void EarthworksFXApplicationBase::UpdateUI()
{
    DrawCommonUI();
}

void EarthworksFXApplicationBase::OnRender()
{
    if (!m_Initialized)
        return;

    if (!m_TargetFbo)
        m_TargetFbo = ew::Fbo::createFromSwapChain(m_pSwapChain);

    m_Earthworks->onFrameRender(m_GpuContext.get(), m_TargetFbo);
}

void EarthworksFXApplicationBase::OnUpdate(double CurrTime, double ElapsedTime, bool DoUpdateUI)
{
    (void)CurrTime;
    (void)ElapsedTime;

    if (!m_Initialized)
        return;

    if (UseFirstPersonCamera())
        SyncFirstPersonCameraToEarthworks();
    else
        SyncInput();

    if (DoUpdateUI && ew::gDebug.toggles.earthworksGui)
        m_Earthworks->onGuiRender();
}

void EarthworksFXApplicationBase::OnWindowResized(Uint32 Width, Uint32 Height)
{
    if (!m_Initialized)
        return;

    m_Earthworks->onResizeSwapChain(Width, Height);
    m_TargetFbo = ew::Fbo::createFromSwapChain(m_pSwapChain);
}

void EarthworksFXApplicationBase::SyncFirstPersonCameraToEarthworks()
{
    const auto& cam = m_Earthworks->getCamera();
    if (!cam)
        return;

    const auto& fpc = m_FirstPersonCamera;
    cam->setPosition(ew::toGlm(fpc.GetPos()));
    cam->setTarget(ew::toGlm(fpc.GetPos() + fpc.GetWorldAhead() * 100.f));
}

void EarthworksFXApplicationBase::SyncInput()
{
    const MouseState mouse = m_InputController.GetMouseState();

    // The Win32 InputController reports client-space pixels, but the Earthworks
    // camera code expects normalized [0,1] screen coordinates (it gates on
    // 'pos.x > 0 && pos.x < 1'), so normalize here.
    const SwapChainDesc& scDesc = m_pSwapChain->GetDesc();
    const float width  = scDesc.Width  > 0 ? static_cast<float>(scDesc.Width)  : 1.f;
    const float height = scDesc.Height > 0 ? static_cast<float>(scDesc.Height) : 1.f;

    ew::MouseEvent event{};
    event.pos = ew::float2{static_cast<float>(mouse.PosX) / width,
                           static_cast<float>(mouse.PosY) / height};

    int buttons = 0;
    if (mouse.ButtonFlags & MouseState::BUTTON_FLAG_LEFT)
        buttons |= static_cast<int>(ew::MouseEvent::Buttons::Left);
    if (mouse.ButtonFlags & MouseState::BUTTON_FLAG_RIGHT)
        buttons |= static_cast<int>(ew::MouseEvent::Buttons::Right);
    if (mouse.ButtonFlags & MouseState::BUTTON_FLAG_MIDDLE)
        buttons |= static_cast<int>(ew::MouseEvent::Buttons::Middle);
    event.buttons = static_cast<ew::MouseEvent::Buttons>(buttons);

    // The camera rotate/pan/orbit code runs on Move events and reads live button
    // state via ImGui::IsMouseDown(), so always deliver a Move (this also lets it
    // track drag deltas frame-to-frame). Deliver Wheel as a separate event.
    event.type = ew::MouseEvent::Type::Move;
    m_Earthworks->onMouseEvent(event);

    if (mouse.WheelDelta != 0.f)
    {
        event.type       = ew::MouseEvent::Type::Wheel;
        event.wheelDelta = ew::float2{0.f, mouse.WheelDelta};
        m_Earthworks->onMouseEvent(event);
    }
}

void EarthworksFXApplicationBase::Render()
{
    if (m_NumImmediateContexts == 0 || !m_pSwapChain)
        return;

    IDeviceContext* pCtx = GetImmediateContext();

    ITextureView* pRTV = m_pSwapChain->GetCurrentBackBufferRTV();
    ITextureView* pDSV = m_pSwapChain->GetDepthBufferDSV();
    pCtx->SetRenderTargets(1, &pRTV, pDSV, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    OnRender();

    pCtx->SetRenderTargets(1, &pRTV, pDSV, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    if (m_pImGui)
    {
        if (m_Window.GetShowUI())
            m_pImGui->Render(pCtx);
        else
            m_pImGui->EndFrame();
    }
}

void EarthworksFXApplicationBase::Present()
{
    if (!m_pSwapChain)
        return;

    if (m_TestFlight)
        m_TestFlight->PrePresent();

    m_pSwapChain->Present(m_Window.GetVSync() ? 1 : 0);

    if (m_TestFlight)
        m_TestFlight->PostPresent();
}

#if PLATFORM_WIN32

bool EarthworksFXApplicationBase::OnWindowCreated(HWND hWnd, LONG WindowWidth, LONG WindowHeight)
{
    m_Window.Attach(hWnd);
    (void)WindowWidth;
    (void)WindowHeight;
    const Win32NativeWindow NativeWindow = m_Window.GetNativeWindow();

    if (!overthinking::Env::isDebuggerAttached()) {
        return InitializeGraphics(&NativeWindow);
    }

    try
    {
        return InitializeGraphics(&NativeWindow);
    }
    catch (const std::exception& ex)
    {
        spdlog::error("Exception during graphics initialization: {}", ex.what());
        ots::CrashGuard::logException("Exception during init()", ex);
        throw;
    }
}

LRESULT EarthworksFXApplicationBase::HandlePlatformMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    LRESULT WindowResult = m_Window.HandleMessage(message, wParam, lParam);
    if (WindowResult != 0)
        return WindowResult;

    if (m_pImGui)
    {
        auto Handled = static_cast<ImGuiImplWin32*>(m_pImGui)->Win32_ProcHandler(hWnd, message, wParam, lParam);
        if (Handled)
            return Handled;
    }

    struct WindowsMessageData
    {
        HWND   hWnd;
        UINT   message;
        WPARAM wParam;
        LPARAM lParam;
    } MsgData = {hWnd, message, wParam, lParam};

    m_InputController.HandleNativeMessage(&MsgData);
    return HandleSampleNativeMessage(&MsgData) ? 1 : 0;
}

#elif PLATFORM_LINUX

bool EarthworksFXApplicationBase::OnGLContextCreated(Display* display, Window window)
{
    LinuxNativeWindow LinuxWindow;
    LinuxWindow.pDisplay = display;
    LinuxWindow.WindowId = window;
    return InitializeGraphics(&LinuxWindow);
}

int EarthworksFXApplicationBase::HandleXEvent(XEvent* xev)
{
    if (m_pImGui)
    {
        auto Handled = static_cast<ImGuiImplLinuxX11*>(m_pImGui)->HandleXEvent(xev);
        if (!Handled || xev->type == ButtonRelease || xev->type == MotionNotify || xev->type == KeyRelease)
            return m_InputController.HandleXEvent(xev);
        return Handled;
    }
    return m_InputController.HandleXEvent(xev);
}

#    if VULKAN_SUPPORTED

bool EarthworksFXApplicationBase::InitVulkan(xcb_connection_t* connection, uint32_t window)
{
    int Width = 0;
    int Height = 0;
    m_Window.GetInitialSize(Width, Height);
    m_Window.AttachVulkan(connection, window,
                          static_cast<uint16_t>(Width > 0 ? Width : 1024),
                          static_cast<uint16_t>(Height > 0 ? Height : 768));
    const LinuxNativeWindow NativeWindow = m_Window.GetNativeWindow();
    return InitializeGraphics(&NativeWindow);
}

void EarthworksFXApplicationBase::HandleXCBEvent(xcb_generic_event_t* event)
{
    HandlePlatformXCBEvent(event);
}

void EarthworksFXApplicationBase::HandlePlatformXCBEvent(xcb_generic_event_t* event)
{
    if (m_pImGui)
    {
        auto Handled   = static_cast<ImGuiImplLinuxXCB*>(m_pImGui)->HandleXCBEvent(event);
        auto EventType = event->response_type & 0x7f;
        if (!Handled || EventType == XCB_MOTION_NOTIFY || EventType == XCB_BUTTON_RELEASE || EventType == XCB_KEY_RELEASE)
            m_InputController.HandleXCBEvent(event);
    }
}

#    endif

#endif

} // namespace Diligent
