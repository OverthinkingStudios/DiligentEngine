#pragma once

#include <cstdint>

// ---------------------------------------------------------------------------
// EarthworksFX runtime debug / bring-up instrumentation.
//
// A single global instance (ew::gDebug), matching the existing global style in
// the ported core (e.g. global_sun_direction in Earthworks_4.h), so it can be
// referenced from the otherwise-untouched Earthworks renderer with small,
// additive edits and surfaced in the host application's ImGui without threading
// a parameter through every render/update signature.
//
// Two responsibilities:
//   * Toggles  - enable/disable individual render passes and compute stages at
//                runtime to bisect rendering problems.
//   * Metrics  - per-frame counters (which passes ran, how many draws, scene
//                tile counts, ...) surfaced live in ImGui.
//
// All of this is debugging scaffolding; it has no effect on a release path that
// leaves every toggle enabled.
// ---------------------------------------------------------------------------

namespace ew
{

// _terrainMode mirror (the enum lives in terrain.h, which we don't want to pull
// into the host app). Keep the values in sync with _terrainMode.
enum class TerrainModeId : int
{
    Vegetation     = 0,
    Ecotope        = 1,
    Terrafector    = 2,
    Roads          = 3,
    Glider         = 4,
    TerrainBuilder = 5,
    TextureTool    = 6,
    Count          = 7,
};

inline const char* TerrainModeName(int mode)
{
    switch (mode)
    {
    case 0: return "vegetation";
    case 1: return "ecotope";
    case 2: return "terrafector";
    case 3: return "roads";
    case 4: return "glider";
    case 5: return "terrainBuilder";
    case 6: return "textureTool";
    default: return "<unknown>";
    }
}

struct DebugToggles
{
    // --- onFrameUpdate (compute) ---------------------------------------
    bool atmosphere    = true;   // sun-in-atmosphere + volumetric fog compute
    bool terrainUpdate = true;   // terrain.update(): tile stream / clip / lod compute

    // --- terrain.onFrameRender passes ----------------------------------
    bool skydome      = true;
    bool terrainTiles = true;    // the indirect terrain-tile draw
    bool billboards   = true;    // terrain sprite/billboard quads
    bool plants       = true;    // plants_Root + plant clip/lod compute
    bool ribbons      = true;    // paraglider ribbons (glider mode)
    bool splines      = true;    // road / terrafector splines

    // --- Earthworks_4.onFrameRender post passes ------------------------
    bool tonemapper = true;
    bool overlay    = true;
    bool debugGrid  = true;

    // --- ImGui ---------------------------------------------------------
    // The Earthworks editor GUI (terrain/vegetation editor windows) drawn by
    // Earthworks_4::onGuiRender. In vegetation mode this includes a full-screen
    // HUD window + a "vegetation builder" panel - i.e. screen-space rectangles
    // that are unaffected by the render-pass toggles above.
    bool earthworksGui = false;

    // Force a _terrainMode at runtime. The desktop host does not forward the
    // 1..7 keys to the renderer, so this is the way to switch modes from the
    // UI. -1 means "no request"; it is applied once and reset to -1.
    int requestTerrainMode = -1;
};

struct DebugMetrics
{
    int  terrainMode        = -1;
    bool vegetationEarlyOut = false; // vegetation mode returns before terrain tiles

    // Per-pass draw submissions counted this frame.
    uint32_t skydomeDraws     = 0;
    uint32_t terrainTileDraws = 0;
    uint32_t billboardDraws   = 0;
    uint32_t plantDraws       = 0;
    uint32_t ribbonDraws      = 0;
    uint32_t splineDraws      = 0;
    uint32_t tonemapperDraws  = 0;
    uint32_t overlayDraws     = 0;
    uint32_t debugGridDraws   = 0;

    // CPU-side scene counts (cheap, no GPU readback).
    uint32_t tilesUsed      = 0;
    uint32_t tilesFree      = 0;
    uint32_t ribbonsLoaded  = 0;
    uint32_t staticSplines  = 0;
    uint32_t dynamicSplines = 0;

    void resetCounters()
    {
        skydomeDraws = terrainTileDraws = billboardDraws = plantDraws = 0;
        ribbonDraws = splineDraws = tonemapperDraws = overlayDraws = debugGridDraws = 0;
        vegetationEarlyOut = false;
    }
};

struct DebugState
{
    DebugToggles toggles;
    DebugMetrics live;  // accumulated during the current frame
    DebugMetrics shown; // snapshot of the last completed frame (stable for the UI)

    void beginFrame() { live.resetCounters(); }
    void endFrame() { shown = live; }
};

inline DebugState gDebug;

} // namespace ew
