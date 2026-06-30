#include "TextureSplitTool.hpp"

#include "imgui.h"

TextureSplitTool::TextureSplitTool()
    : Diligent::EarthworksFXApplicationBase("TextureSplitTool", "texture-split-tool", overthinking::Env::Stage::Dev) {
}

TextureSplitTool::~TextureSplitTool() = default;

void TextureSplitTool::Initialize() {
}

void TextureSplitTool::OnConfigureSettings(Diligent::EarthworksFXAppSettings& settings) {
    // No terrain scene: build the environment + shaders only and render manually.
    settings.CreateScene = false;
}

void TextureSplitTool::OnModifyEngineInitInfo(const ModifyEngineInitInfoAttribs& attribs) {
}

void TextureSplitTool::OnGraphicsReady() {
}

void TextureSplitTool::OnRender() {
}

void TextureSplitTool::OnUpdate(double current_time, double elapsed_time, bool do_update_ui) {
}

void TextureSplitTool::OnWindowResized(Diligent::Uint32 width, Diligent::Uint32 height) {
}

void TextureSplitTool::UpdateUI() { 
    EarthworksFXApplicationBase::UpdateUI(); 
}

void TextureSplitTool::SyncInput() {
}

void TextureSplitTool::ReleaseSwapChainBuffers() {
}

bool TextureSplitTool::HandleSampleNativeMessage(const void* native_msg_data) {
    return false;
}

Diligent::AppBase::CommandLineStatus TextureSplitTool::ProcessSampleCommandLine(int argc, const char* const* argv) {
    return Diligent::AppBase::CommandLineStatus::OK;
}

namespace Diligent
{

NativeAppBase* CreateApplication()
{
    return new TextureSplitTool();
}

} // namespace Diligent, will be flattened soon(tm)
