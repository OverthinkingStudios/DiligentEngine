
#include "groundcover_defines.hlsli"
#include "groundcover_functions.hlsli"		
#include "vegetation_defines.hlsli"
#include "../render_Common.hlsli"



SamplerState gSampler;
//SamplerState gSmpLinearClamp;

TextureCube gEnv : register(t1);
Texture2D gHalfBuffer : register(t2);

struct ribbonTextures
{
    Texture2D<float4> T[4096];
};
ParameterBlock<ribbonTextures>  textures;



StructuredBuffer<instance_PLANT>        instanceBuffer;
RWStructuredBuffer<gpuTile> 			tiles;
StructuredBuffer<tileLookupStruct>		tileLookup;

StructuredBuffer<sprite_material> materials;
StructuredBuffer<plant> plant_buffer;



cbuffer gConstantBuffer
{
    float4x4 viewproj;
    
	float3 	right;
	int		alpha_pass;
    


    float3 eye;
};

cbuffer PerFrameCB
{
    bool gConstColor;
    float3 gAmbient;
	
	// volume fog parameters
    float2 screenSize;
    float fog_far_Start;
    float fog_far_log_F; //(k-1 / k) / log(far)		// FIXME might be k-2 to make up for half pixel offsets
	
    float fog_far_one_over_k; // 1.0 / k
    float fog_near_Start;
    float fog_near_log_F; //(k-1 / k) / log(far)		// FIXME might be k-2 to make up for half pixel offsets
    float fog_near_one_over_k; // 1.0 / k
	
    float gisOverlayStrength;
    int showGIS;
    float redStrength;
    float redScale;
    float4 gisBox;
	
    float redOffset;
    float3 padding;
	
};

float4 sunLight(float3 posKm)
{
    float2 sunUV;
    float dX = dot(posKm.xz, normalize(sunDirection.xz));
    sunUV.x = saturate(0.5 - (dX / 1600.0f));
    sunUV.y = 1 - saturate(posKm.y / 100.0f);
    return SunInAtmosphere.SampleLevel(gSmpLinearClamp, sunUV, 0) * 0.07957747154594766788444188168626;
}


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

float4 textureBicubic(float3 texCoords)
{
    float4 texSize;
    gAtmosphereInscatter.GetDimensions(0, texSize.x, texSize.y, texSize.z, texSize.w);
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

    
    float4 sample0 = gAtmosphereInscatter.Sample(gSmpLinearClamp, float3(offset.xz, texCoords.z));
    float4 sample1 = gAtmosphereInscatter.Sample(gSmpLinearClamp, float3(offset.yz, texCoords.z));
    float4 sample2 = gAtmosphereInscatter.Sample(gSmpLinearClamp, float3(offset.xw, texCoords.z));
    float4 sample3 = gAtmosphereInscatter.Sample(gSmpLinearClamp, float3(offset.yw, texCoords.z));


    float sx = s.x / (s.x + s.y);
    float sy = s.z / (s.z + s.w);

    return lerp(lerp(sample3, sample2, sx), lerp(sample1, sample0, sx), sy);
}

/*
struct VSOut
{
    float4 rootPos : ANCHOR;
    float scale : TEXCOORD;
    uint index : TEXCOORD1;
    float3 sunlight : TEXCOORD2;
    float3 inscatter : TEXCOORD3;
    float3 outscatter : TEXCOORD4;
    float3 worldPos : TEXCOORD5;
    float3 eye : TEXCOORD6;
};
*/
struct GSOut
{
    float4 pos : SV_POSITION;

    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 binormal : BINORMAL;
    
    float3 diffuseLight : COLOR;

    float2 uv : TEXCOORD;
    float scale : TEXCOORD1;
    float3 sunlight : TEXCOORD2;
    float3 inscatter : TEXCOORD3;
    float3 outscatter : TEXCOORD4;
    float3 worldPos : TEXCOORD5;
    float3 eye : TEXCOORD6;
    uint index : TEXCOORD7;
};


