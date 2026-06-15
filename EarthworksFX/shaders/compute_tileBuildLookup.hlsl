// Phase-1 indirect-draw builder. For every visible "surface" tile, split its
// triangle count into 64-triangle blocks, append a lookup entry per block and
// grow the terrain indirect draw. Terrain-only subset of
// docs/source_extract/shaders/compute_tileBuildLookup.hlsl (the plant/quad
// lookups and the GC_feedback bookkeeping are dropped). A dedicated
// lookupCounter replaces feedback[0].numLookupBlocks_Terrain.

#include "terrainDefines.hlsli"
#include "terrain_structs.hlsli"

RWStructuredBuffer<tileLookupStruct> terrainLookup;
RWStructuredBuffer<gpuTile>          tiles;
RWStructuredBuffer<t_DrawArguments>  DrawArgs_Terrain;
RWStructuredBuffer<uint>             lookupCounter;

cbuffer gConstants
{
    uint4 frustumflags[1024];
};

uint unpackFrustum(uint x)
{
    uint index  = x >> 2;
    uint offset = x & 0x3;
    switch (offset)
    {
        case 0: return frustumflags[index].x;
        case 1: return frustumflags[index].y;
        case 2: return frustumflags[index].z;
        case 3: return frustumflags[index].w;
    }
    return 0;
}

[numthreads(32, 1, 1)]
void main(uint dispatchId : SV_DispatchThreadId)
{
    const uint t = dispatchId;

    if ((t > 0) && (t < 1000))
    {
        tiles[t].flags = 0;

        if (unpackFrustum(t) & (1 << 20))
        {
            tiles[t].flags = 1 << 31;

            uint totalTriangles = tiles[t].numTriangles;
            uint numB           = (totalTriangles >> 6) + 1;

            uint blockBase = 0;
            InterlockedAdd(lookupCounter[0], numB, blockBase);

            uint dummy = 0;
            InterlockedAdd(DrawArgs_Terrain[0].instanceCount, 64 * numB, dummy);

            for (uint i = 0; i < numB; i++)
            {
                uint numTriangles = min(totalTriangles, 64);
                totalTriangles -= 64;

                terrainLookup[blockBase + i].tile   = t + (numTriangles << 16);
                terrainLookup[blockBase + i].offset = (t * numVertPerTile) + (i * 64 * 3);
            }
        }
    }
}
