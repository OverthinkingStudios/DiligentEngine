// PORT-REVIEW (step 6 salvage): copied verbatim from legacy/hlsl/terrain/
// render_Buildings_Far.hlsl (port-side shader, no original twin - the feature
// was commented out upstream). LightsCB / shadow() / sunLight() / atmosphere
// textures arrive through ../PBR.hlsli -> render_Common.hlsli.

#include "groundcover_defines.hlsli"
#include "terrainDefines.hlsli"
#include "terrainFunctions.hlsli"
#include "../PBR.hlsli"
#include "gpuLights_functions.hlsli"







cbuffer PerFrameCB
{
    float4x4 view;
    float4x4 viewproj;

    float3 eye;
    uint firstVertex;   // start of the chunk range being drawn (per-draw)
};


struct _buildingVertex
{
    float3 pos;
    uint material;
    float3 normal;
    float something;
    float2 uv;
    float2 somethignesle;
};

StructuredBuffer<_buildingVertex> vertexBuffer;


struct buildingFarVSOut
{
    float4 pos : SV_POSITION;
    float3 texCoords : TEXCOORD; // u, v, tileIdx
    float3 worldPos : TEXCOORD1;
    float3 eye : TEXCOORD2; // worldPos - cameraPos
    uint material : TEXCOORD3;
    float3 normal : TEXCOORD4;

};

buildingFarVSOut vsMain(uint vId : SV_VertexID, uint iId : SV_InstanceID)
{
    buildingFarVSOut output;

    // The CPU draws grid-cell ranges; the range start comes in through the CB
    // because SV_VertexID does NOT include StartVertexLocation on D3D (that
    // only arrived as SV_StartVertexLocation in SM6.8).
    _buildingVertex V = vertexBuffer[firstVertex + vId];
    
    output.worldPos = V.pos;
    output.eye = V.pos - eye;
    output.pos = mul(float4(V.pos, 1), viewproj);
    output.texCoords.xy = V.uv;
    output.material = V.material;
    output.normal = V.normal;
    /*
    float4 tmp = mul(float4(V.pos, 1), view);

    output.pos.xyz = normalize(tmp.xyz);
    float SGN = -sign(output.pos.z);
    output.pos.z = length(tmp.xyz) / 40000 * SGN;
    output.pos.y *= 2550.f / 1080.f;
        
    output.pos.w = 1;
    */
	
	return output;
}



float4 psMain(buildingFarVSOut vIn) : SV_TARGET0
{
    float3 colour = 0.35f;
    //colour = float3(0.1, 0.015, 0.01) * 0.1;
    if (vIn.normal.y > 0.1f)
    {
        colour = float3(0.2, 0.15, 0.15);
    }
	
	
    float Sh = pow(shadow(vIn.worldPos, 0), 0.5);
    float4 sunColor = sunLight(vIn.worldPos * 0.001);
    float3 ambient = sunColor.bgr * 0.05f;
    float Sn = saturate(dot(vIn.normal, -sunDirection));
    colour *= ambient + (Sn * Sh * sunColor.rgb); //    Sh * Sn * sunColour;
    //colour *= sunColor.rgb;
    //colour = sunDirection;

    
    
	// Atmosphere
	{
		//far one
		float3 atmosphereUV;
        atmosphereUV.xy = vIn.pos.xy / float2(4096, 2160);//        screenSize;
		atmosphereUV.z = log( length( vIn.eye.xyz ) / fog_far_Start ) * fog_far_log_F + fog_far_one_over_k;
        atmosphereUV.z = max(0.01, atmosphereUV.z);
        atmosphereUV.z = min(0.99, atmosphereUV.z);
		colour.rgb *= gAtmosphereOutscatter.Sample( gSmpLinearClamp, atmosphereUV ).rgb;
		colour.rgb += gAtmosphereInscatter.Sample( gSmpLinearClamp, atmosphereUV ).rgb;
	}
    
	return float4(colour, 1);
}









