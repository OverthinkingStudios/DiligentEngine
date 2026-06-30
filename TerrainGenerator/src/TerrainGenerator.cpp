#include "TerrainGenerator.hpp"

#include "imgui.h"

TerrainGenerator::TerrainGenerator()
    : Diligent::EarthworksFXApplicationBase("Earthworks Terrain Generator", "terrain-generator", overthinking::Env::Stage::Dev) {
}

void TerrainGenerator::OnConfigureSettings(Diligent::EarthworksFXAppSettings& settings) {
    // No terrain scene: build the environment + shaders only and render manually.
    settings.CreateScene = false;
}

void TerrainGenerator::UpdateUI() {
    EarthworksFXApplicationBase::UpdateUI();

    ImGui::SetNextWindowPos(ImVec2(10, 140), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Terrain Generator"))
    {
        ImGui::TextUnformatted("This is an Earthworks Terrain Generator custom panel and could be whatever you want");
    }
    ImGui::End();
}

namespace Diligent
{

NativeAppBase* CreateApplication()
{
    return new TerrainGenerator();
}

} // namespace Diligent
