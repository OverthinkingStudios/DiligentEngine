#pragma once

#include <cstdint>

// ---------------------------------------------------------------------------
// EarthworksFX runtime debug instrumentation.
//
// A single global instance (ew::gDebug), matching the global style of the
// renderer core (e.g. global_sun_direction in Earthworks_4.h), so it can be
// referenced from anywhere in the renderer and surfaced in the host
// application's ImGui without threading a parameter through every
// render/update signature.
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

    // --- CPU terrain shadow (_shadowEdges) ------------------------------
    // shadowSunAngle is the solver's sun elevation in radians along its locked
    // east-west path (0.95605 ~ 54.8 deg); shadowResolve is a one-shot request -
    // Earthworks_4::onFrameUpdate copies the angle into the solver, sets
    // requestNewShadow and clears the flag. The background solve takes a few
    // seconds; the sun direction and the shadow texture swap atomically when
    // it finishes.
    float shadowSunAngle = 0.95605f;
    bool  shadowResolve  = false;

    // Sync the render camera into the terrain manager each frame (terrain.setCamera).
    // When false the terrain keeps its last camera, so its frustum / tile visibility
    // stops following where we look - a way to test whether the terrain frustum is
    // pointing somewhere other than the render view.
    bool syncCamera = true;

    // --- terrain.onFrameRender passes ----------------------------------
    bool skydome      = true;
    bool terrainTiles = true;    // the indirect terrain-tile draw
    bool buildings    = true;    // far-LOD building volumes (buildingsRenderer)
    bool billboards   = true;    // terrain sprite/billboard quads
    bool plants       = true;    // plants_Root + plant clip/lod compute
    bool ribbons      = true;    // paraglider ribbons (glider mode)
    bool splines      = true;    // road / terrafector splines

    // Replace the terrain pixel shader's full shading (albedo * sun * shadow *
    // atmosphere in/outscatter) with a constant world-position pattern
    // (render_Tiles.hlsl, gConstColor). Splits the search space: pattern shows
    // up -> geometry/draw path is fine and the problem is in the shading
    // inputs; still nothing -> tile pipeline / draw args / camera.
    bool terrainConstColor = false;

    // --- Earthworks_4.onFrameRender post passes ------------------------
    // Render the 3D scene (terrain, globe, ...) straight into the swap chain
    // instead of hdrFbo, and skip the tonemapper. Removes the HDR buffer +
    // tonemapper from the pipeline entirely so anything the scene produces is
    // guaranteed to reach the screen. OFF is the normal path
    // (scene -> hdrFbo -> tonemapper -> swap chain).
    bool bypassHdr = false;
    bool tonemapper = true;    // no effect while bypassHdr is on
    // Tonemapper output mode (compute_tonemapper.hlsl). Because everything that
    // renders into the HDR FBO reaches the screen ONLY through this pass, it is the
    // prime suspect when the HDR content is black. Modes:
    //   0 = normal (ACES + colour cube)
    //   1 = raw HDR (saturate(hdr), no ACES/LUT) -> is there ANY content in hdrFbo?
    //   2 = solid test colour (ignores hdr)       -> does the pass reach the swapchain?
    int  tonemapperView = 0;
    bool overlay        = true;
    // Debug orientation aids (see debugGrid.hlsl / Earthworks_4::renderDebugGlobe):
    bool debugGlobe      = false;  // sparse compass globe (N red / S black), depth-tested so terrain occludes it
    bool debugGroundGrid = false;  // live quadtree leaf tiles colour-coded by LOD, on top
    bool debugEarthworksShader = false;
    bool debugEarthworksInfoGui = true;

    // --- ImGui ---------------------------------------------------------
    // The Earthworks editor GUI (terrain/vegetation editor windows) drawn by
    // Earthworks_4::onGuiRender. In vegetation mode this includes a full-screen
    // HUD window + a "vegetation builder" panel - i.e. screen-space rectangles
    // that are unaffected by the render-pass toggles above.
    bool earthworksGui = false;

    // --- terrafector / roads bake ---------------------------------------
    // bSplineAsTerrafector mirror: bake road splines into tiles at split time.
    // Defaults ON so the live bake runs; some shipped terrains already carry
    // their roads baked into the JP2 elevation instead. Toggles affect NEWLY
    // SPLIT tiles only - hit "rebake" to rebuild all.
    bool tfBakeRoads = true;
    // One-shot: force a full quadtree rebuild (terrain.reset(true)) so bake
    // toggle changes apply to every tile. Cleared by terrain.update().
    bool tfRebake = false;
    // TODO: 3D road overlay (render_spline). Off automatically while tfBakeRoads is
    // on: the overlay only draws when roads are NOT baked as terrafectors. But this
    // defaults true next to tfBakeRoads = true and the gating lives in the renderer,
    // so the UI shows a checked box that does nothing.
    bool tfShowRoadSpline = true;
    // Per-stage bisection of the priority-ordered bake stack (draw order IS
    // the priority system): 1 bakeLow -> 2 road bakeOnly (flattens terrain!)
    // -> 3 bakeHigh -> 4 mesh LOD6/4/2 -> 5 overlay -> 6 road LOD bins ->
    // 7 stamps -> 8 _top combiners.
    bool tfStageBakeLow      = true;
    bool tfStageRoadBakeOnly = true;
    bool tfStageBakeHigh     = true;
    bool tfStageMeshes       = true;
    bool tfStageOverlay      = true;
    bool tfStageRoadBins     = true;
    bool tfStageStamps       = true;
    bool tfStageTop          = true;

    // While > 0, each tile bake (splitRenderTopdown) reads back the elevation
    // centre texel (tileFbo color 0, R32F) BEFORE and AFTER the terrafector
    // stack (full GPU stall - debug only) and logs it with the tile's
    // lod/x/y, then decrements. A bicubic height of hundreds of metres
    // collapsing to ~0 is the signature of the bake flattening the tile to
    // y = 0.
    int tfBakeElevationStatsLeft = 0;
    // A/B test: bake with the roads-combined blend replaced by the plain
    // splines blend (RT0 loses its One/OneMinusSrcAlpha elevation override).
    // If a broken bake changes shape here, the RT0 blend translation or the
    // independent-blend feature is implicated.
    bool tfBakeNoElevationBlend = false;

    // Force a _terrainMode at runtime. The desktop host does not forward the
    // 1..7 keys to the renderer, so this is the way to switch modes from the
    // UI. -1 means "no request"; it is applied once and reset to -1.
    int requestTerrainMode = -1;
};

