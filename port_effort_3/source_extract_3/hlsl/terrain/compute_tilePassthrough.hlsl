
#include "groundcover_defines.hlsli"
#include "groundcover_functions.hlsli"	
#include "terrainDefines.hlsli"
#include "vegetation_defines.hlsli"
			


Texture2D<float> 	gHgt;
Texture2D<uint> 	gNoise;





RWStructuredBuffer<instance_PLANT> 		quad_instance;			// input / output
RWStructuredBuffer<instance_PLANT> 		plant_i;			// output



RWStructuredBuffer<tileLookupStruct>	tileLookup;				// output
RWStructuredBuffer<gpuTile> 			tiles;



//StructuredBuffer<tileExtract>			tileInfo;				// FIXME DOES NTO EXIST ANY MOR EMAKE SURE WE GET THE INDEX
RWStructuredBuffer<GC_feedback>			feedback;

// new
StructuredBuffer<plant> plant_buffer;



cbuffer gConstants			// ??? wonder if this just works here, then we can skip structured buffer for it
{
    uint parent_index;
	uint child_index;
	uint dX;
	uint dY;
};




[numthreads(256, 1, 1)]			
void main(uint dispatchId : SV_DispatchThreadId)
{
	gpuTile tile = tiles[parent_index];

    //float OH = gHgt[uint2(128, 128)].r - (tile.scale_1024 * 2048);	// Its corner origin rather than middle
    //tile.origin.y = OH;
    

	if(dispatchId < tile.numQuads )
	{
		gpuTile tileC = tiles[child_index];
		
		uint XYZ = quad_instance[parent_index * numQuadsPerTile + dispatchId].xyz;
		uint SRTI = quad_instance[parent_index * numQuadsPerTile + dispatchId].s_r_idx;
		//uint cx = XYZ >> 31;
		//uint cy = (XYZ >> 21) & 0x1;

        const float plantY = plant_buffer[PLANT_INDEX(SRTI)].size.y * SCALE(SRTI);;

        

        uint x10 = ((XYZ >> 22) & 0x3ff);
        uint y10 = ((XYZ >> 12) & 0x3ff);   // 10 bit values

        uint cx = 0;
        uint cy = 0;
        if (x10 >= 496) cx = 1;
        if (y10 >= 496) cy = 1;

		if ((cx == dX) && (cy == dY))	//write cleaner		XOR
		{
			uint x = x10 - (cx * 496);	// remaining 9 bits
			uint y = y10 - (cy * 496);
			

			
			// noise only
			uint x_idx = (tile.lod * tile.X * 23 + x);
			uint y_idx = (tile.lod * tile.Y * 13 + y);
			uint rnd = gNoise.Load( int3(x_idx & 0xff, y_idx & 0xff, 0) );

            
			float height = gHgt[int2( (x>>1) + 4, (y>>1) + 4)].r;
            float OH = gHgt[uint2(128, 128)].r - (tileC.scale_1024 * 2048);	// Its corner origin rather than middle
			uint uHgt = (height - OH) / tileC.scale_1024;
			
			// Needs to scale with HFov as well tanV ?
            float FACTOR = plantY / tile.scale_1024 / 2.0f; // FIXME we need plant sizes in the GPU ecotope desc
			
			if (FACTOR > 15.0)	
			{
				// *** its large pack into the plant structure
				uint slot = 0;
				InterlockedAdd( tiles[child_index].numPlants, 1, slot );
                
                plant_i[child_index * numPlantsPerTile + slot].xyz = repack_pos(x, y, uHgt, rnd);
                plant_i[child_index * numPlantsPerTile + slot].s_r_idx = repack_SRTI(SRTI, child_index);
            }
			else
			{
				uint slot = 0;
                InterlockedAdd(tiles[child_index].numQuads, 1, slot);                
 				
				quad_instance[child_index * numQuadsPerTile + slot].xyz =  repack_pos(x, y, uHgt, rnd);
				quad_instance[child_index * numQuadsPerTile + slot].s_r_idx = repack_SRTI( SRTI, child_index);
			}
		}
	}
}
