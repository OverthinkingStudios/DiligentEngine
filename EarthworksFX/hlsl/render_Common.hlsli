#include "terrain/gpuLights_defines.hlsli"



SamplerState gSmpPoint : register(s0);
SamplerState gSmpLinear : register(s1);
SamplerState gSmpAniso : register(s2);
SamplerState gSmpLinearClamp : register(s3);
SamplerComparisonState gSamplerDepth : register(s4);




// ibl and reflections ------------------------------------------------------------------------------------------------------------------------
Texture2D 	gPrevFrame : register(t8);
TextureCube gCube_Far : register(t9);
TextureCube gCube_Mid : register(t10);
TextureCube gCube_Close : register(t11);
TextureCube gEnv : register(t7);           // really needs to be replaced with the above




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

    float3  sunRightVector;      // used for volumetric shadow projection code
    float   padd2;

    float3  sunUpVector;
    float   padd3;

    // all the fog params
    float2 screenSize;
    float fog_far_Start;
    float fog_far_log_F;            //(k-1 / k) / log(far)		FIXME might be k-2 to make up for half pixel offsets
	
    float fog_far_one_over_k;       // 1.0 / k
    float fog_near_Start;
    float fog_near_log_F;           //(k-1 / k) / log(far)		FIXME might be k-2 to make up for half pixel offsets
    float fog_near_one_over_k;      // 1.0 / k
	
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

Texture2D gPreviousFrame : register(t23);


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

float4 vs_textureBicubic(float3 texCoords)
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

    
    float4 sample0 = gAtmosphereInscatter.SampleLevel(gSmpLinearClamp, float3(offset.xz, texCoords.z), 0);
    float4 sample1 = gAtmosphereInscatter.SampleLevel(gSmpLinearClamp, float3(offset.yz, texCoords.z), 0);
    float4 sample2 = gAtmosphereInscatter.SampleLevel(gSmpLinearClamp, float3(offset.xw, texCoords.z), 0);
    float4 sample3 = gAtmosphereInscatter.SampleLevel(gSmpLinearClamp, float3(offset.yw, texCoords.z), 0);


    float sx = s.x / (s.x + s.y);
    float sy = s.z / (s.z + s.w);

    return lerp(lerp(sample3, sample2, sx), lerp(sample1, sample0, sx), sy);
}



float4 sunLight(float3 posKm)
{
    float2 sunUV;
    float dX = dot(posKm.xz, normalize(sunDirection.xz));
    sunUV.x = saturate(0.5 - (dX / 1600.0f));
    sunUV.y = 1 - saturate(posKm.y / 100.0f);
    return SunInAtmosphere.SampleLevel(gSmpLinearClamp, sunUV, 0) * 0.07957747154594766788444188168626;
}



void JHFAA_alpha(inout float3 color, float alpha, float2 screen)
{
    if (alpha < 0.9)
    {
        screen /= screenSize;
        const float3 prev = gPreviousFrame.Sample(gSmpLinearClamp, screen).rgb;
        color = lerp(prev, color, alpha);
    }
}


void atmosphere(inout float3 color, float2 screen, float distance)
{
    float3 atmosphereUV;
    atmosphereUV.xy = screen / screenSize;
    atmosphereUV.z = clamp(log(distance / fog_far_Start) * fog_far_log_F + fog_far_one_over_k, 0.002, 0.99);
    color *= gAtmosphereOutscatter.Sample(gSmpLinearClamp, atmosphereUV).rgb;

    float4 inscatter = textureBicubic(atmosphereUV);
    color.rgb += inscatter.rgb;
}

void vs_atmosphere(inout float3 outscatter, inout float3 inscatter, float2 screen, float distance)
{
    float3 atmosphereUV;
    atmosphereUV.xy = screen;// / screenSize;
    atmosphereUV.z = clamp(log(distance / fog_far_Start) * fog_far_log_F + fog_far_one_over_k, 0.002, 0.99);
    outscatter = gAtmosphereOutscatter.SampleLevel(gSmpLinearClamp, atmosphereUV, 0).rgb;
    inscatter = vs_textureBicubic(atmosphereUV).rgb;
}
