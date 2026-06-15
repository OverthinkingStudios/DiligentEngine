// EarthworksFX implementation of the earthworks GPU sinks (the seam declared in
// earthworks/IGpuSinks.hpp). The Earthworks core calls these; EarthworksFX owns
// the GPU side.

#include "TerrainSystemImpl.hpp"

namespace earthworksfx
{

void GpuSinks::Bake(const earthworks::TileBakeRequest& req)
{
    m_Owner->BakeTile(req);
}

void GpuSinks::Upload(uint32_t /*hash*/, const earthworks::DecodedImage& /*img*/)
{
    // Phase 1 synthesizes / loads the root elevation directly in
    // TerrainInit.cpp::UploadRootElevation. Streaming decoded JP2 tiles into a
    // GPU elevation cache is a later phase.
}

} // namespace earthworksfx
