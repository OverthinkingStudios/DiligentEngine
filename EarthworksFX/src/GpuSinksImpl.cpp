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

void GpuSinks::Upload(uint32_t hash, const earthworks::DecodedImage& img)
{
    m_Owner->UploadElevation(hash, img);
}

} // namespace earthworksfx
