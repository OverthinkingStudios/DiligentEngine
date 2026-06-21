
#include "groundcover_defines.hlsli"
#include "terrainDefines.hlsli"
#include "terrainFunctions.hlsli"
#include "../PBR.hlsli"
#include "gpuLights_functions.hlsli"







cbuffer PerFrameCB
{
    float4x4 viewproj;

    float3 eye;
    float padd1;
	
	// volume fog parameters
	float2 	screenSize;
	float 	fog_far_Start;
	float	fog_far_log_F;			//(k-1 / k) / log(far)		// FIXME might be k-2 to make up for half pixel offsets
	
	float 	fog_far_one_over_k;		// 1.0 / k
	float 	fog_near_Start;
	float	fog_near_log_F;			//(k-1 / k) / log(far)		// FIXME might be k-2 to make up for half pixel offsets
	float 	fog_near_one_over_k;		// 1.0 / k
	
};


struct _gliderwingVertex
{
    float3 pos;
    uint material;
    float3 normal;
    float something;
    float2 uv;
    float2 somethignesle;
};

StructuredBuffer<_gliderwingVertex> vertexBuffer;


struct gliderwingVSOut
{
    float4 pos : SV_POSITION;
    float3 texCoords : TEXCOORD; // u, v, tileIdx
    float3 worldPos : TEXCOORD1;
    float3 eye : TEXCOORD2; // worldPos - cameraPos
    uint material : TEXCOORD3;
    float3 normal : TEXCOORD4;

};

gliderwingVSOut vsMain(uint vId : SV_VertexID, uint iId : SV_InstanceID)
{
    gliderwingVSOut output;

    
    _gliderwingVertex V = vertexBuffer[iId * 3 + vId];
    
    output.worldPos = V.pos;
    output.eye = V.pos - eye;
    output.pos = mul(float4(V.pos, 1), viewproj);
    output.texCoords.xy = V.uv;
    output.material = V.material;
    output.normal = V.normal;
	
	return output;
}


float4 sunLight(float3 posKm)
{
    float2 sunUV;
    float dX = dot(posKm.xz, normalize(sunDirection.xz));
    sunUV.x = saturate(0.5 - (dX / 1600.0f));
    sunUV.y = 1 - saturate(posKm.y / 100.0f);
    return SunInAtmosphere.SampleLevel(gSmpLinearClamp, sunUV, 0) * 0.07957747154594766788444188168626;
}


float4 psMain(gliderwingVSOut vIn) : SV_TARGET0
{
    float3 colour = 0.6f;
    float3 diffuse = 0.6f;
    //colour = float3(0.1, 0.015, 0.01) * 0.1;
    

    if (vIn.texCoords.y > 0.47 && vIn.texCoords.y < 0.60)
    {
        diffuse = float3(0.2, 0.3, 0.5) * 0.5;

        if (vIn.texCoords.y > 0.42 && vIn.texCoords.y < 0.47)
        {
            //diffuse = float3(0.15, 0.15, 0.15);
        }
    }
    else
    {
        if (vIn.texCoords.x > 0.45 && vIn.texCoords.x < 0.55)
        {
            //diffuse = float3(0.6, 0.1, 0.1);
        }
    }


    float dv = 0.2 * saturate((vIn.texCoords.x - 0.7) / 0.3);

    if (vIn.texCoords.y > 0.6 && vIn.texCoords.y < 0.8 + pow(dv, 1.5))
    {
        diffuse = float3(0.5, 0.3, 0.1);
    }

    if (vIn.texCoords.y > 0.2 && vIn.texCoords.y < 0.4)
    {
        diffuse = float3(0.5, 0.3, 0.1);
    }
    
    if ((vIn.texCoords.x > 0.86 && vIn.texCoords.x < 0.88) || (vIn.texCoords.x < 0.14 && vIn.texCoords.x > 0.12))
    {
        diffuse = float3(0.5, 0.2, 0.1);
    }
    
    
    float rib = frac(vIn.texCoords.x * 50);
    if (rib > 0.95)
    {
        //diffuse *= 0.9;
    }
    
	
	
    float Sh = pow(shadow(vIn.worldPos, 0), 0.5);
    float4 sunColor = sunLight(vIn.worldPos * 0.001);
    float3 ambient = sunColor.bgr * 0.28f;
    float Sn = saturate(dot(vIn.normal, -sunDirection));
    colour = diffuse * (ambient + (Sn * Sh * sunColor.rgb));
    //colour *= sunColor.rgb;
    //colour = sunDirection;

    // passtrhough
    float P = saturate(dot(-sunDirection, normalize(vIn.eye)));
    P = pow(P, 8);
    colour += diffuse * (P * sunColor.rgb) * 0.4f;

    
    
	// Atmosphere
	{
		//far one
		float3 atmosphereUV;
        atmosphereUV.xy = vIn.pos.xy / screenSize;
		atmosphereUV.z = log( length( vIn.eye.xyz ) / fog_far_Start ) * fog_far_log_F + fog_far_one_over_k;
        atmosphereUV.z = max(0.01, atmosphereUV.z);
        atmosphereUV.z = min(0.99, atmosphereUV.z);
		colour.rgb *= gAtmosphereOutscatter.Sample( gSmpLinearClamp, atmosphereUV ).rgb;
		colour.rgb += gAtmosphereInscatter.Sample( gSmpLinearClamp, atmosphereUV ).rgb;
	}
    
	return float4(colour, 1);
}









