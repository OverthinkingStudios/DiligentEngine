#ifndef EARTHWORKSEDITOR_SRC_EARTHWORKSEDITOR_HPP_
#define EARTHWORKSEDITOR_SRC_EARTHWORKSEDITOR_HPP_

#include "EarthworksFXApplicationBase.hpp"

/// Earthworks Editor. The EarthworksFX base supplies the full terrain rendering
/// environment; this app keeps the default scene and demonstrates the handful of
/// hooks most editors actually use - extending the base behaviour rather than
/// replacing it (each override calls its base version).
class EarthworksEditor final : public Diligent::EarthworksFXApplicationBase
{
public:
    EarthworksEditor();

protected:
    void Initialize() override;
    void OnConfigureSettings(Diligent::EarthworksFXAppSettings& settings) override;
    void OnGraphicsReady() override;
    void OnUpdate(double current_time, double elapsed_time, bool do_update_ui) override;
    void OnRender() override;
    void UpdateUI() override;
};

#endif  // EARTHWORKSEDITOR_SRC_EARTHWORKSEDITOR_HPP_
