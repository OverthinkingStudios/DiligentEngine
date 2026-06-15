// EarthworksFX terrain — public facade + CPU per-frame entry.
// Ported from docs/source_extract/src/TerrainFrame.cpp (update) and the
// TerrainManager lifecycle.

#include "TerrainSystemImpl.hpp"

namespace earthworksfx
{

TerrainSystem::TerrainSystem() :
    m_Impl(std::make_unique<Impl>())
{
}

TerrainSystem::~TerrainSystem() = default;

void TerrainSystem::Initialize(const TerrainInitInfo& Info)
{
    m_Impl->Initialize(Info);
}

void TerrainSystem::LoadProject(const char* settingsPath)
{
    // Phase 1 keeps the synthetic/root elevation. A real project load parses the
    // settings file and (re)loads the elevation + orthophoto catalogs here.
    if (settingsPath && *settingsPath)
        m_Impl->elevation.loadCatalog(settingsPath);
}

void TerrainSystem::Update(IDeviceContext* pContext, const float4x4& viewProj, const float3& camPos)
{
    m_Impl->Update(pContext, viewProj, camPos);
}

void TerrainSystem::Render(IDeviceContext* pContext, const TerrainFrameAttribs& Attribs)
{
    m_Impl->Render(pContext, Attribs);
}

// ---------------------------------------------------------------------------

void TerrainSystem::Impl::Update(IDeviceContext* /*ctx*/, const float4x4& /*viewProj*/, const float3& /*camPos*/)
{
    // Phase 1: bake the single static LOD-0 tile once, through the GPU sink so
    // the Earthworks <-> EarthworksFX seam is exercised. Later phases drive the
    // earthworks::TerrainQuadtree here and bake on split.
    if (!bakedOnce)
    {
        earthworks::TileBakeRequest req{};
        req.tileIndex    = kPhase1TileIdx;
        req.lod          = 0;
        req.x            = 0;
        req.y            = 0;
        req.elevationHash = 0;
        req.imageHash     = 0;
        req.neighborLodN = req.neighborLodE = req.neighborLodS = req.neighborLodW = -1;

        static_cast<earthworks::ITileBakeSink*>(sinks.get())->Bake(req);
        bakedOnce = true;
    }
}

} // namespace earthworksfx
