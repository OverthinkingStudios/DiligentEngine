/*
	Calculate cross normals over a pixel - read one up, down, left and right
	this way the normal is lined up with the texture
	
	it messes up the edge pixels a little as they use pixsize*2 as well
	
	also at this point the full heightfield is known - also set min and max heights to the TILE struture for hgt compression later, especially plants
*/


#include "terrainDefines.hlsli"
#include "groundcover_defines.hlsli"


RWTexture2D<float> 		gInHgt;
RWTexture2D<float3> 	gOutNormals;
RWTexture2D<float4> 	gOutput;

RWStructuredBuffer<gpuTile>		tiles;		// also update minHgt_mm and maxHgt_mm



cbuffer gConstants
{
	float pixSize;			// .x pixsize			?? why not use float
	float3 padd;
};



[numthreads(tile_cs_ThreadSize, tile_cs_ThreadSize, 1)]
void main(uint2 crd : SV_DispatchThreadId)
{

	uint2 crd_clamped = clamp(crd, 1, tile_numPixels - 2);
	float dx = gInHgt[crd_clamped + int2(-1, 0)].r - gInHgt[crd_clamped + int2(1, 0)].r;
	float dy = gInHgt[crd_clamped + int2(0, -1)].r - gInHgt[crd_clamped + int2(0, 1)].r;
	
	float3 n = normalize( float3(dx, 2.0 * pixSize, dy) );
	gOutNormals[crd] = n * 0.5 + 0.5;
	
	float3 shade = saturate(dot(n, normalize(float3(0.71, 0.2, 0.71 )))) * 0.1;
	//gOutput[crd] = float4(shade,1 );
#if defined(COMPUTE_DEBUG_OUTPUT)
    gOutput[crd] = float4(n * 0.5 + 0.5, 1) * 0.2 - float4(0, 0.2, 0, 1);
#endif
}
