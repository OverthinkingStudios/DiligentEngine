// Phase-1 per-frame reset. The original compute_tileClear also zeroes a large
// vegetation/feedback block (see docs/source_extract/shaders/compute_tileClear.hlsl);
// for the bare-terrain port we only need the terrain indirect-draw args and the
// lookup-block allocator reset.

#include "terrain_structs.hlsli"

RWStructuredBuffer<t_DrawArguments> DrawArgs_Terrain;
RWStructuredBuffer<uint>            lookupCounter;

[numthreads(1, 1, 1)]
void main()
{
    DrawArgs_Terrain[0].vertexCountPerInstance = 3;
    DrawArgs_Terrain[0].instanceCount          = 0;
    DrawArgs_Terrain[0].startVertexLocation    = 0;
    DrawArgs_Terrain[0].startInstanceLocation  = 0;

    lookupCounter[0] = 0;
}
