#include "EarthworksFXApplicationBase.hpp"

#include <algorithm>
#include <filesystem>

#include "Errors.hpp"
#include "CommandLineParser.hpp"
#include "GraphicsAccessories.hpp"

#include "imgui.h"
#include "ImGuiImplDiligent.hpp"
#include "ImGuiUtils.hpp"

#include "EarthworksDebug.h"

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

namespace
{

/// Scopes Falcor's global framework pointer for the duration of an Earthworks
/// call, restoring the previous value afterwards.
class ScopedFalcorFramework
{
public:
    explicit ScopedFalcorFramework(Falcor::FrameworkInterface* pFramework)
    {
        m_Prev = Falcor::gpFramework;
        Falcor::SetFalcorFramework(pFramework);
    }

    ~ScopedFalcorFramework()
    {
        Falcor::SetFalcorFramework(m_Prev);
    }

private:
    Falcor::FrameworkInterface* m_Prev = nullptr;
};

} // namespace

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
    // Tear the Earthworks scene down (and its GPU resources) while the device is
    // still alive, before the swap chain / device are released below.
    if (m_Initialized)
    {
        ScopedFalcorFramework scope{&m_Framework};
        m_Earthworks->onShutdown();
    }
    m_TargetFbo.reset();
    m_Earthworks.reset();
    m_FalcorWrapper.reset();

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

            EngineD3D12CreateInfo EngineCI;
            EngineCI.GraphicsAPIVersion = {11, 0};
            if (m_ValidationLevel >= 0)
                EngineCI.SetValidationLevel(static_cast<VALIDATION_LEVEL>(m_ValidationLevel));

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
    // The full Earthworks rendering environment minus the terrain scene: Falcor
    // device + framework, shader/data search paths, render context and the swap
    // chain target FBO. Always created, so manual-rendering apps still get the
    // Earthworks shaders and rendering behaviour.
    m_FalcorWrapper = std::make_unique<Falcor::EarthworksWrapper>();

    Falcor::SetFalcorDevice(m_pDevice, m_pImmediateContext, m_pSwapChain, m_pEngineFactory);
    Falcor::SetFalcorFramework(&m_Framework);
    Falcor::addDataDirectory(std::filesystem::current_path(), true);
    Falcor::addDataDirectory(std::filesystem::current_path() / "terrains", false);
    Falcor::addDataDirectory(std::filesystem::current_path() / "EarthworksFX", true);

    m_RenderContext = Falcor::RenderContext{m_pImmediateContext};
    m_TargetFbo     = Falcor::Fbo::createFromSwapChain(m_pSwapChain);
}

void EarthworksFXApplicationBase::InitializeScene()
{
    ScopedFalcorFramework scope{&m_Framework};

    m_Earthworks = std::make_unique<Earthworks_4>();
    m_Earthworks->onLoad(&m_RenderContext);

    const SwapChainDesc& SCDesc = m_pSwapChain->GetDesc();
    m_Earthworks->onResizeSwapChain(SCDesc.Width, SCDesc.Height);

    if (const auto& cam = m_Earthworks->getCamera())
    {
        m_FirstPersonCamera.SetPos(cam->getPosition());
        m_FirstPersonCamera.SetLookAt(cam->getTarget());
        m_FirstPersonCamera.SetMoveSpeed(50.f);
        m_FirstPersonCamera.Update(m_InputController, 0.f);
    }

    m_Initialized = true;
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

    // Keep the Falcor framework's frame time current for both scene and manual
    // rendering (Earthworks shaders read it via gpFramework).
    m_Framework.SetAverageFrameTimeMs(m_fSmoothFPS > 0.f ? 1000.0 / static_cast<double>(m_fSmoothFPS) : 16.0);

    if (m_pImGui)
    {
        const SwapChainDesc& SCDesc = m_pSwapChain->GetDesc();
        m_pImGui->NewFrame(SCDesc.Width, SCDesc.Height, SCDesc.PreTransform);
    }

    if (m_pDevice)
    {
        const bool DoUpdateUI = m_Window.GetShowUI();
        if (UseFirstPersonCamera())
            UpdateFirstPersonCamera(static_cast<float>(ElapsedTime));
        OnUpdate(CurrTime, ElapsedTime, DoUpdateUI);
        if (DoUpdateUI)
            UpdateUI();
        m_InputController.ClearState();
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
        ImGui::Checkbox("billboards", &t.billboards);
        ImGui::Checkbox("plants", &t.plants);
        ImGui::Checkbox("ribbons (glider)", &t.ribbons);
        ImGui::Checkbox("splines (roads/terrafector)", &t.splines);

        ImGui::SeparatorText("Compute / update");
        ImGui::Checkbox("terrain update (stream/clip/lod)", &t.terrainUpdate);
        ImGui::Checkbox("atmosphere (sun + volumetric)", &t.atmosphere);

        ImGui::SeparatorText("Post / overlay");
        ImGui::Checkbox("tonemapper", &t.tonemapper);
        ImGui::Checkbox("overlay (thumbnail blit)", &t.overlay);
        ImGui::Checkbox("debug grid", &t.debugGrid);

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
            t.skydome = t.billboards = t.plants = t.ribbons = t.splines = t.overlay = false;
            t.earthworksGui = false;
        }

        // --- Metrics (last completed frame) --------------------------------
        ImGui::SeparatorText("Draws this frame");
        ImGui::Text("skydome      %u", m.skydomeDraws);
        ImGui::Text("terrainTiles %u", m.terrainTileDraws);
        ImGui::Text("billboards   %u", m.billboardDraws);
        ImGui::Text("plants       %u", m.plantDraws);
        ImGui::Text("ribbons      %u", m.ribbonDraws);
        ImGui::Text("splines      %u", m.splineDraws);
        ImGui::Text("tonemapper   %u", m.tonemapperDraws);
        ImGui::Text("overlay      %u", m.overlayDraws);
        ImGui::Text("debugGrid    %u", m.debugGridDraws);

        ImGui::SeparatorText("Scene counts");
        ImGui::Text("tiles used / free : %u / %u", m.tilesUsed, m.tilesFree);
        ImGui::Text("ribbons loaded    : %u", m.ribbonsLoaded);
        ImGui::Text("splines static/dyn: %u / %u", m.staticSplines, m.dynamicSplines);
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

    ScopedFalcorFramework scope{&m_Framework};

    if (!m_TargetFbo)
        m_TargetFbo = Falcor::Fbo::createFromSwapChain(m_pSwapChain);

    m_Earthworks->onFrameRender(&m_RenderContext, m_TargetFbo);
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
    {
        ScopedFalcorFramework scope{&m_Framework};
        m_Earthworks->onGuiRender(&m_Gui);
    }
}

