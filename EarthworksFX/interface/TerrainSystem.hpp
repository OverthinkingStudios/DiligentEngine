#pragma once
// Public, Diligent-side API for the Earthworks terrain integration.
//
// This is the only layer that touches both glm (via Earthworks) and Diligent.
// The CPU/domain logic lives in the Earthworks library (no GPU types); this
// class owns the PSOs, GPU buffers/textures and implements the earthworks GPU
// sinks. See docs/porting/KICKOFF.md (section 4) for the layering contract.

#include <memory>

#include "RenderDevice.h"
#include "DeviceContext.h"
#include "BasicMath.hpp"

namespace earthworksfx
{

struct TerrainInitInfo
{
    Diligent::IRenderDevice*  pDevice  = nullptr;
    Diligent::IDeviceContext* pContext = nullptr; // immediate context (initial upload + bake)

    Diligent::TEXTURE_FORMAT ColorFormat = Diligent::TEX_FORMAT_UNKNOWN;
    Diligent::TEXTURE_FORMAT DepthFormat = Diligent::TEX_FORMAT_UNKNOWN;

    // Directory that contains the ported .hlsl/.hlsli files (resolved by the
    // shader source stream factory). Relative to the working directory.
    const char* ShadersDir = "shaders";

    // Optional terrain project root (elevations.txt). When null/absent a
    // procedural heightmap is synthesized so phase 1 runs without data.
    const char* DataDir = nullptr;

    // World-space edge length of the single phase-1 LOD-0 tile.
    float WorldSize = 2000.0f;
};

// Framing info for the loaded terrain, in render world space. The terrain is
// centered on the XZ origin and spans [-WorldSize/2, +WorldSize/2]; Center.y is
// the elevation at the terrain center. Valid after Initialize().
struct TerrainView
{
    Diligent::float3 Center{0, 0, 0}; // world center, Center.y = center elevation
    float            WorldSize = 0.f; // horizontal extent (meters)
    float            MinHeight = 0.f;
    float            MaxHeight = 0.f;
};

struct TerrainFrameAttribs
{
    Diligent::ITextureView* pColorRTV = nullptr;
    Diligent::ITextureView* pDepthDSV = nullptr;

    Diligent::float4x4 ViewProj;
    Diligent::float3   CameraPos;
    Diligent::float3   SunDir{-0.5f, -0.6f, -0.3f};
};

class TerrainSystem
{
public:
    TerrainSystem();
    ~TerrainSystem();

    TerrainSystem(const TerrainSystem&)            = delete;
    TerrainSystem& operator=(const TerrainSystem&) = delete;

    void Initialize(const TerrainInitInfo& Info);

    // Load an Earthworks project (settings + catalogs). Optional for phase 1.
    void LoadProject(const char* settingsPath);

    // CPU-side per-frame work (LOD pass / bake scheduling). Takes view and proj
    // separately so the quadtree can run its screen-space-error LOD heuristic.
    void Update(Diligent::IDeviceContext* pContext, const Diligent::float4x4& view, const Diligent::float4x4& proj, const Diligent::float3& camPos, float screenResolution = 1080.f);

    // GPU per-frame: clear -> build indirect args -> indirect draw.
    void Render(Diligent::IDeviceContext* pContext, const TerrainFrameAttribs& Attribs);

    // Terrain extent + center elevation for camera framing. Valid after Initialize().
    TerrainView GetView() const;

    // Opaque internal state (defined in TerrainSystemImpl.hpp). Public so the
    // EarthworksFX-internal GPU sinks can name it; instances stay private.
    struct Impl;

private:
    std::unique_ptr<Impl> m_Impl;
};

} // namespace earthworksfx
