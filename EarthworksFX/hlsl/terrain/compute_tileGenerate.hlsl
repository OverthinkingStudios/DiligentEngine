
#include "groundcover_defines.hlsli"
#include "groundcover_functions.hlsli"		
#include "terrainDefines.hlsli"	


Texture2D<float> 	gHgt;
Texture2D<uint> 	gNoise;

Texture2D gEct1;
Texture2D gEct2;
Texture2D gEct3;
Texture2D gEct4;
Texture2D gEct5;

RWStructuredBuffer<gpuTile> 		tiles;
RWStructuredBuffer<instance_PLANT> 	quad_instance;
RWStructuredBuffer<GC_feedback>		feedback;


cbuffer gConstants		
{
	uint		tileIDX;
};



[numthreads(tile_cs_ThreadSize_Generate, tile_cs_ThreadSize_Generate, 1)]				// uses 8 and subtracts 1 fram dispatch to just generate exact pixels
void main(uint3 coord : SV_DispatchThreadId)
{
	coord += uint3(4,4,0);
	gpuTile tile = tiles[tileIDX];	

	if ((tile.lod == 8))
	{
		uint rndLookup = tile.X * 3278 + tile.Y * 7223 + coord.x * 256 + coord.y;
		uint3 C_rnd = (uint3(rndLookup >> 8, rndLookup, 0) + coord) % 256;
		uint rnd = gNoise.Load( C_rnd );
		
		
		float CLEAR = 1 - gEct1.Load( coord ).g;
		if (rnd < (uint)(14000 * CLEAR))
		{
			float height = gHgt.Load( coord );
			uint uHgt = (height - tile.origin.y) / tile.scale_1024;
			// same for ectmaps
		
			uint slot = 0;
			InterlockedAdd( tiles[tileIDX].numQuads, 1, slot );	
			feedback[0].plants_culled = slot;
			
			quad_instance[tileIDX * numQuadsPerTile + slot].xyz = pack_pos(coord.x, coord.y, uHgt, rnd);				// FIXME - redelik seker die is verkeerd -ek dink ek pak 2 extra sub pixel bits 
			quad_instance[tileIDX * numQuadsPerTile + slot].s_r_idx = pack_SRTI(true, rnd, tileIDX, 0);			//(1 << 31) + (child_idx << 11) + (0);
		}
		
	}

}
