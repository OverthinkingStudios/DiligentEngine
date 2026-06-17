// GPU LRU cache for decoded JP2 elevation tiles (R16_UNORM 1024² slices).
// Port of terrain.cpp hashAndCache() — CPU decode stays in Earthworks::ElevationLoader.

#include "TerrainSystemImpl.hpp"

#include <cstring>

namespace earthworksfx
{

namespace
{
constexpr uint32_t kElevTexels  = 1024;
constexpr uint32_t kCacheSlices = 64;
} // namespace

uint32_t ElevationSlicePool::acquire(uint32_t hash)
{
    auto pos = map.find(hash);
    if (pos != map.end())
    {
        order.splice(order.begin(), order, pos->second);
        return pos->second->slice;
    }

    uint32_t slice = 0;
    if (nextSlice < kCacheSlices)
    {
        slice = nextSlice++;
    }
    else
    {
        Entry& tail = order.back();
        slice       = tail.slice;
        map.erase(tail.hash);
        order.pop_back();
    }

    order.push_front({hash, slice});
    map[hash] = order.begin();
    return slice;
}

bool ElevationSlicePool::find(uint32_t hash, uint32_t& slice) const
{
    auto pos = map.find(hash);
    if (pos == map.end())
        return false;
    slice = pos->second->slice;
    return true;
}

void TerrainSystem::Impl::CreateElevationCache()
{
    TextureDesc td;
    td.Name      = "Terrain.elevationCache";
    td.Type      = RESOURCE_DIM_TEX_2D_ARRAY;
    td.Width     = kElevTexels;
    td.Height    = kElevTexels;
    td.ArraySize = kCacheSlices;
    td.MipLevels = 1;
    td.Format    = TEX_FORMAT_R16_UNORM;
    td.Usage     = USAGE_DEFAULT;
    td.BindFlags = BIND_SHADER_RESOURCE;

    device->CreateTexture(td, nullptr, &arrElevationCache);

    TextureDesc st;
    st.Name           = "Terrain.elevationStaging";
    st.Type           = RESOURCE_DIM_TEX_2D;
    st.Width          = kElevTexels;
    st.Height         = kElevTexels;
    st.MipLevels      = 1;
    st.Format         = TEX_FORMAT_R16_UNORM;
    st.Usage          = USAGE_STAGING;
    st.BindFlags      = BIND_NONE;
    st.CPUAccessFlags = CPU_ACCESS_WRITE;
    device->CreateTexture(st, nullptr, &texElevationStaging);

    elevationPool = ElevationSlicePool{};
}

void TerrainSystem::Impl::UploadElevation(uint32_t hash, const earthworks::DecodedImage& img)
{
    if (!arrElevationCache || !texElevationStaging || !img.data ||
        img.width != kElevTexels || img.height != kElevTexels)
        return;
    if (img.bytesPerChannel != 2 || img.channels != 1)
        return;

    const uint32_t slice = elevationPool.acquire(hash);

    const Box region{0, kElevTexels, 0, kElevTexels};
    const auto  rowBytes = Uint64{kElevTexels} * sizeof(uint16_t);

    MappedTextureSubresource mapped;
    immediate->MapTextureSubresource(texElevationStaging, 0, 0, MAP_WRITE, MAP_FLAG_DISCARD,
                                     &region, mapped);

    const auto* src = static_cast<const uint8_t*>(img.data);
    auto*       dst = static_cast<uint8_t*>(mapped.pData);
    if (mapped.Stride == rowBytes)
    {
        std::memcpy(dst, src, static_cast<size_t>(rowBytes * kElevTexels));
    }
    else
    {
        for (uint32_t y = 0; y < kElevTexels; ++y)
            std::memcpy(dst + y * mapped.Stride, src + y * rowBytes, static_cast<size_t>(rowBytes));
    }

    immediate->UnmapTextureSubresource(texElevationStaging, 0, 0);

    CopyTextureAttribs copy{};
    copy.pSrcTexture = texElevationStaging;
    copy.SrcMipLevel = 0;
    copy.SrcSlice    = 0;
    copy.pDstTexture = arrElevationCache;
    copy.DstMipLevel = 0;
    copy.DstSlice    = slice;
    immediate->CopyTexture(copy);
}

bool TerrainSystem::Impl::BindElevationForBake(const earthworks::TileBakeRequest& req,
                                               BicubicConstants&                bicubicOut,
                                               float                            pixelSize)
{
    // LOD-0 / hash 0: sample the root R32F grid (whole-world extent).
    const bool useRoot = (req.lod == 0) || (req.elevationHash == 0);

    if (useRoot)
    {
        const float lodScale = 1.0f / float(1u << req.lod);
        const float tileSize = worldSize * lodScale;
        const float originX  = rootOriginX + float(req.x) * tileSize;
        const float originZ  = rootOriginZ + float(req.y) * tileSize;

        bicubicOut.offset[0]    = (originX - rootOriginX) / worldSize;
        bicubicOut.offset[1]    = (originZ - rootOriginZ) / worldSize;
        bicubicOut.size[0]      = pixelSize / worldSize;
        bicubicOut.size[1]      = pixelSize / worldSize;
        bicubicOut.hgt_offset   = 0.f;
        bicubicOut.hgt_scale    = 1.f;
        bicubicOut.useRoot      = 1;
        bicubicOut.inputSlice   = 0;
        return true;
    }

    const earthworks::HeightMapTile* entry = elevation.find(req.elevationHash);
    if (!entry)
        return false;

#if defined(EARTHWORKS_WITH_OPENJPH)
    uint32_t slice = 0;
    if (!elevationPool.find(req.elevationHash, slice))
    {
        const uint16_t* decodePtr = nullptr;
        auto            cpuIt     = elevationCpuCache.find(req.elevationHash);
        if (cpuIt != elevationCpuCache.end())
        {
            decodePtr = cpuIt->second.data();
        }
        else
        {
            elevationDecodeScratch.clear();
            if (!elevation.decodeTile(req.elevationHash, elevationDecodeScratch))
                return false;
            elevationCpuCache.emplace(req.elevationHash, elevationDecodeScratch);
            decodePtr = elevationCpuCache.find(req.elevationHash)->second.data();
        }

        earthworks::DecodedImage img{};
        img.data            = decodePtr;
        img.width           = kElevTexels;
        img.height          = kElevTexels;
        img.channels        = 1;
        img.bytesPerChannel = 2;
        UploadElevation(req.elevationHash, img);
        if (!elevationPool.find(req.elevationHash, slice))
            return false;
    }

    bicubicOut.offset[0]    = 0.f;
    bicubicOut.offset[1]    = 0.f;
    bicubicOut.size[0]      = pixelSize / entry->size;
    bicubicOut.size[1]      = pixelSize / entry->size;
    bicubicOut.hgt_offset   = entry->hgt_offset;
    bicubicOut.hgt_scale    = entry->hgt_scale;
    bicubicOut.useRoot      = 0;
    bicubicOut.inputSlice   = static_cast<int>(slice);
    return true;
#else
    (void)bicubicOut;
    (void)pixelSize;
    return false;
#endif
}

} // namespace earthworksfx
