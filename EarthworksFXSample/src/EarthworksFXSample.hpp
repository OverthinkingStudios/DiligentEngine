#pragma once

#include "EarthworksFXApplicationBase.hpp"

namespace Diligent
{

/// EarthworksFX terrain demo. Identical to the base app, plus a small debug
/// window (debug-grid toggle + camera readout).
class EarthworksFXSample final : public EarthworksFXApplicationBase
{
public:
    EarthworksFXSample();

protected:
    void UpdateUI() override final;

     void OnConfigureSettings(EarthworksFXAppSettings& s) override final;

private:
    /// App-specific ImGui: debug grid toggle + camera readout. Lives here
    /// rather than in Earthworks_4 to keep the renderer free of host UI.
    void DrawDebugUI();
};

} // namespace Diligent
