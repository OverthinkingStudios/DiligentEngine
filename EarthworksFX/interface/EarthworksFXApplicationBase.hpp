#pragma once

#include <memory>
#include <string>
#include <vector>

#include "overthinkingEnv.h"

#include "NativeAppBase.hpp"
#include "RefCntAutoPtr.hpp"
#include "EngineFactory.h"
#include "RenderDevice.h"
#include "DeviceContext.h"
#include "SwapChain.h"
#include "InputController.hpp"
#include "EarthworksFXWindowBase.hpp"

namespace Diligent
{

class ImGuiImplDiligent;

struct EarthworksFXAppSettings
{
    RENDER_DEVICE_TYPE DeviceType   = RENDER_DEVICE_TYPE_VULKAN;
    int                WindowWidth  = 1280;
    int                WindowHeight = 1024;
    bool               VSync        = true;
    bool               ShowUI       = true;
};

/// Desktop application host for EarthworksFX games and tools (Win32/Linux).
class EarthworksFXApplicationBase : public NativeAppBase
{
public:
    EarthworksFXApplicationBase(const std::string& Title, const std::string& AppDataFolder, overthinking::Env::Stage Stage);
    ~EarthworksFXApplicationBase() override;

    AppBase::CommandLineStatus ProcessCommandLine(int argc, const char* const* argv) override final;

    const char* GetAppTitle() const override final { return m_AppTitle.c_str(); }
    void Update(double CurrTime, double ElapsedTime) override;
    void WindowResize(int width, int height) override;
    void Render() override;
    void Present() override;

    void GetDesiredInitialWindowSize(int& width, int& height) override final {
        m_Window.GetInitialSize(width, height);
    }

    bool IsReady() const override final {
        return m_pDevice && m_pSwapChain && m_NumImmediateContexts > 0;
    }

    IDeviceContext* GetImmediateContext(size_t Ind = 0) {
        VERIFY_EXPR(Ind < m_NumImmediateContexts);
        return m_pDeviceContexts[Ind];
    }

    EarthworksFXWindowBase& GetWindow() { return m_Window; }
    const EarthworksFXWindowBase& GetWindow() const { return m_Window; }

#if PLATFORM_WIN32
    bool OnWindowCreated(HWND hWnd, LONG WindowWidth, LONG WindowHeight) override final;

    LRESULT HandleWin32Message(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) override final {
        return HandlePlatformMessage(hWnd, message, wParam, lParam);
    }
#elif PLATFORM_LINUX
    bool OnGLContextCreated(Display* display, Window window) override final;

    int HandleXEvent(XEvent* xev) override final;

#    if VULKAN_SUPPORTED
    bool InitVulkan(xcb_connection_t* connection, uint32_t window) override final;
    void HandleXCBEvent(xcb_generic_event_t* event) override final;
#    endif
#endif

protected:
    struct ModifyEngineInitInfoAttribs {
        IEngineFactory* const pFactory;
        const RENDER_DEVICE_TYPE DeviceType;
        EngineCreateInfo& EngineCI;
        SwapChainDesc& SCDesc;
    };

    /// called immediately after overthinking::Env::init() in the constructor; log and env is ready
    virtual void Initialize() {}

    virtual EarthworksFXAppSettings GetAppSettings(bool IsInitialization) { (void)IsInitialization; return {}; }

    virtual void ModifyEngineInitInfo(const ModifyEngineInitInfoAttribs& Attribs);

    /// called after the Diligent device, swap chain, and ImGui are ready.
    virtual void OnGraphicsInitialized() {}

    virtual void ReleaseSwapChainBuffers() {}

    virtual void RenderSample() {}

    virtual void UpdateSample(double CurrTime, double ElapsedTime, bool DoUpdateUI) {
        (void)CurrTime;
        (void)ElapsedTime;
        (void)DoUpdateUI;
    }

    virtual void OnWindowResized(Uint32 Width, Uint32 Height) {
        (void)Width;
        (void)Height;
    }

    /// Per-application UI pass. Override in tools/samples to draw custom ImGui
    /// windows; call DrawCommonUI() from the override to get the shared overlay.
    virtual void UpdateUI() {}

    /// Generic stats/controls window shared by every EarthworksFX app (device,
    /// frame timing, and the window's own VSync/fullscreen controls). Safe to
    /// call from any UpdateUI() override.
    void DrawCommonUI();

    virtual bool HandleSampleNativeMessage(const void* pNativeMsgData) {
        (void)pNativeMsgData;
        return false;
    }

    virtual AppBase::CommandLineStatus ProcessSampleCommandLine(int argc, const char* const* argv) {
        (void)argc;
        (void)argv;
        return AppBase::CommandLineStatus::OK;
    }

    RefCntAutoPtr<IEngineFactory> m_pEngineFactory;
    RefCntAutoPtr<IRenderDevice> m_pDevice;
    RefCntAutoPtr<IDeviceContext> m_pImmediateContext;
    std::vector<RefCntAutoPtr<IDeviceContext>> m_pDeferredContexts;
    RefCntAutoPtr<ISwapChain> m_pSwapChain;
    ImGuiImplDiligent* m_pImGui = nullptr;

    float  m_fSmoothFPS        = 0.f;
    double m_LastFPSTime         = 0.0;
    Uint32 m_NumFramesRendered   = 0;

    InputController m_InputController;

private:
    void InitializeDiligentEngine(const NativeWindow* pWindow);
    void InitializeGraphicsResources();
    void UpdateAppSettings(bool IsInitialization);

    void SetFullscreenMode(const DisplayModeAttribs& DisplayMode);
    void SetWindowedMode();

#if PLATFORM_WIN32
    LRESULT HandlePlatformMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
#elif PLATFORM_LINUX && VULKAN_SUPPORTED
    void HandlePlatformXCBEvent(xcb_generic_event_t* event);
#endif

    bool InitializeGraphics(const NativeWindow* pWindow);
    void CreateImGui();

    EarthworksFXWindowBase m_Window;

    RENDER_DEVICE_TYPE m_DeviceType = RENDER_DEVICE_TYPE_D3D12;
    std::vector<RefCntAutoPtr<IDeviceContext>> m_pDeviceContexts;
    Uint32 m_NumImmediateContexts = 0;
    SwapChainDesc m_SwapChainInitDesc;
    std::unique_ptr<ImGuiImplDiligent> m_pImGuiOwner;

    std::string m_AppTitle;
    int m_ValidationLevel = -1;
    bool m_bFullScreenMode = false;
};

} // namespace Diligent
