
Texture2D       galbedo : register(t0);
Texture2D       galpha : register(t1);
Texture2D       gnormal : register(t2);
Texture2D       gtranslucency : register(t3);

SamplerState gSmpLinear : register(s1);


cbuffer gConstantBuffer
{
    float2 A;
    float2 B;
    float2 C;
    float2 D;

    float2 start;
    float2 stop;
    float2 bezier;
    float width;
    float padd;

    bool flipRed;
    bool flipGreen;
    float nStrength;
    bool toSRGB;
};



float2 Pos(float dt)
{
    float2 D = lerp(start, bezier, dt);
    float2 E = lerp(bezier, stop, dt);
    return lerp(D, E, dt);
}

float2 tangent(float dt)
{
    float2 D = lerp(start, bezier, dt);
    float2 E = lerp(bezier, stop, dt);
    return normalize(E - D);
}

float2 bitangent(float dt)
{
    float2 D = lerp(start, bezier, dt);
    float2 E = lerp(bezier, stop, dt);
    float2 tangent = normalize(E - D);
    return float2(-tangent.y, tangent.x);
}


// FIXME play with half res
struct PSIn
{
    float4 pos      : SV_Position;
    float2 tangent  : TANGENT;
    float2 binormal : BINORMAL;
    float2 uv       : TEXCOORD;
};



PSIn vsMain(uint vId : SV_VertexID, uint iId : SV_InstanceID)
{
    PSIn output = (PSIn) 0;
    return output;
}


[maxvertexcount(8)]
void gsMain(point PSIn P[1], inout    TriangleStream<PSIn> OutputStream)
{
    PSIn v = P[0];

            
    v.uv = float2(0, 0);// A;
    v.tangent = normalize(B - A);
    v.binormal = normalize(A - D);
    v.pos = float4(-1, -1, 0.5, 1);
    OutputStream.Append(v);

    v.uv = float2(1, 0);//B;
    v.pos = float4(1, -1, 0.5, 1);
    OutputStream.Append(v);

    v.uv = float2(0, 1);//D;
    v.pos = float4(-1, 1, 0.5, 1);
    OutputStream.Append(v);

    v.uv = float2(1, 1);//C;
    v.pos = float4(1, 1, 0.5, 1);
    OutputStream.Append(v);
            
}


struct PS_OUTPUT
{
    float4 albedo : SV_Target0;
    float4 normal : SV_Target1;
    float4 translucency : SV_Target2;
    float4 extra : SV_Target3;
    float4 light45 : SV_Target4;
};

PS_OUTPUT psMain(PSIn vOut) : SV_TARGET
{
    PS_OUTPUT P = (PS_OUTPUT)0;


    float2 NewUV;
    float2 middle = Pos(vOut.uv.y);
    float2 T = tangent(vOut.uv.y);
    float2 BT = bitangent(vOut.uv.y);

    NewUV = middle + BT * (vOut.uv.x - 0.5) * width * 2;
    
    P.albedo = galbedo.Sample(gSmpLinear, NewUV);
    P.albedo.a = saturate(galpha.Sample(gSmpLinear, NewUV).r + 0.01);
    P.translucency = gtranslucency.Sample(gSmpLinear, NewUV);

    float3 N = gnormal.Sample(gSmpLinear, NewUV).rgb;
    N -= 0.5;
    N *= 2;
    N.g *= -1;      // because texture is flipped

    float3 normProj = N;
    normProj.r = dot(N.rg, BT);
    //T = float2(0, 1);
    normProj.g = dot(N.rg, T);

    normProj.rg *= nStrength;
    if (length(normProj.rg) >= 1) normProj.rg = normalize(normProj.rg);


    normProj.b = 1 - sqrt(normProj.r * normProj.r + normProj.g * normProj.g);

    if (flipRed) normProj.r *= -1;
    if (flipGreen) normProj.g *= -1;

    P.light45.a = 1;
    P.light45.rgb = saturate(dot(normalize(float3(1, 1, 1)), normProj)) * float3(1.5, 1.3, 0.7) * 2.5;
    P.light45.rgb += float3(0, 0, 0.2);
    P.light45.rgb *= P.albedo.rgb;

    float dt = dot(normalize(float3(1, 1, 1)), normProj);
    float spec = saturate(pow(dt, 15));
    P.light45.rgb += spec *110;

    //P.light45.rgb = normProj.r / 2 + 0.5;

    normProj /= 2;
    normProj += 0.5;
    P.normal = float4(normProj,1);

    if (toSRGB)
    {
        P.albedo.rgb = pow(P.albedo.rgb, 0.45);
        P.translucency.rgb = pow(P.translucency.rgb, 0.45);
    }

    return P;
}