// Unlike DebugToggles these are NOT instant switches: each option is sampled
// once while a terrain is loading and has no effect on the running scene.
// Flip them before the next terrain (re)load. Expected to grow as more
// load-time behaviour becomes switchable.
struct DebugLoadOptions
{
    // Bake the building triangles into the semi-dynamic terrain shadow
    // heightfield so buildings cast (and receive) the same baked shadows as
    // the terrain. Sampled in Earthworks_4::onLoad where _shadowEdges::load
    // runs, just before the shadow solver thread starts.
    bool buildingShadows = true;
};

struct DebugMetrics
{
    int  terrainMode        = -1;
    bool vegetationEarlyOut = false; // vegetation mode returns before terrain tiles
    bool updateEarlyOut     = false; // terrain.update() skipped tile streaming for this mode

    // Per-pass draw submissions counted this frame.
    uint32_t skydomeDraws     = 0;
    uint32_t terrainTileDraws = 0;
    uint32_t buildingDraws    = 0;
    uint32_t billboardDraws   = 0;
    uint32_t plantDraws       = 0;
    uint32_t ribbonDraws      = 0;
    uint32_t splineDraws      = 0;
    uint32_t tonemapperDraws  = 0;
    uint32_t overlayDraws     = 0;
    uint32_t debugGlobeDraws  = 0;
    uint32_t debugGridDraws   = 0;