GSOut vsMain(uint vId : SV_VertexID, uint iId : SV_InstanceID)
{
    GSOut output = (GSOut) 0;

    
    uint tileIDX = tileLookup[iId].tile & 0xffff;
    uint numQuad = tileLookup[iId].tile >> 16;
    uint blockID = vId;// (iId & 0x3f);

    // FIXME just fill the rest of the 64 with NULL and render all much faster than this if
    if (blockID < numQuad)
    {
        uint newID = tileLookup[iId].offset + blockID;
        instance_PLANT plant = instanceBuffer[newID];

        output.pos = float4(unpack_pos(plant.xyz, tiles[tileIDX].origin, tiles[tileIDX].scale_1024), 1);
        output.scale = SCALE(plant.s_r_idx);
        output.index = PLANT_INDEX(plant.s_r_idx);
        output.sunlight = sunLight(output.pos.xyz * 0.001).rgb;

        // Now for my atmospeher code --------------------------------------------------------------------------------------------------------

        float3 atmosphereUV;
        float4 pos = mul(output.pos, viewproj);
        atmosphereUV.xy = pos.xy / pos.w / screenSize;
        atmosphereUV.z = log(length(output.pos.xyz - eye.xyz) / fog_far_Start) * fog_far_log_F + fog_far_one_over_k;
        atmosphereUV.z = max(0.01, atmosphereUV.z);
        atmosphereUV.z = min(0.99, atmosphereUV.z);
        output.outscatter = gAtmosphereOutscatter.SampleLevel(gSmpLinearClamp, atmosphereUV, 0 ).rgb;
        output.inscatter = gAtmosphereInscatter.SampleLevel(gSmpLinearClamp, atmosphereUV, 0).rgb;


        output.worldPos = output.pos.xyz;
        output.eye = normalize(output.pos.xyz - eye.xyz);
        output.uv = float2(0, 0);

        output.pos.xyz -= output.eye * 0.2f;

        output.binormal = float3(0, 1, 0);
        output.tangent = normalize(cross(output.binormal, -output.eye));
        output.normal = cross(output.binormal, output.tangent);

        output.diffuseLight = sunLight(output.pos.xyz * 0.001).rgb;
       //output.rootPos.xyz -= normalize(output.eye) * 100.f;
        //output.rootPos.y += 300;
        //output.rootPos.x += 7000;

    }
    return output;
}



[maxvertexcount(4)]
void gsMain(point GSOut sprite[1], inout TriangleStream<GSOut> OutputStream)
{
    GSOut v = sprite[0];


    const plant PLANT = plant_buffer[sprite[0].index];
    //const plant PLANT = plant_buffer[4];

    uint I = sprite[0].index;
    int dx = I & 0xf;
    int dy = I >> 4;
    //float SZ = plant.size.x; //    sprite[0].scale * size[I];
    
    
    //create sprite quad
    //--------------------------------------------
    float3 texRoot = float3(dx, dy, 0) * 0.0625;

    v.pos = mul(sprite[0].pos, viewproj);
    
    float X = abs(v.pos.x / v.pos.z);
    float Y = abs(v.pos.y / v.pos.z);
    if (v.pos.z < 1 || X > 1.3 || Y > 2.1)
    {
    
        //OutputStream.Append(v);
    }
    else
    {
        // add back in       float scale = 1 - pt[0].lineScale.z;
        float X = PLANT.size.x * 1.0 * sprite[0].scale;
        float Y = PLANT.size.y * sprite[0].scale;
        float scale = 1 - 0.8;

        v.uv = float2(0.5, 1.1);
        v.pos = mul(sprite[0].pos + float4(0, -Y * 0.1, 0, 0), viewproj);
        OutputStream.Append(v);

        v.uv = float2(1.0 - scale / 2, 0.5);
        v.pos = mul(sprite[0].pos + (float4(v.tangent, 0) * X) + float4(0, Y * 0.5, 0, 0), viewproj);
        OutputStream.Append(v);
        
        v.uv = float2(0.0 + scale / 2, 0.5);
        v.pos = mul(sprite[0].pos - (float4(v.tangent, 0) * X) + float4(0, Y * 0.5, 0, 0), viewproj);
        OutputStream.Append(v);

        v.uv = float2(0.5, -0.1);
        v.pos = mul(sprite[0].pos + float4(0, Y * 1.1, 0, 0), viewproj);
        OutputStream.Append(v);
    }
 
}



