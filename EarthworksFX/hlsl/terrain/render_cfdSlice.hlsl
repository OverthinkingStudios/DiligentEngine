#include "groundcover_defines.hlsli"
#include "groundcover_functions.hlsli"

SamplerState gSampler;
SamplerState gSamplerClamp;




Texture2D gV : register(t0);
Texture2D gData : register(t1);

Texture2D gV_1 : register(t2);
Texture2D gData_1 : register(t3);

cbuffer gConstantBuffer
{
    float4x4 viewproj;
    float3 eyePos;

    float dT;

    float3 pos_0;
    float3 pos_1;
    float3 pos_2;
    float3 pos_3;
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

    if (vId == 0)
        output.pos = mul(float4(pos_0, 1), viewproj);
    if (vId == 1)
        output.pos = mul(float4(pos_1, 1), viewproj);
    if (vId == 2)
        output.pos = mul(float4(pos_2, 1), viewproj);
    if (vId == 3)
        output.pos = mul(float4(pos_3, 1), viewproj);
    
    output.uv.x = vId & 0x1;
    output.uv.y = 1 - (vId >> 1);
    output.eye = normalize(output.pos.xyz - eyePos);
    
    return output;
}



float to_K(float c)
{
    return c + 273.15f;
}
float to_C(float k)
{
    return k - 273.15f;
}

// in Celcius
float dew_Y(float RH32)
{
    //return log(RH32) + 17.625 * (32.f) / (243.04 + 32.f);
    return log(RH32) + 2.1942110177f;
}

float dew_Temp_C(float RH32)
{
    float y = dew_Y(RH32);
    return (243.04f * y) / (17.625f - y);
}


float4 psMain(PSIn vOut) : SV_TARGET
{
    float3 V1 = gV.Sample(gSamplerClamp, vOut.uv).rgb;
    float3 V2 = gV_1.Sample(gSamplerClamp, vOut.uv).rgb;
    float3 V = lerp(V1, V2, dT);
    
    float3 Data1 = gData.Sample(gSamplerClamp, vOut.uv).rgb;
    float3 Data2 = gData_1.Sample(gSamplerClamp, vOut.uv).rgb;
    float3 Data = lerp(Data1, Data2, dT);
    
    float a = 1;
    a = saturate(pow(Data.b * 1.1, 0.9));

    float3 color = 0;

    //if (V.y > 0)        color.r = V.y * 0.3;
    //else
    //    color.b = -V.y * 0.3;

    color = lerp(color, float3(0.1, 0.1, 0.1) * 0.50, a);

    float C = to_C(Data.x);
    float DewC = dew_Temp_C(Data.y);
    float cloud = saturate((DewC - C) / 2);
    color = lerp(color, float3(0.3, 0.3, 0.3) * 0.6, cloud);
    a = max(a, cloud);
    //a = 1;
    return 0;
    return float4(color, pow(a, 0.3));
}
