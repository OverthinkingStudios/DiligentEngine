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
#include "FirstPersonCamera.hpp"
#include "EarthworksFXWindowBase.hpp"

#include "Falcor.h"
#include "Earthworks_4.h"

namespace Diligent
{

class ImGuiImplDiligent;

struct EarthworksFXAppSettings
{
    RENDER_DEVICE_TYPE DeviceType   = RENDER_DEVICE_TYPE_VULKAN;
    int                WindowWidth  = 2560;
    int                WindowHeight = 1440;
    bool               VSync              = false;
    bool               ShowUI             = true;
    bool               FirstPersonCamera  = true;
};

/// Desktop host for EarthworksFX games, editors, demos and tools (Win32/Linux).
///
/// The base owns the complete rendering environment: it creates the device and
/// swap chain, sets up ImGui, loads the Earthworks scene, and drives the render /
/// update / resize / UI loop. A new application gets a reasonable experience
/// without overriding anything - just construct it with a title and data folder.
///
/// Customization is done through the protected virtual hooks below; the core
/// lifecycle (settings, engine init, scene load) is handled by the base and only
/// exposes a hook to tweak it, never to replace it.
class EarthworksFXApplicationBase : public NativeAppBase
{
public:
    EarthworksFXApplicationBase(const std::string& Title, const std::string& AppDataFolder, overthinking::Env::Stage Stage);
    ~EarthworksFXApplicationBase() override;

    AppBase::CommandLineStatus ProcessCommandLine(int argc, const char* const* argv) override final;

    const char* GetAppTitle() const override final { return m_AppTitle.c_str(); }
    void Update(double CurrTime, double ElapsedTime) override final;
    void WindowResize(int width, int height) override final;
    void Render() override final;
    void Present() override final;

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

    FirstPersonCamera& GetFirstPersonCamera() { return m_FirstPersonCamera; }
    const FirstPersonCamera& GetFirstPersonCamera() const { return m_FirstPersonCamera; }

    /// When true, the shared first-person controller drives camera input each frame.
    /// Override to force on/off regardless of the window toggle.
    virtual bool UseFirstPersonCamera() const { return m_Window.GetFirstPersonCameraEnabled(); }

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

    // --- Customization hooks ------------------------------------------------
    // Every hook has a working default; override only what an app actually needs
    // to change. The base supplies a complete, runnable Earthworks app on its own.

    /// Called immediately after overthinking::Env::init() in the constructor;
    /// logging and environment are ready, but graphics are not.
    virtual void Initialize() {}

    /// Tweak the default window/device settings (resolution, vsync, device type).
    /// Invoked with the derived object fully constructed, before the window opens.
    virtual void OnConfigureSettings(EarthworksFXAppSettings& Settings) { (void)Settings; }

    /// Tweak engine creation (extra device features, swap chain format, ...).
    /// The base already enables what the Earthworks renderer needs.
    virtual void OnModifyEngineInitInfo(const ModifyEngineInitInfoAttribs& Attribs) { (void)Attribs; }

    /// Called once the device, swap chain, ImGui and the Earthworks scene are all
    /// ready. Use it for app-specific GPU setup.
    virtual void OnGraphicsReady() {}

    /// Per-frame scene render. Default renders the Earthworks scene to the swap chain.
    virtual void OnRender();

    /// Per-frame update. Default drives the camera and the Earthworks scene GUI.
    virtual void OnUpdate(double CurrTime, double ElapsedTime, bool DoUpdateUI);

    /// Swap-chain resize. Default resizes the Earthworks scene and target FBO.
    virtual void OnWindowResized(Uint32 Width, Uint32 Height);

    /// App ImGui pass. Default draws the shared overlay (DrawCommonUI). Override to
    /// add windows; call DrawCommonUI() from the override to keep the shared overlay.
    virtual void UpdateUI();

    /// Feed live mouse state into the Earthworks camera (used when the shared
    /// first-person controller is disabled). Override to customize input mapping.
    virtual void SyncInput();

    /// Push the shared first-person camera into the Earthworks camera each frame.
    virtual void SyncFirstPersonCameraToEarthworks();

    virtual void ReleaseSwapChainBuffers() {}

    virtual bool HandleSampleNativeMessage(const void* pNativeMsgData) {
        (void)pNativeMsgData;
        return false;
    }

    virtual AppBase::CommandLineStatus ProcessSampleCommandLine(int argc, const char* const* argv) {
        (void)argc;
        (void)argv;
        return AppBase::CommandLineStatus::OK;
    }

    /// Generic stats/controls window shared by every EarthworksFX app (device,
    /// frame timing, and the window's own VSync/fullscreen controls). Safe to
    /// call from any UpdateUI() override.
    void DrawCommonUI();

    /// The Earthworks scene owned by the base. Valid from OnGraphicsReady() onwards.
    Earthworks_4& GetEarthworks() { return *m_Earthworks; }

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
    FirstPersonCamera m_FirstPersonCamera;

    void UpdateFirstPersonCamera(float ElapsedTime);
    void UpdateFirstPersonCameraProjAttribs();

private:
    /// Falcor FrameworkInterface implementation shared by every EarthworksFX app.
    class Framework final : public Falcor::FrameworkInterface
    {
    public:
        void SetAverageFrameTimeMs(double ms) { m_AvgFrameTimeMs = ms; }
        double getAverageFrameTimeMs() const override { return m_AvgFrameTimeMs; }

        Falcor::FrameRate getFrameRate() const override;
        Falcor::WindowInterface* getWindow() override;

    private:
        double m_AvgFrameTimeMs = 16.0;
        Falcor::WindowInterface m_Window;
    };

    void InitializeDiligentEngine(const NativeWindow* pWindow);
    void InitializeGraphicsResources();
    void InitializeScene();
    void UpdateAppSettings(bool IsInitialization);

    /// Base-owned lifecycle. These are intentionally not virtual: an app shapes
    /// them through the On* hooks above, it never replaces them.
    EarthworksFXAppSettings GetAppSettings(bool IsInitialization);
    void ModifyEngineInitInfo(const ModifyEngineInitInfoAttribs& Attribs);

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

    // --- Earthworks rendering environment owned by the base -----------------
    std::unique_ptr<Falcor::EarthworksWrapper> m_FalcorWrapper;
    std::unique_ptr<Earthworks_4>              m_Earthworks;
    Falcor::Gui                                m_Gui;
    Falcor::RenderContext                      m_RenderContext{nullptr};
    Framework                                  m_Framework;
    Falcor::Fbo::SharedPtr                     m_TargetFbo;
    bool                                       m_Initialized = false;
};

} // namespace Diligent
