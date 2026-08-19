#pragma once

// ---------------------------------------------------------------------------
// Earthworks_4 - the terrain renderer's app-level class.
//
// Lifecycle the host shell drives: onLoad / onFrameRender (which calls
// onFrameUpdate itself, first thing) / onResizeSwapChain / onKeyEvent /
// onMouseEvent / onShutdown.
//
// It owns the terrain, the atmosphere (sun LUT + volumetric fog computes), the
// _shadowEdges CPU shadow-thread handoff, and the HDR pipeline: the scene
// renders into hdrFbo (R11G11B10F + D24S8), the tonemapper (ACES + colour-cube
// LUT, a GRAPHICS fullscreen triangle) writes the swap chain, then hdrFbo is
// blitted HALF-res into hdrPreviousFrame for the next frame's temporal effects
// (JHFAA).
// ---------------------------------------------------------------------------

#include <cstdint>
#include <string>
#include <vector>

#include "ewTypes.h"
#include "ewCamera.h"
#include "ewGpuContext.h"
#include "ewResources.h"
#include "ewShader.h"

#include "EarthworksDebug.h"

#include "terrain.h"
#include "atmosphere.h"

// Sun direction owned by the app level: written by the shadow-solver thread
// handoff, read when building the light buffer. Default is 15 degrees above the
// horizon, coming from +X.
inline ew::float3 global_sun_direction{0.96593f, -0.25882f, 0.f};

class Earthworks_4
{
public:
    void onLoad(ew::GpuContext* pGpu);
    void onFrameUpdate(ew::GpuContext* pGpu);
    void onFrameRender(ew::GpuContext* pGpu, const ew::Fbo::SharedPtr& pTargetFbo);
    void onShutdown();
    void onResizeSwapChain(uint32_t width, uint32_t height);
    bool onKeyEvent(const ew::KeyboardEvent& keyEvent);
    bool onMouseEvent(const ew::MouseEvent& mouseEvent);

    /// Editor GUI hook - not implemented, the body is empty.
    void onGuiRender();

    // move to a postprocess class
    void loadColorCube(std::string name);

    // --- host-app accessors -------------------------------------------------
    bool&                        debugGridEnabled() { return showDebugGrid; }
    const ew::Camera::SharedPtr& getCamera() const { return camera; }

    /// Name of the loaded terrain (terrainSettings.name).
    std::string getTerrainName() const { return terrain.getTerrainName(); }

    /// Command-line terrain override (`-terrain <dir-or-settings.json>`) -
    /// forwards to terrainManager; call before onLoad.
    static void setTerrainOverride(const std::string& pathOrDir) { terrainManager::sTerrainOverride = pathOrDir; }

private:
    // --- the terrain + atmosphere subsystems ------------------------------
    terrainManager terrain;
    atmosphereAndFog atmosphere;     // make directly available in terrain include there

    // --- HDR pipeline -----------------------------------------------------
    ew::Fbo::SharedPtr     hdrFbo;
    ew::Texture::SharedPtr hdrPreviousFrame;   // HALF res - JHFAA temporal feedback

    struct
    {
        ew::pixelShader        tonemapper;
        ew::Texture::SharedPtr colorCube;
    } postProcess;
    // --- debug orientation / movement aids (hlsl/debugGrid.hlsl) -----------
    // The globe is depth-tested (terrain will occlude it); the ground grid is
    // drawn on top. One pixelShader instance, depth state flipped per draw
    // (the PSO cache keeps both variants).
    void renderDebugGlobe(ew::GpuContext* pGpu, const ew::Fbo::SharedPtr& pFbo);
    void renderDebugGroundGrid(ew::GpuContext* pGpu, const ew::Fbo::SharedPtr& pFbo);
    void setDebugGridConstants(int drawMode);

    bool            showDebugGrid = true;
    ew::pixelShader debugGridShader;
    bool            debugGridLoaded = false;

    static constexpr uint32_t kMaxDebugTileRects = 1024; // >= terrain tile pool (997)
    ew::Buffer::SharedPtr  debugTileRectBuffer;   // one float4 per tile: origin.x, origin.z, size, lod
    ew::Buffer::SharedPtr  debugTileHeightBuffer; // terrain height per tile (0 for the placeholder grid)
    std::vector<ew::float4> debugTileRects;
    std::vector<float>      debugTileHeights;
    bool                    debugTilesDirty = true;

    ew::Camera::SharedPtr camera;
    ew::float2            screenSize{0.f, 0.f};
    ew::float2            screenMouseScale{1.f, 1.f};
    ew::float2            screenMouseOffset{0.f, 0.f};
};