    // CPU-side scene counts (cheap, no GPU readback).
    uint32_t tilesUsed      = 0;
    uint32_t tilesFree      = 0;
    uint32_t ribbonsLoaded  = 0;
    uint32_t staticSplines  = 0;
    uint32_t dynamicSplines = 0;

    // Terrafector bake probe result (sticky - keeps the LAST probed tile;
    // filled while toggles.tfBakeElevationStatsLeft counts down).
    uint32_t tfProbeLod    = 0;
    float    tfProbeBefore = 0.f;
    float    tfProbeAfter  = 0.f;

    // GPU-side counts for the main view (CameraType_Main_Center), read back
    // from GC_feedback (filled by compute_tileBuildLookup). These prove what
    // the GPU actually packed into the indirect draw: if gpuTerrainBlocks is 0
    // the terrain draw has instanceCount 0 and cannot produce pixels, no
    // matter what the CPU-side draw counters say.
    uint32_t gpuTerrainTiles  = 0;
    uint32_t gpuTerrainBlocks = 0;
    uint32_t gpuTerrainTris   = 0;
    uint32_t gpuQuads         = 0;

    // Vegetation - from vegetation_feedback via the readback ring. All zeros
    // with no PSO failures in the log is the healthy state for a dataset that
    // ships no plant data.
    uint32_t vegInstances    = 0;   // clipLod-visible plant instances this frame
    uint32_t vegBlocks       = 0;   // 32-vertex blocks queued for the ribbon draws
    uint32_t vegBillboards   = 0;   // feedback numBillboard (cleared to 13 - a sentinel, not a count)
    uint32_t vegFrustDiscard = 0;   // clipLod frustum rejections
    uint32_t vegFeedbackAge  = 0;   // readback ring latency in frames (1-2 expected)

    // --- tile-split diagnostics ----------------------------------------
    // Why the quadtree does (or does not) refine past the root tile. The split
    // test (terrainManager::testForSplit) needs a tile to be BOTH in-frustum and
    // large enough on screen (lod_Pix). Both depend on the camera matrices, so a
    // matrix-convention mismatch shows up as "candidates 0 / inFrust false".
    bool     cameraMainInUse = false; // cameraViews[CameraType_Main_Center].bUse
    float    splitMaxLodPix  = 0.f;   // largest lod_Pix seen this frame (want > 150)
    bool     splitAnyInFrust = false; // any tile passed the frustum test
    uint32_t splitCandidates = 0;     // tiles that requested a split (forSplit set)
    uint32_t splitsPerformed = 0;     // splits actually executed this frame
    uint32_t splitBlockedData = 0;    // forSplit tile skipped: source data not ready
    bool     splitBlockedFree = false;// splitOne bailed early: fewer than 8 free tiles

    void resetCounters()
    {
        skydomeDraws = terrainTileDraws = buildingDraws = billboardDraws = plantDraws = 0;
        ribbonDraws = splineDraws = tonemapperDraws = overlayDraws = 0;
        debugGlobeDraws = debugGridDraws = 0;
        vegetationEarlyOut = false;
        updateEarlyOut = false;

        cameraMainInUse = splitAnyInFrust = splitBlockedFree = false;
        splitMaxLodPix  = 0.f;
        splitCandidates = splitsPerformed = splitBlockedData = 0;
        gpuTerrainTiles = gpuTerrainBlocks = gpuTerrainTris = gpuQuads = 0;
        vegInstances = vegBlocks = vegBillboards = vegFrustDiscard = vegFeedbackAge = 0;
    }
};

struct DebugState
{
    DebugToggles toggles;
    DebugLoadOptions loadOptions;   // sampled during terrain load, not live
    DebugMetrics live;  // accumulated during the current frame
    DebugMetrics shown; // snapshot of the last completed frame (stable for the UI)

    void beginFrame() { live.resetCounters(); }
    void endFrame() { shown = live; }
};

inline DebugState gDebug;

} // namespace ew
