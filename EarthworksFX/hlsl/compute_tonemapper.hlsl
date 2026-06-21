

SamplerState linearSampler : register(s0);


Texture2D<float3>       hdr : register(u0);
RWTexture2D<float3>     output : register(u1);
Texture3D<float3>       cube : register(t0);

cbuffer gConstants
{
    float avsLum;
};


struct VSQuadOut {
    float4 position : SV_Position;
    float2 uv: TexCoord;
};


float3 ACESFilm(float3 x)
{
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}


VSQuadOut vsMain(uint vId : SV_VertexID)
{
    VSQuadOut output;
    output.uv = float2((vId << 1) & 2, vId & 2);
    output.position = float4(output.uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}



float4 psMain(VSQuadOut vIn) : SV_TARGET0
{
    float4 aces = float4(ACESFilm(hdr[vIn.position.xy] * 1.943), 1);
    float3 cc = cube.SampleLevel(linearSampler, aces.rgb * 1.0, 0);
    //if (vIn.position.x > 1300)
    aces.rgb = lerp(aces.rgb, cc, 0.2);
    return aces;
}
