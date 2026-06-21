
#include "compute_volumeFog.hlsli"

RWTexture2D<float4> gResult : register(u0);

// all the scatter functions relate to km so run this function in km as well
float4 sumOpticalDepthToSpaceKm(float2 pos, float2 dir) {
	float altitude = 0;
	float step = 0.5;	// half km first step
	float4 Optical = 0;

	// while 300km sounds excissive - we must ensure here that this volume completely encomapsses the texture. Otherwise pixels outside the atmosphere will be fully lit even at night
	while (altitude < 300) {
		float R = pos.y + EarthR;
		altitude = sqrt((R * R) + (pos.x * pos.x)) - EarthR;
		Optical += opticalDepth(max(0, altitude)) * step;

		pos += step * dir;
		step *= 1.3;
	}

	return Optical;
}



//FIXME - decide what we do with globalExposure, might be better tobake it in here
// Its 512x256 hardcoded in Renderer.h     sunAtmosphereTextureSize = float2(512, 256);
[numthreads(32, 32, 1)] void main(uint2 coord
								  : SV_DispatchThreadId) {

	if (!any(coord)) {
		
		/*	The topleft pixel, stores ambient colour of the sky above the clouds, instead of sunlight
			Reads the sunlight from the previous frame, and does an over simplistic calculation that is good enough

			This is quite a rough hack, and not nearly as good as solving a low resolution hemisphere of values, but see how far we can get with it
			*/
		float3 inscatter = 0;
		float3 outcatter = 0;
		for (int y= 254; y >=0; y--) {								  // to 230 so dont go down to ground level - bit of a guess, seems to work
			float H = 100 * (1 - (y / 256.0 + 0.001));
			//float3 sunlight = gResult[uint2(256, y)].rgb * 0.95 + gResult[uint2(300, y)].rgb * 0.05 + gResult[uint2(400, y)].rgb * 0.005;			  
			//float3 sunlight = (gResult[uint2(256, y)].rgb + gResult[uint2(128, y)].rgb + gResult[uint2(384, y)].rgb) / 3;			  
			float3 sunlight = gResult[uint2(256, y)].rgb;			  
			float4 opticaldepth = opticalDepth(max(0, H)) * 0.39;	  // 100/256 = 0.39 stepsize

			float VoL = -sun_direction.y;
			float phaseRayleigh = 0.785398 * (1.2 + 0.8 * VoL * VoL + 0.5 * VoL);

			outcatter += (Rayleigh_scattering * opticaldepth.x) + (Haze_scattering * opticaldepth.y) + (Fog_scattering * opticaldepth.z);
			float3 ext = exp(-outcatter);
			inscatter += ext * (Rayleigh_scattering * opticaldepth.x * phaseRayleigh * sunlight) / 12;
			//inscatter += ext * (Haze_scattering * opticaldepth.y * sunlight);
			//inscatter += ext * (Fog_scattering * opticaldepth.z * sunlight);
		}
		gResult[coord] = float4(inscatter, 0);
	} else {

		float2 pos;
		pos.x = 1600 * (coord.x / 512.0) - 800;
		pos.y = 100 * (1 - (coord.y / 256.0 + 0.001));

		float4 optR = sumOpticalDepthToSpaceKm(pos, normalize(float2(length(sun_direction.xz), -sun_direction.y + refraction.x)));
		float4 optG = sumOpticalDepthToSpaceKm(pos, normalize(float2(length(sun_direction.xz), -sun_direction.y + refraction.y)));
		float4 optB = sumOpticalDepthToSpaceKm(pos, normalize(float2(length(sun_direction.xz), -sun_direction.y + refraction.z)));
		float optSum = (optG.x + optG.y + optG.z) * Haze_extinction.g;

		float3 Rayleigh = float3(optR.x, optG.x, optB.x) * Rayleigh_extinction;
		float3 Haze = float3(optR.y, optG.y, optB.y) * Haze_extinction;
		float3 Fog = float3(optR.z, optG.z, optB.z) * Fog_extinction;
		float3 Ozone = optG.w * ozone_Colour * 0.001;

        float3 Transmittance = exp(-(Rayleigh + Haze + Fog + Ozone)) * sun_Luminance * sunColourBeforeOzone;

        gResult[coord] = float4(Transmittance * globalExposure, optSum); // scale here for globalExposure
    }
}
