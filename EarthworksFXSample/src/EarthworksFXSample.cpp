#include "EarthworksFXSample.hpp"

#include "imgui.h"
#include "EarthworksDebug.h"

namespace Diligent
{

EarthworksFXSample::EarthworksFXSample()
    : EarthworksFXApplicationBase("EarthworksFX", "EarthworksFX", overthinking::Env::Stage::Dev)
{
}

void EarthworksFXSample::UpdateUI()
{
    DrawCommonUI();
    DrawDebugUI();
}

void EarthworksFXSample::OnConfigureSettings(EarthworksFXAppSettings& s) {
    
}

void EarthworksFXSample::DrawDebugUI()
{
    // Everything here reads the terrain scene; skip it entirely when the app
    // runs without one (EarthworksFXAppSettings::CreateScene == false).
    if (!HasEarthworksScene())
        return;

    ImGui::SetNextWindowPos(ImVec2(10, 140), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("EarthworksFX Debug"))
    {
        Earthworks_4& earthworks = GetEarthworks();
        ImGui::Checkbox("Debug grid (orientation + movement)", &earthworks.debugGridEnabled());
        ImGui::Checkbox("Earthworks debug 2d shaders", &ew::gDebug.toggles.debugEarthworksShader);
        ImGui::Checkbox("Earthworks debug gui", &ew::gDebug.toggles.debugEarthworksInfoGui);

        if (const auto& cam = earthworks.getCamera())
        {
            const auto p = cam->getPosition();
            const auto t = cam->getTarget();
            ImGui::Text("cam pos: %.1f  %.1f  %.1f", p.x, p.y, p.z);
            ImGui::Text("cam tgt: %.1f  %.1f  %.1f", t.x, t.y, t.z);
            if (UseFirstPersonCamera())
                ImGui::Text("move speed: %.1f m/s (mouse wheel)", GetFirstPersonCamera().GetMoveSpeed());
        }
    }
    ImGui::End();
}

NativeAppBase* CreateApplication()
{
    return new EarthworksFXSample();
}

} // namespace Diligent
