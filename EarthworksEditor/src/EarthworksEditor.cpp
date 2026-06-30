#include "EarthworksEditor.hpp"

#include "imgui.h"

EarthworksEditor::EarthworksEditor()
    : Diligent::EarthworksFXApplicationBase("Earthworks Editor", "earthworks-editor", overthinking::Env::Stage::Dev)
{
}

// Called right after the environment (logging/config) is up, before any graphics
// exist. Good place for cheap, device-independent setup.
void EarthworksEditor::Initialize() {
    spdlog::info("EarthworksEditor: starting up");
}

// Tweak the window/device defaults. The base already picks a sensible Vulkan
// windowed setup; the editor keeps the terrain scene (CreateScene stays true).
void EarthworksEditor::OnConfigureSettings(Diligent::EarthworksFXAppSettings& settings) {
    // e.g. Settings.WindowWidth = 1920; Settings.WindowHeight = 1080;
}

// Device, swap chain, ImGui and the terrain scene are all ready here. Create
// editor-specific GPU resources (gizmos, overlays, tool pipelines) in this hook.
void EarthworksEditor::OnGraphicsReady() {
}

// Per-frame update. Extend the base (camera + scene GUI), then add editor logic.
void EarthworksEditor::OnUpdate(double current_time, double elapsed_time, bool do_update_ui) {
    EarthworksFXApplicationBase::OnUpdate(current_time, elapsed_time, do_update_ui);

    // Editor-specific per-frame work (selection, tools, animation) goes here.
}

// Per-frame render. Let the base draw the terrain scene first, then layer the
// editor's own rendering (overlays, gizmos) on top.
void EarthworksEditor::OnRender() {
    EarthworksFXApplicationBase::OnRender();

    // Editor overlays drawn over the terrain would go here, using
    // GetRenderContext() / GetTargetFbo().
}

// ImGui pass. Keep the shared overlay (DrawCommonUI via the base), then add the
// editor's panels.
void EarthworksEditor::UpdateUI() {
    EarthworksFXApplicationBase::UpdateUI();

    ImGui::SetNextWindowPos(ImVec2(10, 140), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Editor"))
    {
        ImGui::TextUnformatted("This is an Earthworks Editor custom panel and could be whatever you want");
    }
    ImGui::End();
}

namespace Diligent
{

NativeAppBase* CreateApplication()
{
    return new EarthworksEditor();
}

} // namespace Diligent
