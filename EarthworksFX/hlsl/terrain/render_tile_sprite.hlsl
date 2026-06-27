
#include "groundcover_defines.hlsli"
#include "groundcover_functions.hlsli"		
#include "vegetation_defines.hlsli"
#include "terrainDefines.hlsli"
#include "../render_Common.hlsli"



SamplerState gSampler;


//TextureCube gEnv : register(t1);
//Texture2D gPreviousFrame : register(t2);

Texture2D<float4> textures_T[4096];



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


    float gisOverlayStrength;
    int showGIS;
    float redStrength;
    float redScale;
    float4 gisBox;

    float redOffset;
    float3 padding;

};



struct GSOut
{
    float4 pos : SV_POSITION;

    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 binormal : BINORMAL;

    float3 diffuseLight : COLOR;

    float3 uv : TEXCOORD;
    float3 inscatter : TEXCOORD1;
    float3 outscatter : TEXCOORD2;
    float3 eye : TEXCOORD3;
    uint index : TEXCOORD4;
};


GSOut vsMain(uint vId : SV_VertexID, uint iId : SV_InstanceID)
{
    GSOut output = (GSOut)0;
    //return output;

    
    uint tileIDX = lu_Tile(tileLookup[iId]);
    uint numQuad = lu_Used(tileLookup[iId]);
    uint tileStart = lu_Index(tileLookup[iId], numQuadsPerTile, 64);
    uint blockID = vId;

    // FIXME just fill the rest of the 64 with NULL and render all much faster than this if
    if (blockID < numQuad)
    {
        uint newID = tileStart + blockID;
        instance_PLANT plant = instanceBuffer[newID];

        float3 P = unpack_pos(plant.xyz, tiles[tileIDX].origin, tiles[tileIDX].scale_1024);
        output.pos = float4(P, 1);
        
        

        
        output.uv.z = SCALE(plant.s_r_idx);
        output.index = PLANT_INDEX(plant.s_r_idx);

        output.diffuseLight = sunLight(P * 0.001).rgb * shadow(P, 0);

        // Now for my atmospeher code --------------------------------------------------------------------------------------------------------
        float3 atmosphereUV;
        float4 xfpos = mul(output.pos, viewproj);
        xfpos /= xfpos.w;
        atmosphereUV.xy = xfpos.xy;// / xfpos.w;// / screenSize;
        atmosphereUV.z = clamp(log(length(output.pos.xyz - eye.xyz) / fog_far_Start) * fog_far_log_F + fog_far_one_over_k, 0.01, 0.99);
        atmosphereUV.x = 0.5 + atmosphereUV.x * 0.5;
        atmosphereUV.y = (1 - atmosphereUV.y) * 0.5;
        output.outscatter = gAtmosphereOutscatter.SampleLevel(gSmpLinearClamp, atmosphereUV, 0).rgb;
        output.inscatter = gAtmosphereInscatter.SampleLevel(gSmpLinearClamp, atmosphereUV, 0).rgb;
        


        output.eye = normalize(P - eye.xyz);

        //output.pos.xyz -= output.eye * 0.2f;

        output.binormal = float3(0, 1, 0);
        output.tangent = normalize(cross(output.binormal, -output.eye.xyz));
        output.normal = cross(output.binormal, output.tangent);
    }
    return output;
}



[maxvertexcount(4)]
void gsMain(point GSOut sprite[1], inout TriangleStream<GSOut> OutputStream)
{
    GSOut v = sprite[0];
    const plant PLANT = plant_buffer[sprite[0].index];
    float X = PLANT.size.x * sprite[0].uv.z;
    float Y = PLANT.size.y * sprite[0].uv.z;
    float scale = 1 - 0.8;

    v.uv.xy = float2(0.5, 1.1);
    v.pos = mul(sprite[0].pos + float4(0, -Y * 0.1, 0, 0), viewproj);
    OutputStream.Append(v);

    v.uv.xy = float2(1.0 - scale / 2, 0.5);
    v.pos = mul(sprite[0].pos + (float4(v.tangent, 0) * X) + float4(0, Y * 0.5, 0, 0), viewproj);
    OutputStream.Append(v);

    v.uv.xy = float2(0.0 + scale / 2, 0.5);
    v.pos = mul(sprite[0].pos - (float4(v.tangent, 0) * X) + float4(0, Y * 0.5, 0, 0), viewproj);
    OutputStream.Append(v);

    v.uv.xy = float2(0.5, -0.1);
    v.pos = mul(sprite[0].pos + float4(0, Y * 1.1, 0, 0), viewproj);
    OutputStream.Append(v);    
}



float4 psMain(GSOut vOut) : SV_TARGET
{
    //return 1;
    clip(vOut.uv.y - 0.03);

//return 1;

    // it may be faster to pack billboard materials into its own buffer and have tighter reads here
    const sprite_material MAT = materials[ plant_buffer[vOut.index].billboardMaterialIndex ];

    float4 albedo = textures_T[MAT.albedoTexture].Sample(gSmpLinearClamp, vOut.uv.xy);
    
    float alpha = pow(albedo.a, MAT.alphaPow);
    //if (alpha < 0.5) return float4(1, 0, 0, 1);
    clip(alpha - 0.5);
    alpha = smoothstep(0.3, 0.8, alpha);
    albedo.rgb *= MAT.albedoScale[0] * 2.f;
    
    float3 N = vOut.normal;
    if (MAT.normalTexture >= 0)
    {
        float3 normalTex = ((textures_T[MAT.normalTexture].Sample(gSmpLinearClamp, vOut.uv.xy).rgb) * 2.0) - 1.0;
        N = (normalTex.r * vOut.tangent) + (normalTex.g * vOut.binormal) + (normalTex.b * vOut.normal);
    }
    
    float ndoth = saturate(dot(N, normalize(sunDirection + vOut.eye)));
    float ndots = dot(N, sunDirection);

    
    // sunlight
    float3 color = vOut.diffuseLight * 3.14 * (saturate(ndots)) * albedo.rgb;
    
    
    // environment cube light
    color += 0.539 * gEnv.SampleLevel(gSampler, N * float3(1, 1, -1), 0).rgb * albedo.rgb;// * pow(vOut.AmbietOcclusion, 0.3);
    
    // specular sunlight
   float RGH = MAT.roughness[0] + 0.001;
   float pw = 15.f / RGH;
   color += pow(ndoth, pw) * 0.6  * vOut.diffuseLight * (1 - RGH);
   
    // translucent light    
    float3 TN = vOut.normal;
    float3 trans = vOut.diffuseLight * 3.14 * (saturate(-ndots)) * MAT.translucency;
    {
        if (MAT.translucencyTexture >= 0)
        {
            float t = textures_T[MAT.translucencyTexture].Sample(gSampler, vOut.uv.xy).r;
            //trans *= t;
        }
    }
    float gray = dot(albedo.rgb, float3(0.229, 0.587, 0.114));
    color += trans * (gray * pow(albedo.rgb / gray, 1.6)) * vOut.diffuseLight * 2;


    
    // Apply atmopsphere
    color.rgb *= vOut.outscatter;
    color.rgb += vOut.inscatter;
       
    // apply JHFAA to edges    
    if (alpha < 0.9)
    {
        float3 prev = gPreviousFrame.Sample(gSmpLinearClamp, vOut.pos.xy / screenSize).rgb;
        color = lerp(prev, color, alpha);
    }
           
    return float4(color, 1);
}
