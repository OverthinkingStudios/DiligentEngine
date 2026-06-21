#include "groundcover_defines.hlsli"
#include "groundcover_functions.hlsli"

SamplerState gSampler;
SamplerState gSamplerClamp;



StructuredBuffer<float4> vertexBuffer;



cbuffer gConstantBuffer
{
    float4x4 viewproj;
    float3 eyePos;
};




struct PSIn
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD; // texture uv
    float3 eye : TEXCOORD1; // can this become SVPR  or per plant somehow, feels overkill here per vertex
};



PSIn vsMain(uint vId : SV_VertexID, uint iId : SV_InstanceID)
{
    PSIn output = (PSIn) 0;

    output.pos = vertexBuffer[iId * 100 + vId];
    output.pos.w = 1;
    output.uv.x = vId;
    output.uv.y = vertexBuffer[iId * 100 + vId].w;
    output.eye = normalize(output.pos.xyz - eyePos);
    
    return output;
}

[maxvertexcount(4)]
void gsMain(line PSIn L[2], inout TriangleStream<PSIn> OutputStream)
{
    PSIn v;

    float3 up = L[1].pos.xyz - L[0].pos.xyz;
    float3 right = normalize(cross(up, L[0].eye));
    
    float4 tangent = 0;
    if (L[0].uv.x < 0.6)
    {
        tangent.xyz = right * 3;
    }
    else
    {
        tangent.xyz = right * 3;
    }

    tangent.xyz = right * 1.15;

//    if (L[1].uv.y < .05)
 //       tangent *= 10;
        

        if (L[0].uv.x < 99.5)
        {
    
            v = L[0];
            v.pos = mul(L[0].pos - tangent, viewproj);
            OutputStream.Append(v);

            v.pos = mul(L[0].pos + tangent, viewproj);
            OutputStream.Append(v);

        
            v = L[1];
            v.uv.x = L[0].uv.x;
            v.pos = mul(L[1].pos - tangent, viewproj);
            OutputStream.Append(v);

            v.pos = mul(L[1].pos + tangent, viewproj);
            OutputStream.Append(v);
        }
}



float4 psMain(PSIn vOut) : SV_TARGET
{
    float speed = pow(vOut.uv.y / 3.0, 2.2);
    //float speed = vOut.uv.y * 100;
    //float speed = vOut.uv.y / 5;
    float3 col = 0;

    if (speed < 1)         col = lerp(float3(0, 0, 0), float3(0, 0, 1), speed);
    else if (speed < 2)    col = lerp(float3(0, 0, 1), float3(0, 1, 0), speed-1);
    else if (speed < 3)    col = lerp(float3(0, 1, 0), float3(1, 1, 0), speed-2);
    else if (speed < 4)    col = lerp(float3(1, 1, 0), float3(1, 0, 0), speed-3);
    else                   col = lerp(float3(1, 0, 0), float3(1, 1, 1), speed-4);

    return float4(col * 0.2f, 1);
    
        float alpha = 1;
    if (vOut.uv.x < 0.5)
    {
        alpha = 0.2;
    }
    
    if (vOut.uv.y < 0.5)
        return float4(0, 0, 1, 1) * alpha;
    if (vOut.uv.y < 1.0)
        return float4(0, 1, 0, 1) * alpha;
    if (vOut.uv.y < 2.0)
        return float4(1, 1, 0, 1) * alpha;
    
    return float4(1, 0, 0, 1);
}
