//#include "../ksShaders/ksCommon.hlsli"	  // this is for lights - replace soon
#include "CSVolumeFogCommon.hlsli"

RWTexture3D<float3> gResult : register(u0);



/*
	FIXME - 90% of aliasing is in here
	when radius becomes big, I have to smooth out the value  dot( normalize(-eye_dir), normalize(lightVec))
	That is because lightVec is a bunch of values not just one
	and the pow(5) vastly exagerates that
	I realy realy have to look at the maths acurately here
*/
float3 calclight(volumeFogLight L, float3 pos, float radius, float3 eye_dir) {
	float3 lightVec = pos - L.position;
	float R = length(lightVec);
	float3 p_R = lightVec + L.direction * radius * 0;	// FIXME this shof backwards causes an erro in th4e phase functio - IT IS VERY SIGNIFICANT
	float3 scaleDir = normalize(p_R);
	float f = dot(L.direction, scaleDir);

	// remove this for now, add back if we have aliasing problems
	//float f0 = saturate((dot(lightDir, lightVec) + 50) / (50)); // not sure this helps much inscatter is the real issue

	float sqr = saturate(radius / (R * R)) / radius;
	float lightStrenth = pow(saturate(f), 15) * sqr;	  // FIXME quite wrong too much falloff

	float2 pzUV = float2(0.0f, 1 - ((dot(normalize(-eye_dir), normalize(lightVec)))*0.5 + 0.5));
	float phaseFunction = hazePhaseFunction.SampleLevel(linearSampler, pzUV, 0).r;

	return lightStrenth * phaseFunction * L.colour;
}



//http://www.philiplaven.com/mieplot.htm

/*	- Really look if sunlight should be here at all
	- If we add this to the far volume,then it should defenitely not be
	- The near fog - I am tempted to do the sun/shadow calculation in the particle system that drives it
*/
[numthreads(32, 1, 1)] 
void main(uint3 coord : SV_DispatchThreadId) {
	float3 eye_dir = normalize(eye_direction + (dx * coord.x) + (dy * coord.y));
	float depth = sliceZero;

	float cosTheta = dot(normalize(eye_dir), -sun_direction.xyz);
	float g = 0.4;
	float g_2 = g * g;
	float sunPhaseFog = (1 - g_2) / pow(1 + g_2 - 2 * g * cosTheta, 1.5) / 12.57;
	// FIXME move away from henyey greenstein and use lookup

	for (uint i = 0; i < numSlices; i++) {
		float step = depth * sliceStep;
		float3 pos = eye_position + eye_dir * (depth + (step / 2));
		depth *= sliceMultiplier;

		float3 LightSum = float3(0, 0, 0);
		
		for (int L = 0; L < numFogLights; L++) {
			LightSum += calclight(volumeFogLights[L], pos, step * 2, eye_dir);
		}
		
		LightSum += envMap.SampleLevel(linearSampler, -sun_direction.xyz, 5).rgb; // maybe also do back position although that gets very low

		// sunlight ---------------------------------------------------------------------------------
		// FIXME lookat far fog and use that mapping for uv instead
		//float2 cloudmapUV = rainmap_uv_from_position(pos);
		//float cloudShadow = 1.0 - CloudMap.SampleLevel(clampSampler, cloudmapUV, 0).r;

		// ??? Its a real question if sunlight should be added to this
		//LightSum += sunColour * sunPhaseFog;
		// FIXME USE correct lookup here
		// Or better still DO NOT DO SUNLIGTH HERE

		coord.z = i;
		gResult[coord] = LightSum;
	}
}