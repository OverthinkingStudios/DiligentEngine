
#include "compute_volumeFog.hlsli"

RWTexture3D<float3> gInscatter : register(u0);
RWTexture2D<float3> gInscatter_cloudBase : register(u1);
RWTexture2D<float3> gInscatter_sky : register(u2);

RWTexture3D<float3> gOutscatter : register(u3);
RWTexture2D<float3> gOutscatter_cloudBase : register(u4);
RWTexture2D<float3> gOutscatter_sky : register(u5);

void write(float3 inscatter, float3 outscatter, uint3 coord) {
	gInscatter[coord] = inscatter;
	gOutscatter[coord] = outscatter;
}

// not quite working but also rework this to return somewthign that I dont have to sale again, i.e. scale by 1/pi from the outset
float acosFast(float inX) {
	float x1 = abs(inX);
	float x2 = x1 * x1;
	float x3 = x2 * x1;
	float s;

	s = -0.2121144f * x1 + 1.5707288f;
	s = 0.0742610f * x2 + s;
	s = -0.0187293f * x3 + s;
	s = sqrt(1.0f - x1) * s;

	// acos function mirroring
	// check per platform if compiles to a selector - no branch neeeded
	// I dont have to chekc because we are guaranteed to be on the 0-180 range
	return s;
	//return inX >= 0.0f ? s : fsl_PI - s;
}

float shadow(float3 pos, float step)
{
    float2 uv = saturate((pos.xz / (4096 * 9.765625)) + 0.5);
    float2 params = terrainShadow.SampleLevel(linearSampler, uv, 0).rg;
    //float h = (pos.y + params.g * .5 - params.r) / (params.g); //    (pos.y - params.r) / params.g;
    float h = (pos.y + params.g * 0.05 - params.r) / (params.g); //    (pos.y - params.r) / params.g;
    return saturate(h * 1);

}

/*	FIXME
	The first slice si WRONG, but we have to chnage the way we do the lookup first to fix this
	It ignores all teh fog in front of that first slice
	better plan to add that in, and then fade to 0 is the value is in front of that slice
	*/

// This needs significantly more testing, smaller seems faster even with worse ocupancy, but may better overlap other work, i.e. shadow rendering
[numthreads(8, 8, 1)] void main(uint3 coord
								: SV_DispatchThreadId) {
	float3 inScatter = 0;
	float3 outScatter = 1;
	float3 newIn = 0;
	float3 newOut = 0;

	float3 direction;
	if (parabolicProjection) {
		direction.xz = (coord.xy / parabolicMapHalfSize) - 1;
		direction.y = 0.5 - 0.5 * (direction.x * direction.x + direction.z * direction.z);
		direction = normalize(direction);
	} else {
		direction = normalize(eye_direction + (dx * coord.x) + (dy * coord.y));
	}
    /*
    float3 eyeCenter = normalize(eye_direction + (dx * 256) + (dy * 128));
    if (coord.y < 256)
    {
    
        float thetaX = ((float) coord.x - 256) / 160.0f;
        float thetaZ = ((float) coord.y - 128) / 160.0f;
        direction = normalize(eyeCenter * cos(thetaX) * cos(thetaZ) + (normalize(dx) * sin(thetaX)) + (normalize(dy) * sin(thetaZ)));
    }
    */
	
	float depth = sliceZero;

    float VoL = dot(direction, -sun_direction);
    float phaseRayleigh = 0.785398 * (1.2 + 0.8 * VoL * VoL + 0.5 * VoL);
	float2 phaseUV = sqrt(acos(VoL) * 0.31830988618379);
    phaseUV.y = min(phaseUV.y, 0.99f);

	float3 IBLdir = direction;
	IBLdir.z *= -1;
	float3 IBL = 0;
	IBL += envMap.SampleLevel(linearSampler, -IBLdir, 6).rgb * 0.3; // back
	IBL += envMap.SampleLevel(linearSampler, IBLdir, 5).rgb * 0.6;	// forward wide
	IBL += envMap.SampleLevel(linearSampler, IBLdir, 2).rgb * 0.1;	// forward narrow1
	IBL *= tmp_IBL_scale;

    IBL = float3(0.05, 0.07, 0.1) * 0.4;
    

	float cloudBase_km = cloudBase / 1000;

	// start at sliceZero and continue up to the cloudbase, or slice depth-2  ------------------------------------
	uint SLICE = 0;
	float R = EarthR + 0.1;
	while ((SLICE < numSlices) && R < (EarthR + cloudBase_km) && (R > EarthR))
    {
        float step = depth * sliceStep;
        float3 pos = eye_position + direction * (depth + (step / 2));
        float S = shadow(pos, step * 5);
        if (SLICE == 0) S = 0;  // No sun light in first slice ever
        calculateStep(direction, phaseRayleigh, phaseUV, IBL, S, depth, newIn, newOut, R);
		acumulateFog(inScatter, outScatter, newIn, newOut);
		coord.z = SLICE++;

        float2 uv = saturate((pos.xz / (4096 * 9.765625)) + 0.5);
        //inScatter.rg = uv;
		write(inScatter, outScatter, coord);
	}

	// if we reach the cloudbase first, just repeat this value till slice depth-2  ------------------------------
	while (SLICE < numSlices) {
		coord.z = SLICE++;
		write(inScatter, outScatter, coord);
        gInscatter[coord] = float3(0, 0, 1); //        SLICE / numSlices;
    }

	// now step untill we reach the cloudbase and write into single slice ---------------------------------------
    int cnt = 150;
	while (cnt > 0 && R < (EarthR + cloudBase_km) && (R > EarthR)) {
		calculateStep(direction, phaseRayleigh, phaseUV, IBL, 1, depth, newIn, newOut, R);
		acumulateFog(inScatter, outScatter, newIn, newOut);
        cnt--;
    }
	gInscatter_cloudBase[coord.xy] = inScatter;
	gOutscatter_cloudBase[coord.xy] = outScatter;

	// now step from the cloudbase to space and write into last slice -------------------------------------------
    cnt = 150;
	while (cnt > 0 && R < (EarthR + 100.0) && (R > EarthR)) {
		calculateStep(direction, phaseRayleigh, phaseUV, IBL, 1, depth, newIn, newOut, R);
		acumulateFog(inScatter, outScatter, newIn, newOut);
        cnt--;
    }
  
    gInscatter_sky[coord.xy] = inScatter;
	gOutscatter_sky[coord.xy] = outScatter;

    /*
    for (int i = 0; i < 128; i++)
    {
        coord.z = i;
        gInscatter[coord] = 0; //
    }
    for (int i = 0; i < 128; i++)
    {
        coord.z = i;
        gInscatter[coord] = float3(0, 0, i);
        if (i < 90)     gInscatter[coord] = float3(0, i, 0);
        if (i < 50)     gInscatter[coord] = float3(i, 0, 0);

            
        //if (coord.z == coord.x)
        if (coord.x < 100)
        {
        //    gInscatter[coord] = 1; //
        }
    }
*/
}
