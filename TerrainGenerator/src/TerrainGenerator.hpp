#ifndef TERRAINGENERATOR_SRC_TERRAINGENERATOR_HPP_
#define TERRAINGENERATOR_SRC_TERRAINGENERATOR_HPP_

#include "EarthworksFXApplicationBase.hpp"

/// Earthworks Terrain Generator. Like TextureSplitTool, this app runs WITHOUT a
/// terrain scene (EarthworksFXAppSettings::CreateScene == false) and drives its
/// own rendering, but it only overrides the hooks it actually needs: opting out
/// of the scene and drawing its own ImGui panel.
class TerrainGenerator final : public Diligent::EarthworksFXApplicationBase
{
public:
    TerrainGenerator();

protected:
    /// Tweak window/device settings. This is where the tool opts out of the scene.
    void OnConfigureSettings(Diligent::EarthworksFXAppSettings& settings) override;

    /// App ImGui pass. Keep the shared overlay (via the base), then add panels.
    void UpdateUI() override;
};

#endif  // TERRAINGENERATOR_SRC_TERRAINGENERATOR_HPP_
