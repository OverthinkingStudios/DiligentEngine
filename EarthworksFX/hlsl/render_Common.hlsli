#include "terrain/gpuLights_defines.hlsli"


SamplerState gSmpPoint : register(s0);
SamplerState gSmpLinear : register(s1);
SamplerState gSmpAniso : register(s2);
SamplerState gSmpLinearClamp : register(s3);




// ibl and reflections ------------------------------------------------------------------------------------------------------------------------
Texture2D 	gPrevFrame : register(t8);
TextureCube gCube_Far : register(t9);
TextureCube gCube_Mid : register(t10);
TextureCube gCube_Close : register(t11);


/*
	cube sample function
	# of cubes to use
	matrix and perspective offset for each cube
	reproject, sample and sum
*/


// lights ----------------------------------------------------------------------------------------------------------------------------------------------------------


cbuffer LightsCB : register(b2)
{
	float3 	sunDirection;
	int		numLights;
	
	float3 	sunColour;			// unless this becomes a texture lookup
	float 	padd;

    float3 sunRightVector;    // used for volumetric shadow projection code
    float padd2;

    float3 sunUpVector;
    float padd3;
	
	gpu_LIGHT Lights[128];		// decent upper limit
};


Texture3D gAtmosphereInscatter : register(t12);
Texture3D gAtmosphereOutscatter : register(t13);
Texture3D gSmokeAndDustInscatter : register(t14);
Texture3D gSmokeAndDustOutscatter : register(t15);

Texture2D SunInAtmosphere : register(t16);

Texture2D cloudShadow : register(t20);
Texture2D terrainShadow : register(t21);

Texture2D gAtmosphereInscatter_Sky : register(t22);


float shadow(float3 pos, float step)
{
    float2 uv = saturate((pos.xz / (4096 * 9.765625)) + 0.5);
    float2 params = terrainShadow.SampleLevel(gSmpLinear, uv, 0).rg;
    float h = (pos.y + params.g * 0.3 - params.r) / (params.g); //    (pos.y - params.r) / params.g;
    h = (pos.y - params.r) / (params.g / 30); //    (pos.y - params.r) / params.g;
    return pow(saturate(h),1.5);
}

/*
void float4 applyVolumeFog(inout float4 colour)
{
	//far one
	float3 atmosphereUV;
	atmosphereUV.xy = vIn.pos.xy / screenSize;
	atmosphereUV.z = log( length( vIn.eye.xyz ) / fog_far_Start) * fog_far_log_F + fog_far_one_over_k; 
	colour.rgb *= gAtmosphereOutscatter.Sample( gSmpLinearClamp, atmosphereUV ).rgb;
	colour.rgb += gAtmosphereInscatter.Sample( gSmpLinearClamp, atmosphereUV ).rgb;

	
	atmosphereUV.z = log( length(vIn.eye.xyz) / fog_near_Start) * fog_near_log_F + fog_near_one_over_k;   
	float4 FOG = gSmokeAndDustInscatter.Sample( gSmpLinearClamp, atmosphereUV );
	colour.rgb *= FOG.a;
	colour.rgb += FOG.rgb;
}
*/




// terrain ------------------------------------------------------------------------------------------------------------------------------------
struct terrainVSOut
{
    float4 pos : SV_POSITION;
    float3 texCoords : TEXCOORD;		// u, v, tileIdx
	float3 worldPos : TEXCOORD1;
	float3 eye : TEXCOORD2;				// worldPos - cameraPos
};
