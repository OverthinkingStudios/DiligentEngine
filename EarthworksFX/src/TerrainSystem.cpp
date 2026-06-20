// EarthworksFX terrain — public facade + CPU per-frame entry.
// Ported from docs/source_extract/src/TerrainFrame.cpp (update) and the
// TerrainManager lifecycle.

#include "TerrainSystemImpl.hpp"

#include <glm/glm.hpp>

namespace earthworksfx
{

namespace
{
// Diligent stores matrices row-major with a row-vector convention (v * M); glm
// is column-major with M * v. Transposing on copy makes the two equivalent, so
// the quadtree's glm math reproduces the same view/clip-space transforms.
glm::mat4 ToGlm(const Diligent::float4x4& m)
{
    glm::mat4 g;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            g[c][r] = m.m[r][c];
    return g;
}
} // namespace

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

void TerrainSystem::Update(IDeviceContext* pContext, const float4x4& view, const float4x4& proj, const float3& camPos, float screenResolution)
{
    m_Impl->Update(pContext, view, proj, camPos, screenResolution);
}

void TerrainSystem::Render(IDeviceContext* pContext, const TerrainFrameAttribs& Attribs)
{
    m_Impl->Render(pContext, Attribs);
}

TerrainView TerrainSystem::GetView() const
{
    // Terrain is centered on the XZ origin (see TileBakePipeline / render_Tiles).
    TerrainView v;
    v.Center    = float3{0.f, m_Impl->centerHeight, 0.f};
    v.WorldSize = m_Impl->worldSize;
    v.MinHeight = m_Impl->minHeight;
    v.MaxHeight = m_Impl->maxHeight;
    return v;
}

// ---------------------------------------------------------------------------

void TerrainSystem::Impl::Update(IDeviceContext* /*ctx*/, const float4x4& view, const float4x4& proj, const float3& camPos, float screenResolution)
{
    // Feed the camera to the quadtree and run one LOD pass. New tiles created by
    // a split are baked immediately through the GPU sink (one split per pass,
    // matching the original cadence); merged tiles free their pool slots.
    quadtree.clearCameras();
    quadtree.setCamera(earthworks::CameraSlot_MainCenter,
                       ToGlm(view), ToGlm(proj),
                       glm::vec3{camPos.x, camPos.y, camPos.z},
                       true, screenResolution);

    quadtree.update(sinks.get());
}

} // namespace earthworksfx
