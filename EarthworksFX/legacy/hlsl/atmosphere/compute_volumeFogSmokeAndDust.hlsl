#include "CSVolumeFogCommon.hlsli"


RWTexture3D<float4> gInOutscatter : register(u0);
 


float mist_exp(float height, float rootHeight, float mistHeight, float density) {
	return saturate(exp(-(height - rootHeight) / mistHeight)) * density;
}

/*
	1)	(32, 1, 1) write is faster but .Load is slower, they always seem to cancel
	2)	On DirectX rgba16f will be faster due to the copy and a lot more so in cockpit when we dont have to calculate all rays - On Vulcan BC6H should be faster
	3)	The current version generates all the densities here in one uber shader since its mostly bandwidth limited
		- It could be paired with a low resolution pre-pass to pre compute some of it
		- It should be paired with a complete tile based pre-pass that generates this call as ComputeIndirect, and can block tiles hidden by the cockpit
*/

[numthreads(8, 8, 1)] 
void main(uint3 coord
								: SV_DispatchThreadId) {
	float4 InOut = float4(0, 0, 0, 1);
	float dSum = 0.0f;
	float3 eye_dir = normalize(eye_direction + (dx * coord.x) + (dy * coord.y));
	float depth = sliceZero;

	for (uint i = 0; i < numSlices; i++) {
		coord.z = i;
		float step = depth * sliceStep;
		float3 pos = eye_position + eye_dir * (depth + (step / 2));	  // simplify with a multiplier
		depth *= sliceMultiplier;

		float E = InOut.a; // temp save old value
		float density = 0;

		float f_time = 0.0f;  // FIXME pass in
		float2 uv = frac(pos.xz / 700.0 + float2(1, 0) * f_time * 0.04);
		float2 uvb = frac(pos.xz / 300.0 + float2(1, 0) * f_time * 0.04);
		float mip = i / 60.0;
		float N = gNoiseA.SampleLevel(linearSampler, uv, mip).r;
		float N2 = gNoiseB.SampleLevel(linearSampler, uvb, mip).r;

		float dH = abs(eye_dir.y * step) * 2.0;
		//density = mist_exp(pos.y, 15 + 35 * N, 1.0 + dH, 0.01 - 0.02 * N2);
		density = 0;
		//mist_exp(pos.y, 0, fogAltitude.x, fogDensity.x);
		
		for (int i = 0; i < numFogVolumes; i++) {
			float3 uvw = mul(float4(pos + eye_dir * step / 16, 1), volumes[i].world).xyz + 0.5;
			if (!any(uvw - saturate(uvw))) {
				float4 fogValue = fogDensities[i].SampleLevel(linearSampler, uvw.zx, 0);
				
				if (uvw.y < fogValue.r)			density += fogValue.g * 10; 
			}
		}
		dSum += density * step;

		float3 uv1 = float3(coord.x / 256.0f, coord.y / 128.0f, coord.z / 256.0); // FIXME this scale conversion has to be passed in, this is just gInOutscatter.size
		float3 lights = gLightVolume.SampleLevel(linearSampler, uv1, 0).rgb;

		InOut.rgb += lights * density * E * step;
		InOut.a = 1.0 - exp(-dSum);		// flipping this allows a null texture to be valid
		
		gInOutscatter[coord] = InOut;
	}
}






/*
  // Optional code for Bicubic upsample of lights, looks really good but probably not worth it
  // Makes this function slower, but allowes lower resolution ligts calculation
  // The total number of lights may determine what is best

  static const float4 X[8] =
  {
	  {-0.033983, 0.887831, 0.963135, 0.036865},
	  {-0.115544, 0.710406, 0.860107, 0.139893},
	  {-0.211024, 0.564182, 0.727783, 0.272217},
	  {-0.317728, 0.434970, 0.577881, 0.422119},
	  {-0.434970, 0.317728, 0.422119, 0.577881},
	  {-0.564182, 0.211024, 0.272217, 0.727783},
	  {-0.710406, 0.115544, 0.139893, 0.860107},
	  {-0.887831, 0.033983, 0.036865, 0.963135}
  };

  uint fX = (coord.x + 4) % 8;
  uint fY = (coord.y + 4) % 8;
  float cX = coord.x + 4;
  float cY = coord.y + 4;

  float3 sc = float3( 1.0 / 256.0, 1.0/128.0, i / 128.0 / 4.0);		// check as well ??? might need an offset in Z

  float3 uv1 = float3( cX + X[fX].x*8.0, cY + X[fY].x*8.0, 1) * sc;
  float3 uv2 = float3( cX + X[fX].y*8.0, cY + X[fY].x*8.0, 1) * sc;
  float3 uv3 = float3( cX + X[fX].x*8.0, cY + X[fY].y*8.0, 1) * sc;
  float3 uv4 = float3( cX + X[fX].y*8.0, cY + X[fY].y*8.0, 1) * sc;

  float3 lights = gLightValues.SampleLevel( gSampler, uv1, 0)  * X[fX].z  * X[fY].z +
				  gLightValues.SampleLevel( gSampler, uv2, 0)  * X[fX].w  * X[fY].z +
				  gLightValues.SampleLevel( gSampler, uv3, 0)  * X[fX].z  * X[fY].w +
				  gLightValues.SampleLevel( gSampler, uv4, 0)  * X[fX].w  * X[fY].w ;		// 0.3ms on this read alone rgba16f
*/