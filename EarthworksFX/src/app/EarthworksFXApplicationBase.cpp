#include "EarthworksFXApplicationBase.hpp"

#include <algorithm>

#include "Errors.hpp"
#include "CommandLineParser.hpp"
#include "GraphicsAccessories.hpp"

#include "imgui.h"
#include "ImGuiImplDiligent.hpp"
#include "ImGuiUtils.hpp"

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
    Initialize();
    UpdateAppSettings(true);
}

EarthworksFXApplicationBase::~EarthworksFXApplicationBase()
{
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
}

void EarthworksFXApplicationBase::UpdateAppSettings(bool IsInitialization)
{
    const EarthworksFXAppSettings Settings = GetAppSettings(IsInitialization);

    if (IsInitialization)
    {
        m_DeviceType = Settings.DeviceType;
        m_Window.SetInitialSize(Settings.WindowWidth, Settings.WindowHeight);
    }

    m_Window.SetVSync(Settings.VSync);
    m_Window.SetShowUI(Settings.ShowUI);
}

AppBase::CommandLineStatus EarthworksFXApplicationBase::ProcessCommandLine(int argc, const char* const* argv)
{
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

void EarthworksFXApplicationBase::InitializeGraphicsResources()
{
    ImGui::StyleColorsDiligent();
    OnGraphicsInitialized();

    const SwapChainDesc& SCDesc = m_pSwapChain->GetDesc();
    OnWindowResized(SCDesc.Width, SCDesc.Height);
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
    }
}

void EarthworksFXApplicationBase::Update(double CurrTime, double ElapsedTime)
{
    UpdateAppSettings(false);

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
        const bool DoUpdateUI = m_Window.GetShowUI();
        UpdateSample(CurrTime, ElapsedTime, DoUpdateUI);
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
        m_Window.DrawImGuiControls();
    }
    ImGui::End();
}

void EarthworksFXApplicationBase::Render()
{
    if (m_NumImmediateContexts == 0 || !m_pSwapChain)
        return;

    IDeviceContext* pCtx = GetImmediateContext();

    ITextureView* pRTV = m_pSwapChain->GetCurrentBackBufferRTV();
    ITextureView* pDSV = m_pSwapChain->GetDepthBufferDSV();
    pCtx->SetRenderTargets(1, &pRTV, pDSV, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    RenderSample();

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
