// GPU init for four child gpuTile records on quadtree split.
// Ported from docs/source_extract/shaders/compute_tileSplitMerge.hlsl (feedback removed).

#include "terrain_structs.hlsli"

RWStructuredBuffer<gpuTile> tiles;

cbuffer gConstants
{
    tileForSplit child[4];
};

[numthreads(1, 1, 1)]
void main(uint dispatchId : SV_DispatchThreadId)
{
    for (int i = 0; i < 4; ++i)
    {
        tiles[child[i].index].flags         = 0;
        tiles[child[i].index].origin        = child[i].origin;
        tiles[child[i].index].scale_1024    = child[i].scale / 1024.0;
        tiles[child[i].index].lod           = child[i].lod;
        tiles[child[i].index].Y             = child[i].y;
        tiles[child[i].index].X             = child[i].x;
        tiles[child[i].index].numPlants     = 0;
        tiles[child[i].index].numQuads      = 0;
        tiles[child[i].index].numTriangles  = 0;
        tiles[child[i].index].numVerticis   = 0;
    }
}
