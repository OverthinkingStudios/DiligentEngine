// Adaptive vertex seeding: marks pixels where the surface curvature exceeds a
// LOD-scaled cutoff, packing the seed coordinate into the half-res vertex map.
// Ported from docs/source_extract/shaders/compute_tileVertices.hlsl (debug
// outputs removed).
//
// neighborLodN/E/S/W + tileLod constrain edge seeds when an adjacent tile is
// coarser, preventing T-junction holes at LOD boundaries.

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
    int4   neighborLod; // N, E, S, W
    int    tileLod;
    int3   _padNbor;
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

void stitchEdge(int nLod, int2 crd, int fixedAxis, int fixedVal)
{
    // No adjacent tile (world exterior) — leave adaptive-only edge behaviour.
    if (nLod < 0)
        return;

    // Same-LOD siblings need dense edge seeds (step=1). Coarser neighbours
    // need sparser seeds; finer neighbours need the coarse tile to subdivide.
    int step = 1;
    if (nLod > tileLod)
        step = max(1, 1 << (nLod - tileLod));
    else if (nLod < tileLod)
        step = max(1, 1 << (tileLod - nLod));

    if (fixedAxis == 0)
    {
        if (crd.y != fixedVal || (crd.x % step) != 0)
            return;
    }
    else
    {
        if (crd.x != fixedVal || (crd.y % step) != 0)
            return;
    }

    // Packed idx uses the flood-grid coordinate (7 bits per axis), matching
    // render_Tiles: world xz from (idx & 0x7f) * 2.
    gOutVerts[crd] = (uint(crd.y) << 7) | uint(crd.x);
}

[numthreads(tile_cs_ThreadSize, tile_cs_ThreadSize, 1)]
void main(int2 coord : SV_DispatchThreadId)
{
    // texVertsA is shared across all tile bakes; zero before seeding so a prior
    // bake cannot leave stale indices that delaunay would connect to (0, 0).
    if (coord.x < 128 && coord.y < 128)
        gOutVerts[coord] = 0;

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

    // Align edge seeds to coarser neighbors (half-res vertex grid 0..127).
    stitchEdge(neighborLod.x, coord, 0, 127); // N
    stitchEdge(neighborLod.y, coord, 1, 127); // E
    stitchEdge(neighborLod.z, coord, 0, 0);   // S
    stitchEdge(neighborLod.w, coord, 1, 0);   // W
}
