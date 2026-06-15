// Bicubic resample of the source elevation into the per-tile 256x256 height
// texture. Ported from docs/source_extract/shaders/compute_tileBicubic.hlsl,
// height (isHeight==1) path only — the orthophoto/albedo path is a later phase.

#include "terrainDefines.hlsli"

SamplerState        linearSampler;
Texture2D<float>    gInput;
RWTexture2D<float>  gOutput;

cbuffer gConstants
{
    float2 offset;
    float2 size;
    float  hgt_offset;
    float  hgt_scale;
    int    isHeight;
    float  _pad0;
};

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
    float2 texSize;
    gInput.GetDimensions(texSize.x, texSize.y);
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

    float sample0 = gInput.SampleLevel(linearSampler, offsetB.xz, 0).r;
    float sample1 = gInput.SampleLevel(linearSampler, offsetB.yz, 0).r;
    float sample2 = gInput.SampleLevel(linearSampler, offsetB.xw, 0).r;
    float sample3 = gInput.SampleLevel(linearSampler, offsetB.yw, 0).r;

    float sx = s.x / (s.x + s.y);
    float sy = s.z / (s.z + s.w);

    float H = lerp(lerp(sample3, sample2, sx), lerp(sample1, sample0, sx), sy);

    gOutput[crd] = (H * hgt_scale) + hgt_offset;
}
