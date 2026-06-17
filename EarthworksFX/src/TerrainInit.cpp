// EarthworksFX terrain — GPU resource + pipeline creation and elevation upload.
// Ported from docs/source_extract/src/TerrainInit.cpp (Falcor -> Diligent).

#include "TerrainSystemImpl.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "MapHelper.hpp"
#include "EngineFactory.h"
#include "GraphicsTypes.h"

namespace earthworksfx
{

// CPU/GPU layout guardrails (KICKOFF rule 5 + guardrails checklist).
static_assert(sizeof(earthworks::GpuTile) == 48, "gpuTile must match HLSL");
static_assert(sizeof(earthworks::TerrainVertex) == 8, "Terrain_vertex must match HLSL");
static_assert(sizeof(earthworks::DrawArguments) == 16, "DrawArguments must match HLSL");

namespace
{
constexpr uint32_t kTileNumPixels = earthworks::kTileNumPixels;   // 256
constexpr uint32_t kTileInner     = earthworks::kTileInnerPixels; // 248
constexpr uint32_t kVertPerTile   = earthworks::kNumVertPerTile;  // 32768
} // namespace

// ---------------------------------------------------------------------------

void TerrainSystem::Impl::Initialize(const TerrainInitInfo& Info)
{
    device     = Info.pDevice;
    immediate  = Info.pContext;
    colorFormat = Info.ColorFormat;
    depthFormat = Info.DepthFormat;
    shadersDir  = Info.ShadersDir ? Info.ShadersDir : "shaders";
    dataDir     = Info.DataDir ? Info.DataDir : "";
    worldSize   = Info.WorldSize > 0.f ? Info.WorldSize : 2000.f;

    device->GetEngineFactory()->CreateDefaultShaderSourceStreamFactory(shadersDir.c_str(), &shaderFactory);
    VERIFY_EXPR(shaderFactory);

    sinks = std::make_unique<GpuSinks>(this);

    CreateGpuResources();
    LoadShaders();
    UploadRootElevation(); // may adopt the real LOD-0 world extent into worldSize

    // Seed the quadtree with the LOD-0 root tile (terrain centered on XZ origin)
    // and bake it so something draws before the first split.
    rootOriginX = -worldSize * 0.5f;
    rootOriginZ = -worldSize * 0.5f;
    quadtree.initialize(worldSize, glm::vec3{rootOriginX, 0.f, rootOriginZ});
    quadtree.setMaxLod(8); // bounds refinement against the GPU tile pool

    if (earthworks::QuadtreeTile* root = quadtree.root())
    {
        earthworks::TileBakeRequest req{};
        req.tileIndex     = root->index;
        req.lod           = root->lod;
        req.x             = root->x;
        req.y             = root->y;
        req.elevationHash = root->elevationHash;
        req.imageHash     = root->imageHash;
        req.neighborLodN = req.neighborLodE = req.neighborLodS = req.neighborLodW = -1;
        static_cast<earthworks::ITileBakeSink*>(sinks.get())->Bake(req);
    }
}

// ---------------------------------------------------------------------------

RefCntAutoPtr<IBuffer> TerrainSystem::Impl::CreateConstantBuffer(size_t size, const char* name)
{
    BufferDesc desc;
    desc.Name           = name;
    desc.Usage          = USAGE_DYNAMIC;
    desc.BindFlags      = BIND_UNIFORM_BUFFER;
    desc.CPUAccessFlags = CPU_ACCESS_WRITE;
    desc.Size           = size;
    RefCntAutoPtr<IBuffer> buf;
    device->CreateBuffer(desc, nullptr, &buf);
    return buf;
}

RefCntAutoPtr<IBuffer> TerrainSystem::Impl::CreateStructuredBuffer(uint32_t stride, uint32_t count, BIND_FLAGS bind, const char* name, const void* initData)
{
    BufferDesc desc;
    desc.Name              = name;
    desc.Usage             = USAGE_DEFAULT;
    desc.BindFlags         = bind;
    desc.Mode              = BUFFER_MODE_STRUCTURED;
    desc.ElementByteStride = stride;
    desc.Size              = Uint64{stride} * count;

    BufferData data;
    data.pData    = initData;
    data.DataSize = initData ? desc.Size : 0;

    RefCntAutoPtr<IBuffer> buf;
    device->CreateBuffer(desc, initData ? &data : nullptr, &buf);
    return buf;
}

void TerrainSystem::Impl::CreateGpuResources()
{
    const uint32_t w  = kTileNumPixels;     // 256
    const uint32_t hw = kTileNumPixels / 2; // 128

    auto makeTex2D = [&](const char* name, TEXTURE_FORMAT fmt, uint32_t width, uint32_t height,
                         BIND_FLAGS bind, uint32_t arraySize, const void* zeroData) -> RefCntAutoPtr<ITexture> {
        TextureDesc td;
        td.Name      = name;
        td.Type      = arraySize > 1 ? RESOURCE_DIM_TEX_2D_ARRAY : RESOURCE_DIM_TEX_2D;
        td.Width     = width;
        td.Height    = height;
        td.MipLevels = 1;
        td.ArraySize = arraySize;
        td.Format    = fmt;
        td.Usage     = USAGE_DEFAULT;
        td.BindFlags = bind;

        RefCntAutoPtr<ITexture> tex;
        if (zeroData)
        {
            TextureSubResData sub;
            sub.pData  = zeroData;
            sub.Stride = width * (fmt == TEX_FORMAT_R16_UINT ? 2u : 4u);
            TextureData td0;
            td0.pSubResources   = &sub;
            td0.NumSubresources = 1;
            device->CreateTexture(td, &td0, &tex);
        }
        else
        {
            device->CreateTexture(td, nullptr, &tex);
        }
        return tex;
    };

    texHeight  = makeTex2D("Terrain.tileHeight", TEX_FORMAT_R32_FLOAT, w, w,
                           BIND_UNORDERED_ACCESS | BIND_SHADER_RESOURCE, 1, nullptr);
    texNormals = makeTex2D("Terrain.tileNormals", TEX_FORMAT_RGBA16_FLOAT, w, w,
                           BIND_UNORDERED_ACCESS | BIND_SHADER_RESOURCE, 1, nullptr);

    // Vertex maps are zero-initialized; compute_tileVertices also clears texVertsA
    // at the start of every bake because the ping-pong targets are shared across
    // the whole tile pool.
    std::vector<uint16_t> zeros(static_cast<size_t>(hw) * hw, 0);
    texVertsA = makeTex2D("Terrain.vertsA", TEX_FORMAT_R16_UINT, hw, hw,
                          BIND_UNORDERED_ACCESS | BIND_SHADER_RESOURCE, 1, zeros.data());
    texVertsB = makeTex2D("Terrain.vertsB", TEX_FORMAT_R16_UINT, hw, hw,
                          BIND_UNORDERED_ACCESS | BIND_SHADER_RESOURCE, 1, zeros.data());

    arrNormals = makeTex2D("Terrain.normalsArray", TEX_FORMAT_RGBA16_FLOAT, w, w,
                           BIND_SHADER_RESOURCE, kNumTiles, nullptr);

    // Structured buffers.
    bufTiles         = CreateStructuredBuffer(sizeof(earthworks::GpuTile), kNumTiles,
                                              BIND_UNORDERED_ACCESS | BIND_SHADER_RESOURCE, "Terrain.tiles");
    bufVB            = CreateStructuredBuffer(sizeof(earthworks::TerrainVertex), kVertPerTile * kNumTiles,
                                              BIND_UNORDERED_ACCESS | BIND_SHADER_RESOURCE, "Terrain.VB");
    bufTileCenters   = CreateStructuredBuffer(sizeof(float) * 4, kNumTiles,
                                              BIND_UNORDERED_ACCESS, "Terrain.tileCenters");
    bufTerrainLookup = CreateStructuredBuffer(sizeof(uint32_t) * 2, 65536,
                                              BIND_UNORDERED_ACCESS | BIND_SHADER_RESOURCE, "Terrain.terrainLookup");
    bufDrawArgs      = CreateStructuredBuffer(sizeof(earthworks::DrawArguments), 1,
                                              BIND_UNORDERED_ACCESS | BIND_INDIRECT_DRAW_ARGS, "Terrain.drawArgs");
    bufLookupCounter = CreateStructuredBuffer(sizeof(uint32_t), 1,
                                              BIND_UNORDERED_ACCESS, "Terrain.lookupCounter");

    // Constant buffers.
    cbBicubic     = CreateConstantBuffer(sizeof(BicubicConstants), "cb.bicubic");
    cbNormals     = CreateConstantBuffer(sizeof(NormalsConstants), "cb.normals");
    cbVertices    = CreateConstantBuffer(sizeof(VerticesConstants), "cb.vertices");
    cbJumpFlood   = CreateConstantBuffer(sizeof(JumpFloodConstants), "cb.jumpflood.ping");
    cbJumpFloodB  = CreateConstantBuffer(sizeof(JumpFloodConstants), "cb.jumpflood.pong");
    cbDelaunay    = CreateConstantBuffer(sizeof(DelaunayConstants), "cb.delaunay");
    cbBuildLookup = CreateConstantBuffer(sizeof(BuildLookupConstants), "cb.buildLookup");
    cbDraw        = CreateConstantBuffer(sizeof(DrawConstants), "cb.draw");

    // Samplers.
    {
        SamplerDesc sd;
        sd.MinFilter = FILTER_TYPE_LINEAR;
        sd.MagFilter = FILTER_TYPE_LINEAR;
        sd.MipFilter = FILTER_TYPE_POINT;
        sd.AddressU  = TEXTURE_ADDRESS_CLAMP;
        sd.AddressV  = TEXTURE_ADDRESS_CLAMP;
        sd.AddressW  = TEXTURE_ADDRESS_CLAMP;
        device->CreateSampler(sd, &samplerLinearClamp);
    }
}

// ---------------------------------------------------------------------------

RefCntAutoPtr<IShader> TerrainSystem::Impl::LoadShader(const char* file, SHADER_TYPE type, const char* name)
{
    ShaderCreateInfo ci;
    ci.SourceLanguage                  = SHADER_SOURCE_LANGUAGE_HLSL;
    ci.Desc.UseCombinedTextureSamplers = false; // we use separate (immutable) samplers
    ci.CompileFlags                    = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;
    ci.pShaderSourceStreamFactory      = shaderFactory;
    ci.Desc.ShaderType                 = type;
    ci.Desc.Name                       = name;
    ci.EntryPoint                      = (type == SHADER_TYPE_VERTEX) ? "vsMain" : (type == SHADER_TYPE_PIXEL ? "psMain" : "main");
    ci.FilePath                        = file;

    RefCntAutoPtr<IShader> shader;
    device->CreateShader(ci, &shader);
    return shader;
}

void TerrainSystem::Impl::LoadShaders()
{
    auto makeCompute = [&](const char* file, const char* name, bool withLinearSampler) -> RefCntAutoPtr<IPipelineState> {
        ComputePipelineStateCreateInfo info;
        info.PSODesc.Name                              = name;
        info.PSODesc.PipelineType                      = PIPELINE_TYPE_COMPUTE;
        info.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;

        ImmutableSamplerDesc immSam{SHADER_TYPE_COMPUTE, "linearSampler", samplerLinearClamp->GetDesc()};
        if (withLinearSampler)
        {
            info.PSODesc.ResourceLayout.ImmutableSamplers    = &immSam;
            info.PSODesc.ResourceLayout.NumImmutableSamplers = 1;
        }

        RefCntAutoPtr<IShader> cs = LoadShader(file, SHADER_TYPE_COMPUTE, name);
        info.pCS                  = cs;

        RefCntAutoPtr<IPipelineState> pso;
        device->CreateComputePipelineState(info, &pso);
        return pso;
    };

    bicubic.pPSO     = makeCompute("compute_tileBicubic.hlsl", "CS.bicubic", true);
    normals.pPSO     = makeCompute("compute_tileNormals.hlsl", "CS.normals", false);
    vertices.pPSO    = makeCompute("compute_tileVertices.hlsl", "CS.vertices", true);
    jumpFlood.pPSO   = makeCompute("compute_tileJumpFlood.hlsl", "CS.jumpFlood", false);
    jumpFloodB.pPSO  = jumpFlood.pPSO; // same PSO, distinct SRB
    delaunay.pPSO    = makeCompute("compute_tileDelaunay.hlsl", "CS.delaunay", false);
    clearPass.pPSO   = makeCompute("compute_tileClear.hlsl", "CS.clear", false);
    buildLookup.pPSO = makeCompute("compute_tileBuildLookup.hlsl", "CS.buildLookup", false);

    auto srbVar = [](IShaderResourceBinding* srb, const char* n) {
        return srb->GetVariableByName(SHADER_TYPE_COMPUTE, n);
    };
    auto uav = [](IBuffer* b) { return b->GetDefaultView(BUFFER_VIEW_UNORDERED_ACCESS); };
    auto srv = [](IBuffer* b) { return b->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE); };
    auto tuav = [](ITexture* t) { return t->GetDefaultView(TEXTURE_VIEW_UNORDERED_ACCESS); };
    auto tsrv = [](ITexture* t) { return t->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE); };

    // --- bicubic ---
    bicubic.pPSO->CreateShaderResourceBinding(&bicubic.pSRB, true);
    srbVar(bicubic.pSRB, "gConstants")->Set(cbBicubic);
    srbVar(bicubic.pSRB, "gOutput")->Set(tuav(texHeight));
    // gInput bound later in UploadRootElevation.

    // --- normals ---
    normals.pPSO->CreateShaderResourceBinding(&normals.pSRB, true);
    srbVar(normals.pSRB, "gConstants")->Set(cbNormals);
    srbVar(normals.pSRB, "gInHgt")->Set(tsrv(texHeight));
    srbVar(normals.pSRB, "gOutNormals")->Set(tuav(texNormals));

    // --- vertices ---
    vertices.pPSO->CreateShaderResourceBinding(&vertices.pSRB, true);
    srbVar(vertices.pSRB, "gConstants")->Set(cbVertices);
    srbVar(vertices.pSRB, "gInHgt")->Set(tsrv(texHeight));
    srbVar(vertices.pSRB, "gOutVerts")->Set(tuav(texVertsA));
    srbVar(vertices.pSRB, "tileCenters")->Set(uav(bufTileCenters));
    srbVar(vertices.pSRB, "tiles")->Set(uav(bufTiles));

    // --- jump flood ping (A -> B) ---
    jumpFlood.pPSO->CreateShaderResourceBinding(&jumpFlood.pSRB, true);
    srbVar(jumpFlood.pSRB, "gConstants")->Set(cbJumpFlood);
    srbVar(jumpFlood.pSRB, "gInVerts")->Set(tsrv(texVertsA));
    srbVar(jumpFlood.pSRB, "gOutVerts")->Set(tuav(texVertsB));

    // --- jump flood pong (B -> A) ---
    jumpFloodB.pPSO->CreateShaderResourceBinding(&jumpFloodB.pSRB, true);
    srbVar(jumpFloodB.pSRB, "gConstants")->Set(cbJumpFloodB);
    srbVar(jumpFloodB.pSRB, "gInVerts")->Set(tsrv(texVertsB));
    srbVar(jumpFloodB.pSRB, "gOutVerts")->Set(tuav(texVertsA));

    // --- delaunay (reads final flood output: texVertsB) ---
    delaunay.pPSO->CreateShaderResourceBinding(&delaunay.pSRB, true);
    srbVar(delaunay.pSRB, "gConstants")->Set(cbDelaunay);
    srbVar(delaunay.pSRB, "gInHgt")->Set(tsrv(texHeight));
    srbVar(delaunay.pSRB, "gInVerts")->Set(tsrv(texVertsB));
    srbVar(delaunay.pSRB, "VB")->Set(uav(bufVB));
    srbVar(delaunay.pSRB, "tiles")->Set(uav(bufTiles));

    // --- clear ---
    clearPass.pPSO->CreateShaderResourceBinding(&clearPass.pSRB, true);
    srbVar(clearPass.pSRB, "DrawArgs_Terrain")->Set(uav(bufDrawArgs));
    srbVar(clearPass.pSRB, "lookupCounter")->Set(uav(bufLookupCounter));

    // --- build lookup ---
    buildLookup.pPSO->CreateShaderResourceBinding(&buildLookup.pSRB, true);
    srbVar(buildLookup.pSRB, "gConstants")->Set(cbBuildLookup);
    srbVar(buildLookup.pSRB, "terrainLookup")->Set(uav(bufTerrainLookup));
    srbVar(buildLookup.pSRB, "tiles")->Set(uav(bufTiles));
    srbVar(buildLookup.pSRB, "DrawArgs_Terrain")->Set(uav(bufDrawArgs));
    srbVar(buildLookup.pSRB, "lookupCounter")->Set(uav(bufLookupCounter));

    // --- graphics draw PSO ---
    {
        GraphicsPipelineStateCreateInfo info;
        info.PSODesc.Name                                  = "Terrain.draw";
        info.PSODesc.PipelineType                          = PIPELINE_TYPE_GRAPHICS;
        info.GraphicsPipeline.NumRenderTargets             = 1;
        info.GraphicsPipeline.RTVFormats[0]                = colorFormat;
        info.GraphicsPipeline.DSVFormat                    = depthFormat;
        info.GraphicsPipeline.PrimitiveTopology            = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        info.GraphicsPipeline.RasterizerDesc.CullMode      = CULL_MODE_NONE;
        info.GraphicsPipeline.DepthStencilDesc.DepthEnable = True;
        info.GraphicsPipeline.DepthStencilDesc.DepthFunc   = COMPARISON_FUNC_LESS;

        info.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;

        SamplerDesc aniso;
        aniso.MinFilter      = FILTER_TYPE_ANISOTROPIC;
        aniso.MagFilter      = FILTER_TYPE_ANISOTROPIC;
        aniso.MipFilter      = FILTER_TYPE_ANISOTROPIC;
        aniso.MaxAnisotropy  = 8;
        aniso.AddressU       = TEXTURE_ADDRESS_CLAMP;
        aniso.AddressV       = TEXTURE_ADDRESS_CLAMP;
        aniso.AddressW       = TEXTURE_ADDRESS_CLAMP;
        ImmutableSamplerDesc immSam{SHADER_TYPE_PIXEL, "gSmpAniso", aniso};
        info.PSODesc.ResourceLayout.ImmutableSamplers    = &immSam;
        info.PSODesc.ResourceLayout.NumImmutableSamplers = 1;

        RefCntAutoPtr<IShader> vs = LoadShader("render_Tiles.hlsl", SHADER_TYPE_VERTEX, "VS.terrain");
        RefCntAutoPtr<IShader> ps = LoadShader("render_Tiles.hlsl", SHADER_TYPE_PIXEL, "PS.terrain");
        info.pVS                  = vs;
        info.pPS                  = ps;

        device->CreateGraphicsPipelineState(info, &drawPSO);

        drawPSO->CreateShaderResourceBinding(&drawSRB, true);
        drawSRB->GetVariableByName(SHADER_TYPE_VERTEX, "VB")->Set(srv(bufVB));
        drawSRB->GetVariableByName(SHADER_TYPE_VERTEX, "tiles")->Set(srv(bufTiles));
        drawSRB->GetVariableByName(SHADER_TYPE_VERTEX, "tileLookup")->Set(srv(bufTerrainLookup));
        if (auto* v = drawSRB->GetVariableByName(SHADER_TYPE_VERTEX, "gConstantBuffer"))
            v->Set(cbDraw);
        if (auto* v = drawSRB->GetVariableByName(SHADER_TYPE_PIXEL, "gConstantBuffer"))
            v->Set(cbDraw);
        drawSRB->GetVariableByName(SHADER_TYPE_PIXEL, "gNormArray")->Set(tsrv(arrNormals));
    }
}

