#pragma once

// ---------------------------------------------------------------------------
// Buildings far-LOD renderer ("rappersville" data set).
//
// PORT NOTE: this is NEW code, not part of the ported Earthworks tree. The
// original feature lived directly in terrain.cpp / glider.h but was commented
// out in the port (the CPU-side _buildingVertex struct came from glider.h,
// which was never ported). All building-related code is delegated to this
// class so the ported files only need one-liner load() / render() /
// updateShaderConstants() / setAtmosphere() calls - easy to keep when the
// core sources get re-copied from upstream.
//
// Renders flat-shaded building volumes (0.35 grey walls, darker roofs) with
// sun + terrain shadow + atmospheric fog - the "Far" LOD, there is no fancier
// building renderer in the source. Gated at runtime by
// ew::gDebug.toggles.buildings.
// ---------------------------------------------------------------------------

#include "Falcor.h"
#include "pixelShader.h"

#include <cstdint>
#include <string>
#include <vector>

struct shaderLightBuffer;   // vegetationBuilder.h

using namespace Falcor;

class buildingsRenderer
{
public:
    // CPU mirror of _buildingVertex in render_Buildings_Far.hlsl (48 bytes).
    // Originally declared in glider.h (not ported). Scalars follow each float3
    // at 16-byte boundaries, so the C++ and DXC/SPIR-V layouts match (see
    // BRINGUP_NOTES F9 lesson).
    struct vertex
    {
        float3   pos;       // world-space
        uint32_t material;
        float3   normal;
        float    pad0;
        float2   uv;
        float2   pad1;
    };
    static_assert(sizeof(vertex) == 48, "must match _buildingVertex in render_Buildings_Far.hlsl");

    // One x/z grid cell of buildings, a contiguous vertex range in vertexData
    // (triangles are bucketed by centroid and sorted at load). bbMin/bbMax is
    // the actual x/z extent of the verts in the cell, so triangles hanging
    // over the cell edge are still culled conservatively.
    struct chunk
    {
        float2   bbMin;
        float2   bbMax;
        uint32_t firstVertex;
        uint32_t numVertices;
    };

    // _basePath without extension, e.g. <dirRoot>/buildings/rappersville;
    // reads <basePath>.info.txt (vertex count) + <basePath>.raw (vertices).
    // Missing files log a warning and leave the renderer disabled.
    void load(const std::string& _basePath);

    // Per-frame LightsCB constants + terrainShadow texture, same values the
    // terrain shader gets (call from terrainManager::updateShaderConstants).
    void updateShaderConstants(Texture::SharedPtr _terrainShadow, const shaderLightBuffer& _buffer);

    // Atmosphere textures, bound once after load (call from Earthworks_4::onLoad).
    void setAtmosphere(Texture::SharedPtr _inscatter, Texture::SharedPtr _outscatter, Texture::SharedPtr _sunlight);

    // Draws the building chunks whose x/z bounds overlap any of the visible
    // terrain tile rects (same float4 layout as getDebugTileRects: x, z,
    // size, lod - see terrainManager::getVisibleTileRects). An empty list
    // means "no visibility info" and draws everything, so buildings never
    // vanish if the tile flags are stale. No-op when not loaded or toggled
    // off (ew::gDebug.toggles.buildings).
    void render(RenderContext* _renderContext, const Fbo::SharedPtr& _fbo, const GraphicsState::Viewport& _viewport,
                const rmcv::mat4& _view, const rmcv::mat4& _viewproj, const float3& _eye,
                const std::vector<float4>& _visibleTileRects);

    bool loaded() const { return numTriangles > 0; }

private:
    void buildChunks(std::vector<vertex>& _verts);

    pixelShader        shader;
    Buffer::SharedPtr  vertexData;
    Sampler::SharedPtr sampler_Clamp;
    int                numTriangles = 0;
    std::vector<chunk> chunks;          // grid cells, row-major, built in load()
};
