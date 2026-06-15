#pragma once
// Internal shared state for the EarthworksFX terrain system. Split across:
//   TerrainInit.cpp       - resource + PSO creation, elevation upload
//   TileBakePipeline.cpp  - the per-tile compute bake chain
//   TerrainDrawPass.cpp   - per-frame clear + build-lookup + indirect draw
//   TerrainSystem.cpp     - public TerrainSystem facade + Update
//   GpuSinksImpl.cpp      - earthworks::ITileBakeSink / IElevationSink

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "RefCntAutoPtr.hpp"
#include "RenderDevice.h"
#include "DeviceContext.h"
#include "Buffer.h"
#include "Texture.h"
#include "PipelineState.h"
#include "ShaderResourceBinding.h"
#include "Shader.h"
#include "Sampler.h"
#include "BasicMath.hpp"

#include "earthworks/TerrainTypes.h"
#include "earthworks/IGpuSinks.hpp"
#include "earthworks/ElevationLoader.hpp"

#include "TerrainSystem.hpp"

namespace earthworksfx
{

using namespace Diligent;

// ---- CPU mirrors of the HLSL cbuffers (std cbuffer packing) ----------------

struct BicubicConstants
{
    float offset[2];
    float size[2];
    float hgt_offset;
    float hgt_scale;
    int   isHeight;
    float pad0;
};
static_assert(sizeof(BicubicConstants) == 32, "BicubicConstants cbuffer layout");

struct NormalsConstants
{
    float pixSize;
    float pad[3];
};

struct VerticesConstants
{
    float constants[4]; // .x pixSize*vertScale  .w tileIndex
};

struct JumpFloodConstants
{
    uint32_t step;
    uint32_t pad[3];
};

struct DelaunayConstants
{
    uint32_t tileIndex;
    uint32_t pad[3];
};

struct BuildLookupConstants
{
    // uint4 frustumflags[1024]
    uint32_t frustumFlags[1024 * 4];
};

struct DrawConstants
{
    float4x4 viewProj;
    float3   eye;
    float    pad0;
    float3   sunDir;
    float    pad1;
};

// ---- A compute pass: PSO + SRB + (optional) constant buffer ----------------

struct ComputePass
{
    RefCntAutoPtr<IPipelineState>         pPSO;
    RefCntAutoPtr<IShaderResourceBinding> pSRB;
};

class GpuSinks; // fwd

struct TerrainSystem::Impl
{
    // --- lifecycle (TerrainInit.cpp) ---
    void Initialize(const TerrainInitInfo& Info);
    void CreateGpuResources();
    void LoadShaders();
    void UploadRootElevation();

    RefCntAutoPtr<IShader>        LoadShader(const char* file, SHADER_TYPE type, const char* name);
    RefCntAutoPtr<IBuffer>        CreateConstantBuffer(size_t size, const char* name);
    RefCntAutoPtr<IBuffer>        CreateStructuredBuffer(uint32_t stride, uint32_t count, BIND_FLAGS bind, const char* name, const void* initData = nullptr);

    // --- bake (TileBakePipeline.cpp) ---
    void BakeTile(const earthworks::TileBakeRequest& req);

    // --- per frame (TerrainDrawPass.cpp / TerrainSystem.cpp) ---
    void Update(IDeviceContext* ctx, const float4x4& viewProj, const float3& camPos);
    void Render(IDeviceContext* ctx, const TerrainFrameAttribs& attribs);

    // --- device / config ---
    RefCntAutoPtr<IRenderDevice>                   device;
    RefCntAutoPtr<IDeviceContext>                  immediate;
    RefCntAutoPtr<IShaderSourceInputStreamFactory> shaderFactory;

    TEXTURE_FORMAT colorFormat = TEX_FORMAT_UNKNOWN;
    TEXTURE_FORMAT depthFormat = TEX_FORMAT_UNKNOWN;

    std::string shadersDir;
    std::string dataDir;
    float       worldSize = 2000.0f;

    static constexpr uint32_t kNumTiles      = 4;   // small phase-1 pool
    static constexpr uint32_t kRootElevSize  = 1024;
    static constexpr uint32_t kPhase1TileIdx = 1;   // >0 (build-lookup skips slot 0)

    // --- compute passes ---
    ComputePass bicubic;
    ComputePass normals;
    ComputePass vertices;
    ComputePass jumpFlood; // ping (A->B)
    ComputePass jumpFloodB;// pong (B->A) — second SRB, same PSO
    ComputePass delaunay;
    ComputePass clearPass;
    ComputePass buildLookup;

    // --- graphics draw pass ---
    RefCntAutoPtr<IPipelineState>         drawPSO;
    RefCntAutoPtr<IShaderResourceBinding> drawSRB;

    // --- constant buffers ---
    RefCntAutoPtr<IBuffer> cbBicubic;
    RefCntAutoPtr<IBuffer> cbNormals;
    RefCntAutoPtr<IBuffer> cbVertices;
    RefCntAutoPtr<IBuffer> cbJumpFlood;   // ping
    RefCntAutoPtr<IBuffer> cbJumpFloodB;  // pong
    RefCntAutoPtr<IBuffer> cbDelaunay;
    RefCntAutoPtr<IBuffer> cbBuildLookup;
    RefCntAutoPtr<IBuffer> cbDraw;

    // --- textures ---
    RefCntAutoPtr<ITexture> texRootElevation; // source DEM (R32F)
    RefCntAutoPtr<ITexture> texHeight;        // per-tile baked height (R32F)
    RefCntAutoPtr<ITexture> texNormals;       // per-tile normals (RGBA16F)
    RefCntAutoPtr<ITexture> texVertsA;        // half-res vertex map (R16U)
    RefCntAutoPtr<ITexture> texVertsB;
    RefCntAutoPtr<ITexture> arrNormals;       // Texture2DArray normals pool

    // --- buffers ---
    RefCntAutoPtr<IBuffer> bufTiles;          // gpuTile[kNumTiles]
    RefCntAutoPtr<IBuffer> bufVB;             // Terrain_vertex[kNumVertPerTile*kNumTiles]
    RefCntAutoPtr<IBuffer> bufTileCenters;    // centerFeedback[kNumTiles]
    RefCntAutoPtr<IBuffer> bufTerrainLookup;  // tileLookupStruct[]
    RefCntAutoPtr<IBuffer> bufDrawArgs;       // DrawArguments[1] (indirect)
    RefCntAutoPtr<IBuffer> bufLookupCounter;  // uint[1]

    // --- samplers ---
    RefCntAutoPtr<ISampler> samplerLinearClamp;

    // --- domain ---
    earthworks::ElevationLoader elevation;
    std::unique_ptr<GpuSinks>   sinks;

    bool bakedOnce = false;
};

// earthworks GPU sinks implemented by EarthworksFX.
class GpuSinks final : public earthworks::ITileBakeSink, public earthworks::IElevationSink
{
public:
    explicit GpuSinks(TerrainSystem::Impl* owner) : m_Owner(owner) {}

    void Bake(const earthworks::TileBakeRequest& req) override;
    void Upload(uint32_t hash, const earthworks::DecodedImage& img) override;

private:
    TerrainSystem::Impl* m_Owner = nullptr;
};

} // namespace earthworksfx
