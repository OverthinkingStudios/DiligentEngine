// Bicubic resample of the source elevation into the per-tile 256x256 height
// texture. Ported from docs/source_extract/shaders/compute_tileBicubic.hlsl,
// height (isHeight==1) path only — the orthophoto/albedo path is a later phase.
//
// gInputRoot (R32F LOD-0 grid) and gInputTiles (R16 JP2 cache array) are bound
// once; per-tile selection is via useRoot / inputSlice in the cbuffer so bakes do
// not rebind the SRB (which would allocate Vulkan descriptors from the dynamic heap).

#include "terrainDefines.hlsli"

SamplerState           linearSampler;
Texture2D<float>       gInputRoot;
Texture2DArray<float>  gInputTiles;
RWTexture2D<float>     gOutput;

cbuffer gConstants
{
    float2 offset;
    float2 size;
    float  hgt_offset;
    float  hgt_scale;
    int    isHeight;
    int    useRoot;
    int    inputSlice;
    int    _pad0;
};

float sampleInput(float2 uv)
{
    if (useRoot != 0)
        return gInputRoot.SampleLevel(linearSampler, uv, 0).r;
    return gInputTiles.SampleLevel(linearSampler, float3(uv, inputSlice), 0).r;
}

float4 cubic(float v)
{
    float4 n = float4(1.0, 2.0, 3.0, 4.0) - v;
    float4 s = n * n * n;
    float  x = s.x;
    float  y = s.y - 4.0 * s.x;
    float  z = s.z - 4.0 * s.y + 6.0 * s.x;
    float  w = 6.0 - x - y - z;
    return float4(x, y, z, w) * (1.0 / 6.0);
}

[numthreads(tile_cs_ThreadSize, tile_cs_ThreadSize, 1)]
void main(int2 crd : SV_DispatchThreadId)
{
    uint w, h;
    if (useRoot != 0)
        gInputRoot.GetDimensions(w, h);
    else
    {
        uint slices;
        gInputTiles.GetDimensions(w, h, slices);
    }
    float2 texSize = float2(w, h);
    float2 invTexSize = 1.0 / texSize;

    float2 iTc = ((crd - 4.0) * size + offset) * texSize - 0.5;

    float2 f = frac(iTc);
    iTc -= f;

    float4 xcubic = cubic(f.x);
    float4 ycubic = cubic(f.y);

    float4 c = iTc.xxyy + float2(-0.5, +1.5).xyxy;

    float4 s       = float4(xcubic.xz + xcubic.yw, ycubic.xz + ycubic.yw);
    float4 offsetB = c + float4(xcubic.yw, ycubic.yw) / s;

    offsetB *= invTexSize.xxyy;

    float sample0 = sampleInput(offsetB.xz);
    float sample1 = sampleInput(offsetB.yz);
    float sample2 = sampleInput(offsetB.xw);
    float sample3 = sampleInput(offsetB.yw);

    float sx = s.x / (s.x + s.y);
    float sy = s.z / (s.z + s.w);

    float H = lerp(lerp(sample3, sample2, sx), lerp(sample1, sample0, sx), sy);

    gOutput[crd] = (H * hgt_scale) + hgt_offset;
}
