#pragma once

// ---------------------------------------------------------------------------
// Buildings far-LOD renderer ("rappersville" data set).
//
// PORT-REVIEW (step 6 salvage): carried over from the previous port
// (legacy/core/buildings.*) - developer-verified working code with chunk
// culling the original lacked. This is NEW code, not part of the original
// Earthworks tree: the original feature lived directly in terrain.cpp /
// glider.h and was commented out. All building-related code is delegated to
// this class so the ported files only need one-liner load() / render() /
// updateShaderConstants() / setAtmosphere() calls. The step-6 adaptation is
// purely mechanical: Falcor compat surface -> ew:: (named binds, glm
// matrices, Diligent state descs); the culling and data layout are unchanged.
//
// Renders flat-shaded building volumes (0.35 grey walls, darker roofs) with
// sun + terrain shadow + atmospheric fog - the "Far" LOD, there is no fancier
// building renderer in the source. Gated at runtime by
// ew::gDebug.toggles.buildings.
// ---------------------------------------------------------------------------

#include "ewGpuContext.h"
#include "ewResources.h"
#include "ewShader.h"

#include <cstdint>
#include <string>
#include <vector>

struct shaderLightBuffer;   // vegetationBuilder.h

class buildingsRenderer
{
public:
    // CPU mirror of _buildingVertex in render_Buildings_Far.hlsl (48 bytes).
    // Originally declared in glider.h (not ported). Scalars follow each float3
    // at 16-byte boundaries, so the C++ and DXC/SPIR-V layouts match (see
    // BRINGUP_NOTES F9 lesson).
    struct vertex
    {
        ew::float3 pos;       // world-space
        uint32_t   material;
        ew::float3 normal;
        float      pad0;
        ew::float2 uv;
        ew::float2 pad1;
    };
    static_assert(sizeof(vertex) == 48, "must match _buildingVertex in render_Buildings_Far.hlsl");

    // One x/z grid cell of buildings, a contiguous vertex range in vertexData
    // (triangles are bucketed by centroid and sorted at load). bbMin/bbMax is
    // the actual x/z extent of the verts in the cell, so triangles hanging
    // over the cell edge are still culled conservatively.
    struct chunk
    {
        ew::float2 bbMin;
        ew::float2 bbMax;
        uint32_t   firstVertex;
        uint32_t   numVertices;
    };

    // _basePath without extension, e.g. <dirRoot>/buildings/rappersville;
    // reads <basePath>.info.txt (vertex count) + <basePath>.raw (vertices).
    // Missing files log loudly and leave the renderer disabled (graceful -
    // most terrains ship no building data).
    void load(const std::string& _basePath);

    // Per-frame LightsCB constants + terrainShadow texture, same values the
    // terrain shader gets (call from terrainManager::updateShaderConstants).
    void updateShaderConstants(ew::Texture::SharedPtr _terrainShadow, const shaderLightBuffer& _buffer);

    // Atmosphere textures, bound once after load (call from Earthworks_4::onLoad).
    void setAtmosphere(ew::Texture::SharedPtr _inscatter, ew::Texture::SharedPtr _outscatter, ew::Texture::SharedPtr _sunlight);

    // Splat the building triangles into a _dim x _dim max-height grid
    // (row-major, row = world z, column = world x, world origin at the grid
    // centre - the same mapping shadow() in render_Common.hlsli uses). Cells
    // covered by a triangle take max(cell, interpolated triangle height);
    // every vertex also splats its own cell so buildings smaller than one
    // cell (~10 m) still register. Used by _shadowEdges::load() to make the
    // baked terrain shadows include buildings as casters. Buildings are
    // static, so calling this once before the shadow solver thread starts
    // needs no synchronization.
    void overlayShadowHeights(float* _height, int _dim, float _metersPerPixel) const;

    // Draws the building chunks whose x/z bounds overlap any of the visible
    // terrain tile rects (same float4 layout as getVisibleTileRects: x, z,
    // size, lod). An empty list means "no visibility info" and draws
    // everything, so buildings never vanish if the tile flags are stale.
    // _view/_viewproj follow the ewCamera upload convention (transposed glm).
    // No-op when not loaded or toggled off (ew::gDebug.toggles.buildings).
    void render(ew::GpuContext* _renderContext, const ew::Fbo::SharedPtr& _fbo,
                const glm::mat4& _view, const glm::mat4& _viewproj, const ew::float3& _eye,
                const std::vector<ew::float4>& _visibleTileRects);

    bool loaded() const { return numTriangles > 0; }

private:
    void buildChunks(std::vector<vertex>& _verts);

    ew::pixelShader        shader;
    ew::Buffer::SharedPtr  vertexData;
    ew::Sampler::SharedPtr sampler_Clamp;
    int                    numTriangles = 0;
    std::vector<chunk>     chunks;      // grid cells, row-major, built in load()
    std::vector<vertex>    cpuVerts;    // CPU copy kept for overlayShadowHeights()
};
