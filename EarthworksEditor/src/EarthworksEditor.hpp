#ifndef EARTHWORKSEDITOR_SRC_EARTHWORKSEDITOR_HPP_
#define EARTHWORKSEDITOR_SRC_EARTHWORKSEDITOR_HPP_

#include "EarthworksFXApplicationBase.hpp"

/// Earthworks Editor. The EarthworksFX base supplies the full rendering
/// environment; the editor only brands the app (title + AppData folder) and is
/// the place to grow editor-specific tooling via the base's On* hooks.
class EarthworksEditor final : public Diligent::EarthworksFXApplicationBase
{
public:
    EarthworksEditor();
};

#endif  // EARTHWORKSEDITOR_SRC_EARTHWORKSEDITOR_HPP_
