// Jump-flood the sparse vertex seeds so every cell in the half-res grid points
// at its nearest seed. Ported from
// docs/source_extract/shaders/compute_tileJumpFlood.hlsl (debug output removed).

#include "terrainDefines.hlsli"
#include "terrainFunctions.hlsli"

Texture2D<uint>   gInVerts;
RWTexture2D<uint>  gOutVerts;

cbuffer gConstants
{
    uint step;
    uint3 _pad;
};

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
            V = gInVerts[crd + int2(x, y) * int(step)];
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
}
