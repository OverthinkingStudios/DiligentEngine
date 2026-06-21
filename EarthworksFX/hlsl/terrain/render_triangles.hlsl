
#include "groundcover_defines.hlsli"
#include "groundcover_functions.hlsli"		
#include "../PBR.hlsli"





SamplerState gSampler;


Texture2D gAlbedo : register(t0);

TextureCube gSky : register(t1);


StructuredBuffer<triangleVertex>        instanceBuffer;
StructuredBuffer<xformed_PLANT>       instances;


cbuffer gConstantBuffer
{
    float4x4 viewproj;
    float3 eye;
    int useSkyDome;
};



struct VSOut
{
    float4 pos : SV_POSITION;
    float2 texCoords : TEXCOORD;
    float3 N : TEXCOORD1;
    float3 world : TEXCOORD2;
};




float4 cubic(float v)
{
    float4 n = float4(1.0, 2.0, 3.0, 4.0) - v;
    float4 s = n * n * n;
    float x = s.x;
    float y = s.y - 4.0 * s.x;
    float z = s.z - 4.0 * s.y + 6.0 * s.x;
    float w = 6.0 - x - y - z;
    return float4(x, y, z, w) * (1.0 / 6.0);
}

float4 textureBicubic(float2 texCoords)
{
    float4 texSize;
    gAtmosphereInscatter_Sky.GetDimensions(texSize.x, texSize.y);
    float2 invTexSize = 1.0 / texSize.xy;
   
    texCoords.xy = texCoords.xy * texSize.xy - 0.5;

   
    float2 fxy = frac(texCoords.xy);
    texCoords.xy -= fxy;

    float4 xcubic = cubic(fxy.x);
    float4 ycubic = cubic(fxy.y);

    float4 c = texCoords.xxyy + float2(-0.5, +1.5).xyxy;
    
    float4 s = float4(xcubic.xz + xcubic.yw, ycubic.xz + ycubic.yw);
    float4 offset = c + float4(xcubic.yw, ycubic.yw) / s;
    
    offset *= invTexSize.xxyy;

    
    float4 sample0 = gAtmosphereInscatter_Sky.Sample(gSmpLinearClamp, offset.xz);
    float4 sample1 = gAtmosphereInscatter_Sky.Sample(gSmpLinearClamp, offset.yz);
    float4 sample2 = gAtmosphereInscatter_Sky.Sample(gSmpLinearClamp, offset.xw);
    float4 sample3 = gAtmosphereInscatter_Sky.Sample(gSmpLinearClamp, offset.yw);


    float sx = s.x / (s.x + s.y);
    float sy = s.z / (s.z + s.w);

    return lerp(lerp(sample3, sample2, sx), lerp(sample1, sample0, sx), sy);
}



VSOut vsMain(uint vId : SV_VertexID, uint iId : SV_InstanceID)
{
    VSOut output = (VSOut)0;

    const xformed_PLANT plant = instances[iId];
    const uint P = 0;// plant.index * 128;
    const triangleVertex R = instanceBuffer[vId + P];

    //float3 vPos = float3(R.pos.x * plant.rotation.x + R.pos.z * plant.rotation.y, R.pos.y, R.pos.x * plant.rotation.y - R.pos.z * plant.rotation.x);
    //output.pos = mul( float4(plant.position + vPos * plant.scale*40.4, 1), viewproj );
    float3 vPos = R.pos *18000 + eye;
    output.pos = mul(float4(vPos, 1), viewproj);
    output.pos.z = 1;
    output.pos.w = 1.000002;
    output.texCoords = float2(R.u, R.v);
    output.N = R.norm;
    //output.world = normalize(eye - plant.position);
    output.world = R.pos;

    return output;
}





float4 psMain(VSOut vOut, bool isFrontFace : SV_IsFrontFace) : SV_TARGET
{


    float3 dir = normalize(vOut.world.xyz);

    //return float4(dir, 1);
 //   if (dir.z > 0.99)
  //      return float4(0, 0, 1, 1);

    //if (dir.x > 0.99)
      //  return float4(1, 0, 0, 1);


    if (useSkyDome == 1)
    {
    
        //dir.z *= -1;
        return gSky.SampleLevel(gSampler, dir, 0);
    //return float4(normalize(vOut.world.xyz) * 0.5 + 0.5, 1);
    
        float3 normal = vOut.N * (2 * isFrontFace - 1);

        float3 sunDir = normalize(float3(0.7, -0.3, 0.2));
        float3 spec = reflect(sunDir, normal);
        float A = dot(normal, sunDir);
        float A_backface = saturate(-A);
        A = saturate(A + 0.1);
        float S = pow(abs(dot(vOut.world, spec)), 25);

        float3 colour = float3(0.07, 0.1, 0.05) * 0.5;
        float3 final = colour + float3(0.1, 0.1, 0) * A_backface + S * 0.2;
        return float4(final, 1);
    }
    else if (useSkyDome == 2)       // FIXME do with #defines 
    {
        float4 texSize;
        gAtmosphereInscatter.GetDimensions(0, texSize.x, texSize.y, texSize.z, texSize.w);

        texSize.x = 2550;
        texSize.y = 1440;

        texSize.x = 4096;
        texSize.y = 2160;
    
        float2 atmosphereUV;
        atmosphereUV = vOut.pos.xy / texSize.xy;
    //clip(-1);
        float4 inscatter = textureBicubic(atmosphereUV);
        inscatter.a = 1;
        

    //inscatter.xy = atmosphereUV / 3;
        return inscatter;
    }
    else
    {
        float4 texSize;
        gAtmosphereInscatter.GetDimensions(0, texSize.x, texSize.y, texSize.z, texSize.w);

        texSize.x = 2550;
        texSize.y = 1440;
        texSize.x = 4096;
        texSize.y = 2160;
    
        float2 atmosphereUV;
        atmosphereUV = vOut.pos.xy / texSize.xy;
    //clip(-1);
        float4 inscatter = textureBicubic(atmosphereUV);
        inscatter.a = 1;
        

    //inscatter.xy = atmosphereUV / 3;
        return inscatter;
    }

}
