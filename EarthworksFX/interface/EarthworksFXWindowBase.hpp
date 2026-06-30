#pragma once

#include "PlatformDefinitions.h"
#include "GraphicsTypes.h"
#include "NativeWindow.h"

#if PLATFORM_WIN32
#    include "WinHPreface.h"
#    include <Windows.h>
#    include "WinHPostface.h"
#elif PLATFORM_LINUX
#    if VULKAN_SUPPORTED
#        include <xcb/xcb.h>
#    endif
#endif

namespace Diligent
{

class EarthworksFXWindowBase
{
public:
    EarthworksFXWindowBase()  = default;
    ~EarthworksFXWindowBase() = default;

    void SetInitialSize(int Width, int Height);
    void GetInitialSize(int& Width, int& Height) const;

    void SetVSync(bool Enable) { m_bVSync = Enable; }
    bool GetVSync() const { return m_bVSync; }

    void SetShowUI(bool Show) { m_bShowUI = Show; }
    bool GetShowUI() const { return m_bShowUI; }

    /// Generic ImGui widgets for this window's own state (VSync, fullscreen).
    /// Reusable by any EarthworksFX application/tool; call it from inside an
    /// existing ImGui window during the UI pass. Implemented in the .cpp so the
    /// ImGui include does not leak into this widely-included header.
    void DrawImGuiControls();

#if PLATFORM_WIN32
    void Attach(HWND hWnd);
    HWND GetHWND() const { return m_hWnd; }

    Win32NativeWindow GetNativeWindow() const;

    /// Borderless fullscreen over the primary monitor (Alt+Enter).
    void ToggleFullscreenWindow();

    /// Returns non-zero if the message was handled.
    LRESULT HandleMessage(UINT Message, WPARAM wParam, LPARAM lParam);

    bool IsBorderlessFullscreen() const { return m_bBorderlessFullscreen; }

    void ExitBorderlessFullscreenIfActive();
#elif PLATFORM_LINUX && VULKAN_SUPPORTED
    void AttachVulkan(xcb_connection_t* pConnection, uint32_t WindowId, uint16_t Width, uint16_t Height);

    xcb_connection_t* GetXCBConnection() const { return m_pXCBConnection; }
    uint32_t          GetXCBWindow() const { return m_XCBWindow; }
    uint16_t          GetWidth() const { return m_Width; }
    uint16_t          GetHeight() const { return m_Height; }

    LinuxNativeWindow GetNativeWindow() const;
#endif

private:
    int  m_InitialWidth  = 1280;
    int  m_InitialHeight = 1024;
    bool m_bVSync        = true;
    bool m_bShowUI       = true;

#if PLATFORM_WIN32
    HWND m_hWnd                  = nullptr;
    bool m_bBorderlessFullscreen = false;
    RECT m_WindowRect            = {};
    LONG m_WindowStyle           = 0;
#elif PLATFORM_LINUX && VULKAN_SUPPORTED
    xcb_connection_t* m_pXCBConnection = nullptr;
    uint32_t          m_XCBWindow      = 0;
    uint16_t          m_Width          = 0;
    uint16_t          m_Height         = 0;
#endif
};

} // namespace Diligent
