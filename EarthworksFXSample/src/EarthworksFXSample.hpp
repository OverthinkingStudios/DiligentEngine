#pragma once

#include "EarthworksFXApplicationBase.hpp"

namespace Diligent
{

/// EarthworksFX terrain demo. Identical to the base app, plus a small debug
/// window (debug-grid toggle + camera readout) used during terrain bring-up.
class EarthworksFXSample final : public EarthworksFXApplicationBase
{
public:
    EarthworksFXSample();

protected:
    void UpdateUI() override final;

     void OnConfigureSettings(EarthworksFXAppSettings& s) override final;

private:
    /// App-specific ImGui (debug grid toggle + camera readout). Bring-up aid
    /// kept out of Earthworks_4 so re-porting that file stays clean.
    void DrawDebugUI();
};

} // namespace Diligent
