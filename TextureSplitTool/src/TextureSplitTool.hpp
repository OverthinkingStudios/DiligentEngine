#ifndef TEXTURESPLITTOOL_SRC_TEXTURESPLITTOOL_HPP_
#define TEXTURESPLITTOOL_SRC_TEXTURESPLITTOOL_HPP_

#include "EarthworksFXApplicationBase.hpp"

/// TextureSplitTool - an EarthworksFX app that runs WITHOUT a terrain scene
/// (EarthworksFXAppSettings::CreateScene == false). It still gets the complete
/// rendering environment (device, swap chain, ImGui, Falcor device/framework and
/// the Earthworks shaders), and drives all rendering itself.
///
/// Every overridable base hook is declared here - most are empty - so this class
/// doubles as a reference for what an EarthworksFX application can customize.
class TextureSplitTool final : public Diligent::EarthworksFXApplicationBase
{
public:
    TextureSplitTool();
    virtual ~TextureSplitTool();

protected:
    // --- one-time setup -----------------------------------------------------

    /// After logging/config init, before any graphics exist.
    void Initialize() override;

    /// Tweak window/device settings. This is where the tool opts out of the scene.
    void OnConfigureSettings(Diligent::EarthworksFXAppSettings& settings) override;

    /// Tweak engine creation (extra device features, swap chain format, ...).
    void OnModifyEngineInitInfo(const ModifyEngineInitInfoAttribs& attribs) override;

    /// Device, swap chain, ImGui and the rendering environment are ready. Create
    /// the tool's GPU resources / pipelines here.
    void OnGraphicsReady() override;

    // --- per-frame loop -----------------------------------------------------

    /// Per-frame scene render. With no terrain scene, the tool renders here using
    /// GetRenderContext() / GetTargetFbo().
    void OnRender() override;

    /// Per-frame update.
    void OnUpdate(double current_time, double elapsed_time, bool do_update_ui) override;

    /// Swap-chain resize. Recreate size-dependent targets here, e.g.
    /// SetTargetFbo(Falcor::Fbo::createFromSwapChain(m_pSwapChain)).
    void OnWindowResized(Diligent::Uint32 Width, Diligent::Uint32 Height) override;

    /// App ImGui pass. Call DrawCommonUI() to keep the shared overlay.
    void UpdateUI() override;

    // --- input --------------------------------------------------------------

    /// Feed live mouse state into the (absent) Earthworks camera. Unused here.
    void SyncInput() override;

    // --- misc ---------------------------------------------------------------

    /// Release swap-chain-sized resources before a resize/fullscreen switch.
    void ReleaseSwapChainBuffers() override;

    /// Handle a raw platform message. Return true if consumed.
    bool HandleSampleNativeMessage(const void* native_msg_data) override;

    /// Parse app-specific command-line arguments.
    Diligent::AppBase::CommandLineStatus ProcessSampleCommandLine(int argc, const char* const* argv) override;
};

#endif  // TEXTURESPLITTOOL_SRC_TEXTURESPLITTOOL_HPP_