void EarthworksFXApplicationBase::OnWindowResized(Uint32 Width, Uint32 Height)
{
    if (!m_Initialized)
        return;

    ScopedFalcorFramework scope{&m_Framework};
    m_Earthworks->onResizeSwapChain(Width, Height);
    m_TargetFbo = Falcor::Fbo::createFromSwapChain(m_pSwapChain);
}

void EarthworksFXApplicationBase::SyncFirstPersonCameraToEarthworks()
{
    const auto& cam = m_Earthworks->getCamera();
    if (!cam)
        return;

    const auto& fpc = m_FirstPersonCamera;
    cam->setPosition(fpc.GetPos());
    cam->setTarget(fpc.GetPos() + fpc.GetWorldAhead() * 100.f);
}

void EarthworksFXApplicationBase::SyncInput()
{
    const MouseState mouse = m_InputController.GetMouseState();

    // The Win32 InputController reports client-space pixels, but the Earthworks
    // camera code expects Falcor-style normalized [0,1] screen coordinates
    // (it gates on 'pos.x > 0 && pos.x < 1'), so normalize here.
    const SwapChainDesc& scDesc = m_pSwapChain->GetDesc();
    const float width  = scDesc.Width  > 0 ? static_cast<float>(scDesc.Width)  : 1.f;
    const float height = scDesc.Height > 0 ? static_cast<float>(scDesc.Height) : 1.f;

    Falcor::MouseEvent event{};
    event.pos = Falcor::float2{static_cast<float>(mouse.PosX) / width,
                               static_cast<float>(mouse.PosY) / height};

    int buttons = 0;
    if (mouse.ButtonFlags & MouseState::BUTTON_FLAG_LEFT)
        buttons |= static_cast<int>(Falcor::MouseEvent::Buttons::Left);
    if (mouse.ButtonFlags & MouseState::BUTTON_FLAG_RIGHT)
        buttons |= static_cast<int>(Falcor::MouseEvent::Buttons::Right);
    if (mouse.ButtonFlags & MouseState::BUTTON_FLAG_MIDDLE)
        buttons |= static_cast<int>(Falcor::MouseEvent::Buttons::Middle);
    event.buttons = static_cast<Falcor::MouseEvent::Buttons>(buttons);

    ScopedFalcorFramework scope{&m_Framework};

    // The camera rotate/pan/orbit code runs on Move events and reads live button
    // state via ImGui::IsMouseDown(), so always deliver a Move (this also lets it
    // track drag deltas frame-to-frame). Deliver Wheel as a separate event.
    event.type = Falcor::MouseEvent::Type::Move;
    m_Earthworks->onMouseEvent(event);

    if (mouse.WheelDelta != 0.f)
    {
        event.type       = Falcor::MouseEvent::Type::Wheel;
        event.wheelDelta = Falcor::float2{0.f, mouse.WheelDelta};
        m_Earthworks->onMouseEvent(event);
    }
}

Falcor::FrameRate EarthworksFXApplicationBase::Framework::getFrameRate() const
{
    return Falcor::FrameRate{};
}

Falcor::WindowInterface* EarthworksFXApplicationBase::Framework::getWindow()
{
    return &m_Window;
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

    m_pSwapChain->Present(m_Window.GetVSync() ? 1 : 0);
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