float4 psMain(GSOut vOut) : SV_TARGET
{
    clip(vOut.uv.y);

    const plant PLANT = plant_buffer[vOut.index];
    const sprite_material MAT = materials[PLANT.billboardMaterialIndex];

    float4 samp = 1;

    float4 albedo = textures.T[MAT.albedoTexture].Sample(gSmpLinearClamp, vOut.uv.xy);
    albedo.rgb *= MAT.albedoScale[0] * 2.f;
    float alpha = pow(albedo.a, MAT.alphaPow);
    clip(alpha - 0.5);
    alpha = smoothstep(0.3, 0.8, alpha);

    float Shadow = pow(shadow(vOut.worldPos, 0), 0.25); // Should realyl fo this in VS, just make sunlight zero

    float3 N = vOut.normal;
    if (MAT.normalTexture >= 0)
    {
        float3 normalTex = ((textures.T[MAT.normalTexture].Sample(gSmpLinearClamp, vOut.uv.xy).rgb) * 2.0) - 1.0;
        N = (normalTex.r * vOut.tangent) + (normalTex.g * vOut.binormal) + (normalTex.b * vOut.normal);
    }
    //N *= flipNormal;
    float ndoth = saturate(dot(N, normalize(sunDirection + vOut.eye)));
    float ndots = dot(N, sunDirection);

    float dappled = 1; // 1 - vOut.Shadow;

    // sunlight
    float3 color = vOut.diffuseLight * 3.14 * (saturate(ndots)) * albedo.rgb * dappled * Shadow;

    
    // environment cube light
    color += 0.539 * gEnv.SampleLevel(gSampler, N * float3(1, 1, -1), 0).rgb * albedo.rgb;// * pow(vOut.AmbietOcclusion, 0.3);

 // specular sunlight
    float RGH = MAT.roughness[0] + 0.001;
    float pw = 15.f / RGH;
    color += pow(ndoth, pw) * 1.6 * dappled * vOut.diffuseLight * (1 - RGH) * Shadow;

// translucent light    
    float3 TN = vOut.normal;
    float3 trans = (saturate(-ndots)) * saturate(dot(-sunDirection, vOut.eye)) * MAT.translucency * dappled;
    {
        if (MAT.translucencyTexture >= 0)
        {
            float t = textures.T[MAT.translucencyTexture].Sample(gSampler, vOut.uv.xy).r;
            trans *= pow(t, 2);
        }
    }
    color += trans * pow(albedo.rgb, 1.5) * 150 * vOut.diffuseLight * Shadow;

// Now for my atmospeher code --------------------------------------------------------------------------------------------------------
// FIXME per plant rather
	{
		//far one
        float3 atmosphereUV;
        atmosphereUV.xy = vOut.pos.xy / screenSize;
        atmosphereUV.z = log(length(vOut.worldPos.xyz - eye.xyz) / fog_far_Start) * fog_far_log_F + fog_far_one_over_k;
        atmosphereUV.z = max(0.01, atmosphereUV.z);
        atmosphereUV.z = min(0.99, atmosphereUV.z);
        color.rgb *= gAtmosphereOutscatter.Sample(gSmpLinearClamp, atmosphereUV).rgb;

        float4 inscatter = textureBicubic(atmosphereUV);
        color.rgb += inscatter.rgb;
    }


    // apply JHFAA to edges    
    if (alpha < 0.9)
    {
        float2 uv = vOut.pos.xy / screenSize; //        float2(2560, 1440);
        float3 prev = gHalfBuffer.Sample(gSmpLinearClamp, uv).rgb;
        color = lerp(prev, color, alpha);
    }

    samp.rgb = color;
    return samp;
}
