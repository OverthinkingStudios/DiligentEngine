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

    // CPU-side per-frame work (LOD pass / bake scheduling).
    void Update(Diligent::IDeviceContext* pContext, const Diligent::float4x4& viewProj, const Diligent::float3& camPos);

    // GPU per-frame: clear -> build indirect args -> indirect draw.
    void Render(Diligent::IDeviceContext* pContext, const TerrainFrameAttribs& Attribs);

    // Opaque internal state (defined in TerrainSystemImpl.hpp). Public so the
    // EarthworksFX-internal GPU sinks can name it; instances stay private.
    struct Impl;

private:
    std::unique_ptr<Impl> m_Impl;
};

} // namespace earthworksfx
