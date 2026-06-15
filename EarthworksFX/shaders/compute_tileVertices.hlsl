// Adaptive vertex seeding: marks pixels where the surface curvature exceeds a
// LOD-scaled cutoff, packing the seed coordinate into the half-res vertex map.
// Ported from docs/source_extract/shaders/compute_tileVertices.hlsl (debug
// outputs removed).

#include "terrainDefines.hlsli"
#include "terrainFunctions.hlsli"
#include "terrain_structs.hlsli"

SamplerState                        linearSampler;
Texture2D<float>                    gInHgt;
RWTexture2D<uint>                   gOutVerts;
RWStructuredBuffer<centerFeedback>  tileCenters;
RWStructuredBuffer<gpuTile>         tiles;

cbuffer gConstants
{
    float4 constants; // .x pixSize*vertScale  .w tileIndex
};
#define _PIXSIZE constants.x

bool testPixel(const int2 crd, const uint size, const float cutoff)
{
    int3 hCrd = int3(crd * 2, 0);
    float avs = gInHgt.Load(hCrd + int3(-int(size), -int(size), 0)) +
                gInHgt.Load(hCrd + int3(-int(size),  int(size), 0)) +
                gInHgt.Load(hCrd + int3( int(size), -int(size), 0)) +
                gInHgt.Load(hCrd + int3( int(size),  int(size), 0));

    float mid = gInHgt.Load(hCrd);

    return (abs(mid - (avs * 0.25)) > cutoff);
}

[numthreads(tile_cs_ThreadSize, tile_cs_ThreadSize, 1)]
void main(int2 coord : SV_DispatchThreadId)
{
    if (coord.x == 0 && coord.y == 0)
    {
        uint  tileIdx      = (uint)constants.w;
        float centerHeight = gInHgt.SampleLevel(linearSampler, float2(0.5, 0.5), 0);
        tileCenters[tileIdx].min = centerHeight;
        tiles[tileIdx].origin.y  = centerHeight - (tiles[tileIdx].scale_1024 * 2048);
    }

    uint idx = 0;

    if (coord.x <= 28 && coord.y <= 28)
    {
        uint2 pixCrd = coord * 4 + 9;
        if (testPixel(pixCrd, 4, _PIXSIZE * 0.1))
        {
            InterlockedAdd(tiles[(uint)constants.w].numVerticis, 1, idx);
            gOutVerts[pixCrd] = (pixCrd.y << 7) + pixCrd.x;
        }
    }
    GroupMemoryBarrierWithGroupSync();

    if (coord.x <= 61 && coord.y <= 61)
    {
        uint2 pixCrd = coord * 2 + 3;
        if (testPixel(pixCrd, 2, _PIXSIZE * 0.15))
        {
            InterlockedAdd(tiles[(uint)constants.w].numVerticis, 1, idx);
            gOutVerts[pixCrd] = (pixCrd.y << 7) + pixCrd.x;
        }
    }
    GroupMemoryBarrierWithGroupSync();

    float lastscale = 0.1;
    if (idx > 20) lastscale = 0.15;

    if (coord.x <= 124 && coord.y <= 124)
    {
        uint2 pixCrd = coord + 2;
        if (testPixel(pixCrd, 1, _PIXSIZE * lastscale))
        {
            InterlockedAdd(tiles[(uint)constants.w].numVerticis, 1, idx);
            gOutVerts[pixCrd] = (pixCrd.y << 7) + pixCrd.x;
        }
    }
}
