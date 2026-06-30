#include "EarthworksFXSample.hpp"

#include "imgui.h"

#include <filesystem>

namespace Diligent
{

namespace
{

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

EarthworksFXSample::EarthworksFXSample()
    : EarthworksFXApplicationBase("EarthworksFX", "EarthworksFX", overthinking::Env::Stage::Dev)
    , m_RenderContext(nullptr)
{
}

EarthworksFXSample::~EarthworksFXSample()
{
    if (m_Initialized)
    {
        ScopedFalcorFramework scope{&m_Framework};
        m_Earthworks.onShutdown();
    }

    m_FalcorWrapper.reset();
}

void EarthworksFXSample::ModifyEngineInitInfo(const ModifyEngineInitInfoAttribs& Attribs)
{
    EarthworksFXApplicationBase::ModifyEngineInitInfo(Attribs);

    Attribs.EngineCI.Features.ComputeShaders = DEVICE_FEATURE_STATE_ENABLED;
    Attribs.EngineCI.Features.DepthClamp     = DEVICE_FEATURE_STATE_OPTIONAL;
    Attribs.SCDesc.ColorBufferFormat = TEX_FORMAT_BGRA8_UNORM_SRGB;
}

EarthworksFXAppSettings EarthworksFXSample::GetAppSettings(bool IsInitialization)
{
    EarthworksFXAppSettings Settings;
    if (IsInitialization)
    {
        Settings.DeviceType   = RENDER_DEVICE_TYPE_VULKAN;
        Settings.VSync        = false;
        Settings.ShowUI       = true;
        Settings.WindowWidth  = 2560;
        Settings.WindowHeight = 1440;
    }
    return Settings;
}

void EarthworksFXSample::OnGraphicsInitialized()
{
    m_FalcorWrapper = std::make_unique<Falcor::EarthworksWrapper>();

    Falcor::SetFalcorDevice(m_pDevice, m_pImmediateContext, m_pSwapChain, m_pEngineFactory);
    Falcor::SetFalcorFramework(&m_Framework);
    Falcor::addDataDirectory(std::filesystem::current_path(), true);
    Falcor::addDataDirectory(std::filesystem::current_path() / "terrains", false);
    Falcor::addDataDirectory(std::filesystem::current_path() / "EarthworksFX", true);

    m_RenderContext = Falcor::RenderContext{m_pImmediateContext};

    ScopedFalcorFramework scope{&m_Framework};
    m_Earthworks.onLoad(&m_RenderContext);

    const auto& scDesc = m_pSwapChain->GetDesc();
    m_Earthworks.onResizeSwapChain(scDesc.Width, scDesc.Height);
    m_TargetFbo = Falcor::Fbo::createFromSwapChain(m_pSwapChain);

    m_Initialized = true;
}

void EarthworksFXSample::RenderSample()
{
    if (!m_Initialized)
        return;

    ScopedFalcorFramework scope{&m_Framework};
    m_Framework.SetAverageFrameTimeMs(m_fSmoothFPS > 0.f ? 1000.0 / static_cast<double>(m_fSmoothFPS) : 16.0);

    if (!m_TargetFbo)
        m_TargetFbo = Falcor::Fbo::createFromSwapChain(m_pSwapChain);

    m_Earthworks.onFrameRender(&m_RenderContext, m_TargetFbo);
}

void EarthworksFXSample::UpdateSample(double CurrTime, double ElapsedTime, bool DoUpdateUI)
{
    (void)CurrTime;
    (void)ElapsedTime;

    if (!m_Initialized)
        return;

    SyncInput();

    if (DoUpdateUI)
    {
        ScopedFalcorFramework scope{&m_Framework};
        m_Earthworks.onGuiRender(&m_Gui);
    }
}

void EarthworksFXSample::OnWindowResized(Uint32 Width, Uint32 Height)
{
    if (!m_Initialized)
        return;

    ScopedFalcorFramework scope{&m_Framework};
    m_Earthworks.onResizeSwapChain(Width, Height);
    m_TargetFbo = Falcor::Fbo::createFromSwapChain(m_pSwapChain);
}

void EarthworksFXSample::UpdateUI()
{
    DrawCommonUI();
    DrawDebugUI();
}

void EarthworksFXSample::DrawDebugUI()
{
    ImGui::SetNextWindowPos(ImVec2(10, 140), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("EarthworksFX Debug"))
    {
        ImGui::Checkbox("Debug grid (orientation + movement)", &m_Earthworks.debugGridEnabled());

        if (const auto& cam = m_Earthworks.getCamera())
        {
            const auto p = cam->getPosition();
            const auto t = cam->getTarget();
            ImGui::Text("cam pos: %.1f  %.1f  %.1f", p.x, p.y, p.z);
            ImGui::Text("cam tgt: %.1f  %.1f  %.1f", t.x, t.y, t.z);
        }
    }
    ImGui::End();
}

void EarthworksFXSample::SyncInput()
{
    const MouseState mouse = m_InputController.GetMouseState();

    // The Win32 InputController reports client-space pixels, but the Earthworks
    // camera code expects Falcor-style normalized [0,1] screen coordinates
    // (it gates on 'pos.x > 0 && pos.x < 1'), so normalize here.
    const auto& scDesc = m_pSwapChain->GetDesc();
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
    m_Earthworks.onMouseEvent(event);

    if (mouse.WheelDelta != 0.f)
    {
        event.type       = Falcor::MouseEvent::Type::Wheel;
        event.wheelDelta = Falcor::float2{0.f, mouse.WheelDelta};
        m_Earthworks.onMouseEvent(event);
    }
}

Falcor::FrameRate EarthworksFXSample::SampleFramework::getFrameRate() const
{
    return Falcor::FrameRate{};
}

Falcor::WindowInterface* EarthworksFXSample::SampleFramework::getWindow()
{
    return &m_Window;
}

NativeAppBase* CreateApplication()
{
    return new EarthworksFXSample();
}

} // namespace Diligent
