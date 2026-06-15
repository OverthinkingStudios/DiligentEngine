// Cross normals from the baked height texture, written to the per-tile normal
// texture (stored xyz*0.5+0.5). Ported from
// docs/source_extract/shaders/compute_tileNormals.hlsl; the debug MRT write and
// the unused tiles buffer are dropped for phase 1.

#include "terrainDefines.hlsli"

Texture2D<float>    gInHgt;
RWTexture2D<float4>  gOutNormals;

cbuffer gConstants
{
    float  pixSize;
    float3 padd;
};

[numthreads(tile_cs_ThreadSize, tile_cs_ThreadSize, 1)]
void main(uint2 crd : SV_DispatchThreadId)
{
    int2 crd_clamped = clamp(int2(crd), 1, tile_numPixels - 2);
    float dx = gInHgt[crd_clamped + int2(-1, 0)].r - gInHgt[crd_clamped + int2(1, 0)].r;
    float dy = gInHgt[crd_clamped + int2(0, -1)].r - gInHgt[crd_clamped + int2(0, 1)].r;

    float3 n = normalize(float3(dx, 2.0 * pixSize, dy));
    gOutNormals[crd] = float4(n * 0.5 + 0.5, 1.0);
}
