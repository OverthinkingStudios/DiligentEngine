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

    // _basePath without extension, e.g. <dirRoot>/buildings/rappersville;
    // reads <basePath>.info.txt (vertex count) + <basePath>.raw (vertices).
    // Missing files log a warning and leave the renderer disabled.
    void load(const std::string& _basePath);

    // Per-frame LightsCB constants + terrainShadow texture, same values the
    // terrain shader gets (call from terrainManager::updateShaderConstants).
    void updateShaderConstants(Texture::SharedPtr _terrainShadow, const shaderLightBuffer& _buffer);

    // Atmosphere textures, bound once after load (call from Earthworks_4::onLoad).
    void setAtmosphere(Texture::SharedPtr _inscatter, Texture::SharedPtr _outscatter, Texture::SharedPtr _sunlight);

    // Draws all buildings; no-op when not loaded or toggled off
    // (ew::gDebug.toggles.buildings).
    void render(RenderContext* _renderContext, const Fbo::SharedPtr& _fbo, const GraphicsState::Viewport& _viewport,
                const rmcv::mat4& _view, const rmcv::mat4& _viewproj, const float3& _eye);

    bool loaded() const { return numTriangles > 0; }

private:
    pixelShader        shader;
    Buffer::SharedPtr  vertexData;
    Sampler::SharedPtr sampler_Clamp;
    int                numTriangles = 0;
};
