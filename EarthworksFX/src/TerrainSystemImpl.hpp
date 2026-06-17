#pragma once
// Internal shared state for the EarthworksFX terrain system. Split across:
//   TerrainInit.cpp       - resource + PSO creation, elevation upload
//   TileBakePipeline.cpp  - the per-tile compute bake chain
//   TerrainDrawPass.cpp   - per-frame clear + build-lookup + indirect draw
//   TerrainSystem.cpp     - public TerrainSystem facade + Update
//   GpuSinksImpl.cpp      - earthworks::ITileBakeSink / IElevationSink

#include <array>
#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
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
#include "earthworks/TerrainQuadtree.hpp"

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
    int   useRoot;     // 1 = gInputRoot, 0 = gInputTiles[inputSlice]
    int   inputSlice;
    int   pad0;
};
static_assert(sizeof(BicubicConstants) == 40, "BicubicConstants cbuffer layout");

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

// LRU slice allocator for arrElevationCache (hash -> array slice index).
struct ElevationSlicePool
{
    struct Entry
    {
        uint32_t hash;
        uint32_t slice;
    };

    std::list<Entry>                                          order;
    std::unordered_map<uint32_t, std::list<Entry>::iterator> map;
    uint32_t                                                  nextSlice = 0;

    uint32_t acquire(uint32_t hash);
    bool     find(uint32_t hash, uint32_t& slice) const;
};

struct TerrainSystem::Impl
{
    // --- lifecycle (TerrainInit.cpp) ---
    void Initialize(const TerrainInitInfo& Info);
    void CreateGpuResources();
    void LoadShaders();
    void UploadRootElevation();
    void CreateElevationCache();
    void UploadElevation(uint32_t hash, const earthworks::DecodedImage& img);
    bool BindElevationForBake(const earthworks::TileBakeRequest& req, BicubicConstants& bicubicOut, float pixelSize);

    RefCntAutoPtr<IShader>        LoadShader(const char* file, SHADER_TYPE type, const char* name);
    RefCntAutoPtr<IBuffer>        CreateConstantBuffer(size_t size, const char* name);
    RefCntAutoPtr<IBuffer>        CreateStructuredBuffer(uint32_t stride, uint32_t count, BIND_FLAGS bind, const char* name, const void* initData = nullptr);

    // --- bake (TileBakePipeline.cpp) ---
    void BakeTile(const earthworks::TileBakeRequest& req);

    // --- per frame (TerrainDrawPass.cpp / TerrainSystem.cpp) ---
    void Update(IDeviceContext* ctx, const float4x4& view, const float4x4& proj, const float3& camPos);
    void Render(IDeviceContext* ctx, const TerrainFrameAttribs& attribs);

    // GPU tile-pool slot for a quadtree node. Slot 0 is reserved (build-lookup
    // skips it), so the CPU quadtree index maps to GPU slot index+1.
    static uint32_t GpuSlot(uint32_t quadtreeIndex) { return quadtreeIndex + 1; }

    // --- device / config ---
    RefCntAutoPtr<IRenderDevice>                   device;
    RefCntAutoPtr<IDeviceContext>                  immediate;
    RefCntAutoPtr<IShaderSourceInputStreamFactory> shaderFactory;

    TEXTURE_FORMAT colorFormat = TEX_FORMAT_UNKNOWN;
    TEXTURE_FORMAT depthFormat = TEX_FORMAT_UNKNOWN;

    std::string shadersDir;
    std::string dataDir;
    float       worldSize = 2000.0f;

    // Terrain framing, filled by UploadRootElevation (see TerrainSystem::GetView).
    float centerHeight = 0.f;
    float minHeight    = 0.f;
    float maxHeight    = 0.f;

    // GPU tile pool. Slot 0 is reserved (build-lookup skips it); the CPU quadtree
    // owns kPoolSize nodes mapped to GPU slots 1..kPoolSize. kNumTiles must stay
    // < 1000 (build-lookup guard) and bounds GPU memory (per slot: ~512 KB normals
    // array slice + 256 KB vertex buffer).
    static constexpr uint32_t kNumTiles      = 256;
    static constexpr uint32_t kPoolSize      = kNumTiles - 1;
    static constexpr uint32_t kRootElevSize  = 1024;
    static constexpr uint32_t kElevationCacheSlices = 64;

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
    RefCntAutoPtr<ITexture> texRootElevation; // LOD-0 root DEM (R32F)
    RefCntAutoPtr<ITexture> arrElevationCache; // JP2 LRU pool (R16_UNORM 1024² slices)
    RefCntAutoPtr<ITexture> texElevationStaging; // reusable upload surface (USAGE_STAGING)
    ElevationSlicePool                        elevationPool;
    std::unordered_map<uint32_t, std::vector<uint16_t>> elevationCpuCache;
    std::vector<uint16_t>                               elevationDecodeScratch;
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
    earthworks::TerrainQuadtree quadtree{kPoolSize};
    std::unique_ptr<GpuSinks>   sinks;

    // Root tile world placement (terrain centered on the XZ origin).
    float rootOriginX = 0.f;
    float rootOriginZ = 0.f;
    // Approximate viewport height used by the LOD screen-space-error metric.
    float screenResolution = 1080.f;
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
