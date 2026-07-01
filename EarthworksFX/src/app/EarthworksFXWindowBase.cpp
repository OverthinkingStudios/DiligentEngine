#include "EarthworksFXWindowBase.hpp"

#include "imgui.h"
#include "EarthworksDebug.h"

namespace Diligent
{

void EarthworksFXWindowBase::DrawImGuiControls(bool ShowFirstPersonCamera)
{
    ImGui::Checkbox("VSync", &m_bVSync);

    ImGui::SameLine();
    if (ImGui::Button(m_bBorderlessFullscreen ? "Windowed" : "Fullscreen"))
        ToggleFullscreenWindow();

    if (ShowFirstPersonCamera)
        ImGui::Checkbox("First-person camera (WASD + RMB look)", &m_bFirstPersonCamera);

    ImGui::Checkbox("Sync camera into terrain", &ew::gDebug.toggles.syncCamera);
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("When off, the terrain manager keeps its last camera, so its\nfrustum / tile visibility stops following the view. Use it to\ntest whether the terrain frustum points where we are looking.");
}

void EarthworksFXWindowBase::SetInitialSize(int Width, int Height)
{
    m_InitialWidth  = Width;
    m_InitialHeight = Height;
}

void EarthworksFXWindowBase::GetInitialSize(int& Width, int& Height) const
{
    Width  = m_InitialWidth;
    Height = m_InitialHeight;
}

#if PLATFORM_WIN32

void EarthworksFXWindowBase::Attach(HWND hWnd)
{
    m_hWnd = hWnd;
}

Win32NativeWindow EarthworksFXWindowBase::GetNativeWindow() const
{
    return Win32NativeWindow{m_hWnd};
}

void EarthworksFXWindowBase::ToggleFullscreenWindow()
{
  if (!m_hWnd) return;

#if PLATFORM_WIN32
    m_bBorderlessFullscreen = !m_bBorderlessFullscreen;

    if (m_bBorderlessFullscreen)
    {
        GetWindowRect(m_hWnd, &m_WindowRect);
        m_WindowStyle = GetWindowLong(m_hWnd, GWL_STYLE);

        SetWindowLong(m_hWnd, GWL_STYLE, m_WindowStyle & ~(WS_CAPTION | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_SYSMENU | WS_THICKFRAME));

        DEVMODE devMode = {};
        devMode.dmSize  = sizeof(DEVMODE);
        EnumDisplaySettings(nullptr, ENUM_CURRENT_SETTINGS, &devMode);

        SetWindowPos(
            m_hWnd,
            HWND_TOPMOST,
            devMode.dmPosition.x,
            devMode.dmPosition.y,
            devMode.dmPosition.x + devMode.dmPelsWidth,
            devMode.dmPosition.y + devMode.dmPelsHeight,
            SWP_FRAMECHANGED | SWP_NOACTIVATE);

        ShowWindow(m_hWnd, SW_MAXIMIZE);
    }
    else
    {
        SetWindowLong(m_hWnd, GWL_STYLE, m_WindowStyle);

        SetWindowPos(
            m_hWnd,
            HWND_NOTOPMOST,
            m_WindowRect.left,
            m_WindowRect.top,
            m_WindowRect.right - m_WindowRect.left,
            m_WindowRect.bottom - m_WindowRect.top,
            SWP_FRAMECHANGED | SWP_NOACTIVATE);

        ShowWindow(m_hWnd, SW_NORMAL);
    }
#endif
}

void EarthworksFXWindowBase::ExitBorderlessFullscreenIfActive()
{
    if (m_bBorderlessFullscreen)
        ToggleFullscreenWindow();
}

LRESULT EarthworksFXWindowBase::HandleMessage(UINT Message, WPARAM wParam, LPARAM lParam)
{
    switch (Message)
    {
        case WM_SYSKEYDOWN:
            if ((wParam == VK_RETURN) && (lParam & (1 << 29)))
            {
                ToggleFullscreenWindow();
                return 0;
            }
            break;

        default:
            break;
    }

    (void)wParam;
    (void)lParam;
    return 0;
}

#elif PLATFORM_LINUX && VULKAN_SUPPORTED

void EarthworksFXWindowBase::AttachVulkan(xcb_connection_t* pConnection, uint32_t WindowId, uint16_t Width, uint16_t Height)
{
    m_pXCBConnection = pConnection;
    m_XCBWindow      = WindowId;
    m_Width          = Width;
    m_Height         = Height;
}

LinuxNativeWindow EarthworksFXWindowBase::GetNativeWindow() const
{
    LinuxNativeWindow Window;
    Window.pXCBConnection = m_pXCBConnection;
    Window.WindowId       = m_XCBWindow;
    return Window;
}

#endif

} // namespace Diligent
