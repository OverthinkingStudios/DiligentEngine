// Jump-flood the sparse vertex seeds so every cell in the half-res grid points
// at its nearest seed. Ported from
// docs/source_extract/shaders/compute_tileJumpFlood.hlsl (debug output removed).
//
// Ping-pong rule (do not "fix" this): a thread only writes gOutVerts when it
// finds a nearer seed in the step neighbourhood. Cells that are not written
// keep whatever is already in the output texture — pass 2 retains vertex seeds
// still in texVertsA; pass 3 retains pass-1 flood values still in texVertsB.
// The first pass (step==4) into texVertsB must explicitly stamp 0 when no seed
// is reachable so a prior tile bake cannot leave stale indices in the shared
// ping-pong surfaces.

#include "terrainDefines.hlsli"
#include "terrainFunctions.hlsli"

Texture2D<uint>   gInVerts;
RWTexture2D<uint>  gOutVerts;

cbuffer gConstants
{
    uint step;
    uint3 _pad;
};

uint loadVert(int2 p)
{
    if (any(p < 0) || any(p > int2(127, 127)))
        return 0;
    return gInVerts[p];
}

[numthreads(tile_cs_ThreadSize, tile_cs_ThreadSize, 1)]
void main(int2 crd : SV_DispatchThreadId)
{
    float dstSqr;
    float smallest = 500000;
    uint  V;

    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            V = loadVert(crd + int2(x, y) * int(step));
            if (V != 0)
            {
                const int2 delta = int2(V & 0x7f, (V >> 7) & 0x7f) - crd;
                dstSqr = dot(delta, delta);
                if (dstSqr < smallest)
                {
                    smallest = dstSqr;
                    gOutVerts[crd] = V;
                }
            }
        }
    }

    // First pass only (A->B, step==4): purge stale texVertsB from the previous
    // tile bake. Later passes rely on unwritten cells retaining prior contents.
    if (smallest >= 500000 && step == 4)
        gOutVerts[crd] = 0;
}
