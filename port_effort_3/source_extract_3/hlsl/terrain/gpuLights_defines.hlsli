 #pragma once



struct gpu_LIGHT
{
	float3 		position;
	float 		radius;
	
	float3 		direction;
	float 		slope;
	
	float3 		colour;
	float 		power;			// for pow function - also test if just cators fo 2 is faster and multiply out instead of pow(), then save int log(pow) - might well be good enough
	
	// index to shadow map
	// flatten for uniform center spot
};


//float4x4 	invMat;			// inverse like a view matrix translates a world position into light space

