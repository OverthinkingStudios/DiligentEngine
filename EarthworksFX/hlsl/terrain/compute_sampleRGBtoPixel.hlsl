

#include "terrainDefines.hlsli"
#include "terrainFunctions.hlsli"
#include "groundcover_defines.hlsli"





Texture2D<float3> 					gIn;
RWTexture2D<float4> 					gOut;

RWStructuredBuffer<uint> data;

cbuffer gConstants
{
    uint4 counters;
    uint2 pix;
};


groupshared uint vCount = 0;
groupshared uint R = 0;
groupshared uint G = 0;
groupshared uint B = 0;
groupshared uint3 vSum = 0;


#if defined(_TO_TEXTURE)

[numthreads(32, 32, 1)]
void main(int2 coord : SV_DispatchThreadId)
{
    uint index = (coord.y * 256 + coord.x) * 4;

    if (data[index] > 0)
    {
        data[index + 1] /= data[index];
        data[index + 2] /= data[index];
        data[index + 3] /= data[index];
        gOut[coord] = float4(data[index + 1] / 1024.f, data[index + 2] / 1024.f, data[index + 3] / 1024.f + 0.2, 1);
    }
}

#else

[numthreads(32, 32, 1)]	
void main(int2 coord : SV_DispatchThreadId)
{
    uint index = (pix.y * 256 + pix.x) * 4;


    uint cnt;

    float3 rgb = gIn[coord];
    if (any(rgb))
    {
        InterlockedAdd(data[index], 1, cnt);
    }

    uint rr = 1024 * rgb.r;
    uint gg = 1024 * rgb.g;
    uint bb = 1024 * rgb.b;
    InterlockedAdd(data[index + 1], rr, cnt);
    InterlockedAdd(data[index + 2], gg, cnt);
    InterlockedAdd(data[index + 3], bb, cnt);

    
}

#endif
