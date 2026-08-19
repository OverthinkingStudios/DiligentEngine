

SamplerState linearSampler : register(s0);


Texture2D<float3>       hdr : register(t1);  // read-only SRV: must be 't', not 'u' (DXC rejects a UAV register here)
RWTexture2D<float3>     output : register(u1);
Texture3D<float3>       cube : register(t0);

cbuffer gConstants
{
    float avsLum;
    int   debugView;   // 0 = normal, 1 = raw HDR (no ACES/LUT), 2 = solid test colour
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
    // Full-screen quad as 6 NON-INDEXED vertices (2 triangles). The tonemapper's
    // GraphicsState is given a null-index-buffer VAO in Earthworks_4 so this takes the
    // same non-indexed draw path the debug grid uses successfully; SV_VertexID is then a
    // plain 0..5 counter. All corners are inside [-1,1] (no oversized-triangle guard-band
    // clip). This replaced the half-screen result from the pixelShader indexed-draw path.
    const float2 corners[6] = {
        float2(-1.0,  1.0), float2( 1.0,  1.0), float2(-1.0, -1.0),  // TL, TR, BL
        float2( 1.0,  1.0), float2( 1.0, -1.0), float2(-1.0, -1.0)   // TR, BR, BL
    };
    const float2 uvs[6] = {
        float2(0, 0), float2(1, 0), float2(0, 1),
        float2(1, 0), float2(1, 1), float2(0, 1)
    };
    VSQuadOut output;
    output.position = float4(corners[vId], 0.0, 1.0);
    output.uv       = uvs[vId];
    return output;
}



float4 psMain(VSQuadOut vIn) : SV_TARGET0
{
    float3 hdrColor = hdr[vIn.position.xy];

    // --- bring-up diagnostics (EarthworksFX -> DiligentEngine) --------------
    // Everything drawn into the HDR FBO only reaches the screen through this pass,
    // so these modes isolate where a "black screen" comes from.
    if (debugView == 2)
        return float4(0.6, 0.0, 0.6, 1.0);            // solid magenta: does this pass write the swapchain?
    if (debugView == 1)
        return float4(saturate(hdrColor), 1.0);       // raw HDR: is there ANY content in hdrFbo?

    float4 aces = float4(ACESFilm(hdrColor * 1.7943), 1);
    float3 cc = cube.SampleLevel(linearSampler, aces.rgb * 1.0, 0);
    //if (vIn.position.x > 1300)
    aces.rgb = lerp(aces.rgb, cc, 0.2);
    return aces;
}
