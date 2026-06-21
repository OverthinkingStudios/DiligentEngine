#include "EarthworksFXSample.hpp"

#include <cstdio>
#include <filesystem>

namespace Diligent
{

FILE* EarthworksFXSample::s_LogFile = nullptr;

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

Falcor::Fbo::SharedPtr CreateSwapChainTargetFbo(ISwapChain* pSwapChain)
{
    if (!pSwapChain)
        return nullptr;

    const auto& scDesc = pSwapChain->GetDesc();
    Falcor::Fbo::Desc desc;
    desc.setColorTarget(0, scDesc.ColorBufferFormat);
    desc.setDepthStencilTarget(scDesc.DepthBufferFormat);
    return Falcor::Fbo::create2D(scDesc.Width, scDesc.Height, desc);
}

} // namespace

EarthworksFXSample::EarthworksFXSample()
    : m_RenderContext(nullptr)
{
}

EarthworksFXSample::~EarthworksFXSample()
{
    if (m_Initialized)
    {
        ScopedFalcorFramework scope{&m_Framework};
        m_Earthworks.onShutdown();
    }

    if (s_LogFile)
    {
        std::fclose(s_LogFile);
        s_LogFile = nullptr;
    }
}

void EarthworksFXSample::ModifyEngineInitInfo(const ModifyEngineInitInfoAttribs& Attribs)
{
    SampleBase::ModifyEngineInitInfo(Attribs);

    Attribs.EngineCI.Features.ComputeShaders = DEVICE_FEATURE_STATE_ENABLED;
    Attribs.EngineCI.Features.DepthClamp     = DEVICE_FEATURE_STATE_OPTIONAL;
    Attribs.SCDesc.ColorBufferFormat         = TEX_FORMAT_R11G11B10_FLOAT;
}

DesiredApplicationSettings EarthworksFXSample::GetDesiredApplicationSettings(bool IsInitialization)
{
    DesiredApplicationSettings Settings;
    if (IsInitialization)
    {
        Settings.SetDeviceType(RENDER_DEVICE_TYPE_VULKAN);
        Settings.SetVSync(true);
        Settings.SetWindowWidth(2560);
        Settings.SetWindowHeight(1440);
    }
    return Settings;
}

void EarthworksFXSample::Initialize(const SampleInitInfo& InitInfo)
{
    SampleBase::Initialize(InitInfo);

    s_LogFile = std::fopen("log.txt", "w");
    if (s_LogFile)
    {
        std::fprintf(s_LogFile, "EarthworksFXSample::Initialize()\n");
        std::fflush(s_LogFile);
    }
    m_Earthworks.logFile = s_LogFile;

    Falcor::SetFalcorDevice(m_pDevice, m_pImmediateContext, m_pSwapChain);
    Falcor::SetFalcorFramework(&m_Framework);
    Falcor::addDataDirectory(std::filesystem::current_path(), true);
    Falcor::addDataDirectory(std::filesystem::current_path() / "terrains", false);
    Falcor::addDataDirectory(std::filesystem::current_path() / "EarthworksFX", true);

    m_RenderContext = Falcor::RenderContext{m_pImmediateContext};

    const auto& scDesc = m_pSwapChain->GetDesc();
    m_Earthworks.onResizeSwapChain(scDesc.Width, scDesc.Height);
    m_TargetFbo = CreateSwapChainTargetFbo(m_pSwapChain);

    {
        ScopedFalcorFramework scope{&m_Framework};
        m_Earthworks.onLoad(&m_RenderContext);
    }

    m_Initialized = true;
}

void EarthworksFXSample::Render()
{
    if (!m_Initialized)
        return;

    ScopedFalcorFramework scope{&m_Framework};
    m_Framework.SetAverageFrameTimeMs(m_fSmoothFPS > 0.f ? 1000.0 / static_cast<double>(m_fSmoothFPS) : 16.0);

    if (!m_TargetFbo)
        m_TargetFbo = CreateSwapChainTargetFbo(m_pSwapChain);

    m_Earthworks.onFrameRender(&m_RenderContext, m_TargetFbo);
}

void EarthworksFXSample::Update(double CurrTime, double ElapsedTime, bool DoUpdateUI)
{
    SampleBase::Update(CurrTime, ElapsedTime, DoUpdateUI);

    if (!m_Initialized)
        return;

    SyncInput();

    if (DoUpdateUI)
    {
        ScopedFalcorFramework scope{&m_Framework};
        m_Earthworks.onGuiRender(&m_Gui);
    }
}

void EarthworksFXSample::WindowResize(Uint32 Width, Uint32 Height)
{
    SampleBase::WindowResize(Width, Height);

    if (!m_Initialized)
        return;

    ScopedFalcorFramework scope{&m_Framework};
    m_Earthworks.onResizeSwapChain(Width, Height);
    m_TargetFbo = CreateSwapChainTargetFbo(m_pSwapChain);
}

void EarthworksFXSample::UpdateUI()
{
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("EarthworksFX", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Falcor terrain port (1:1 algorithm, Diligent host).");
        ImGui::Text("FPS: %.1f", m_fSmoothFPS);
        ImGui::Text("Terrain: terrains/switserland_Steg/ (pwd-relative)");
        ImGui::Text("API: Vulkan");
        ImGui::Text("See EarthworksFX/MIGRATION.md for remaining wiring.");
    }
    ImGui::End();
}

void EarthworksFXSample::SyncInput()
{
    const MouseState mouse = m_InputController.GetMouseState();

    Falcor::MouseEvent event{};
    event.pos = Falcor::float2{static_cast<float>(mouse.PosX), static_cast<float>(mouse.PosY)};

    if (mouse.ButtonFlags & MouseState::BUTTON_FLAG_LEFT)
        event.buttons = Falcor::MouseEvent::Buttons::Left;

    if (mouse.ButtonFlags & MouseState::BUTTON_FLAG_RIGHT)
        event.buttons = static_cast<Falcor::MouseEvent::Buttons>(static_cast<int>(event.buttons) | static_cast<int>(Falcor::MouseEvent::Buttons::Right));

    if (mouse.ButtonFlags & MouseState::BUTTON_FLAG_WHEEL)
    {
        event.type       = Falcor::MouseEvent::Type::Wheel;
        event.wheelDelta = Falcor::float2{0.f, mouse.WheelDelta};
    }
    else if (mouse.ButtonFlags & (MouseState::BUTTON_FLAG_LEFT | MouseState::BUTTON_FLAG_RIGHT))
    {
        event.type = Falcor::MouseEvent::Type::ButtonDown;
    }
    else
    {
        event.type = Falcor::MouseEvent::Type::Move;
    }

    ScopedFalcorFramework scope{&m_Framework};
    m_Earthworks.onMouseEvent(event);

    // TODO: map keyboard events (D toggles editor GUI, Q hides GUI in original Earthworks_4).
}

Falcor::FrameRate EarthworksFXSample::SampleFramework::getFrameRate() const
{
    return Falcor::FrameRate{};
}

Falcor::WindowInterface* EarthworksFXSample::SampleFramework::getWindow()
{
    return &m_Window;
}

SampleBase* CreateSample()
{
    return new EarthworksFXSample();
}

} // namespace Diligent