// ---------------------------------------------------------------------------

void TerrainSystem::Impl::UploadRootElevation()
{
    const uint32_t       N = kRootElevSize;
    std::vector<float>   heights;
    uint32_t             srcSize = 0;
    bool                 loaded  = false;

    if (!dataDir.empty() && elevation.loadCatalog(dataDir))
    {
        loaded = elevation.loadRootElevation(heights, srcSize);
        // Render the root tile at its true world extent (manifest LOD-0 size),
        // not the placeholder default, so the camera framing matches reality.
        if (loaded && elevation.rootWorldSize() > 0.f)
            worldSize = elevation.rootWorldSize();
    }

    if (!loaded)
    {
        // Procedural multi-octave heightmap so phase 1 has visible relief and
        // enough curvature to seed the adaptive mesh.
        srcSize = N;
        heights.resize(static_cast<size_t>(N) * N);
        const float twoPi = 6.2831853f;
        for (uint32_t y = 0; y < N; ++y)
        {
            for (uint32_t x = 0; x < N; ++x)
            {
                float u = float(x) / float(N);
                float v = float(y) / float(N);
                float h = 0.f;
                h += 180.f * std::sin(u * twoPi * 1.0f) * std::cos(v * twoPi * 1.0f);
                h += 70.f * std::sin(u * twoPi * 3.0f + 1.3f) * std::cos(v * twoPi * 2.0f);
                h += 30.f * std::sin(u * twoPi * 6.0f) * std::cos(v * twoPi * 7.0f + 0.7f);
                h += 12.f * std::sin(u * twoPi * 13.0f + 2.1f) * std::cos(v * twoPi * 11.0f);
                heights[static_cast<size_t>(y) * N + x] = h + 200.f;
            }
        }
    }

    // Framing stats from the (real or procedural) root grid. The center texel
    // maps to the terrain center (world XZ origin); see render_Tiles placement.
    if (srcSize > 0 && !heights.empty())
    {
        const size_t centerIdx = (static_cast<size_t>(srcSize) / 2) * srcSize + (srcSize / 2);
        centerHeight           = heights[centerIdx];
        minHeight = maxHeight = heights[0];
        for (float h : heights)
        {
            minHeight = std::min(minHeight, h);
            maxHeight = std::max(maxHeight, h);
        }
    }

    TextureDesc td;
    td.Name      = "Terrain.rootElevation";
    td.Type      = RESOURCE_DIM_TEX_2D;
    td.Width     = srcSize;
    td.Height    = srcSize;
    td.MipLevels = 1;
    td.Format    = TEX_FORMAT_R32_FLOAT;
    td.Usage     = USAGE_DEFAULT;
    td.BindFlags = BIND_SHADER_RESOURCE;

    TextureSubResData sub;
    sub.pData  = heights.data();
    sub.Stride = srcSize * sizeof(float);
    TextureData init;
    init.pSubResources   = &sub;
    init.NumSubresources = 1;

    device->CreateTexture(td, &init, &texRootElevation);

    bicubic.pSRB->GetVariableByName(SHADER_TYPE_COMPUTE, "gInput")
        ->Set(texRootElevation->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
}

} // namespace earthworksfx
