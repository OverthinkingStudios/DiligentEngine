
#include "groundcover_defines.hlsli"
#include "terrainDefines.hlsli"
				


RWStructuredBuffer<tileLookupStruct>	tileLookup;				// output
RWStructuredBuffer<tileLookupStruct>	plantLookup;			// output
RWStructuredBuffer<tileLookupStruct>	terrainLookup;			// output

RWStructuredBuffer<gpuTile> 			tiles;
StructuredBuffer<uint> 					visibility;

RWStructuredBuffer<GC_feedback>			feedback;

RWStructuredBuffer<t_DrawArguments> 	DrawArgs_Quads;
RWStructuredBuffer<t_DrawArguments> 	DrawArgs_Plants;
RWStructuredBuffer<t_DrawArguments> 	DrawArgs_Terrain;
RWStructuredBuffer<t_DispatchArguments> DispatchArgs_Plants;


cbuffer gConstants
{
	uint4 frustumflags[1024];
};


uint unpackFrustum(uint x)
{
    uint index = x >> 2;
    uint offset = x & 0x3;
    switch (offset)
    {
    case 0: return frustumflags[index].x;
    case 1: return frustumflags[index].y;
    case 2: return frustumflags[index].z;
    case 3: return frustumflags[index].w;
    }
    return 0;
}


[numthreads(32, 1, 1)]
void main(uint dispatchId : SV_DispatchThreadId)
{
	const uint t = dispatchId.x;
	

	if ((t>0) && (t < 1000))		// FIXME hardcoded
	{

        uint S = 0;
        const uint lod = tiles[t].lod;
        if ((unpackFrustum(t) & (1 << 20)))
        {
            InterlockedAdd(feedback[0].numTiles[lod], 1, S);
            InterlockedAdd(feedback[0].numSprite[lod], tiles[t].numQuads, S);
            InterlockedMax(feedback[0].numPlantsLOD[lod], tiles[t].numPlants, S);
            InterlockedMax(feedback[0].numTris[lod], tiles[t].numTriangles, S);
        }

        if ((unpackFrustum(t) & (1 << 20)) && tiles[t].numQuads > 0)
        //if ((unpackFrustum(t) >0) && tiles[t].numQuads > 0)
        //if (tiles[t].numQuads > 0)
		{
			
			uint totalQuads = tiles[t].numQuads;
            uint numB = (totalQuads >> 6) + 1;		// FIXME hardcoded move to header

            uint numQuadLookupBlocks;
            InterlockedAdd(feedback[0].numLookupBlocks_Quads, numB, numQuadLookupBlocks);

            uint slot = 0;
            InterlockedAdd(feedback[0].numQuadTiles, 1, slot);
            InterlockedAdd(DrawArgs_Quads[0].instanceCount, numB, slot);
            InterlockedAdd(feedback[0].numQuadBlocks, numB, slot);
            InterlockedAdd(feedback[0].numQuads, totalQuads, slot);
            InterlockedMax(feedback[0].maxQuads, totalQuads, slot);

			for (uint i = 0; i < numB; i++)
			{
				uint numPlants = min(totalQuads, 64);  // number of plants on this block
				totalQuads -= 64;
				tileLookup[numQuadLookupBlocks + i].tile = t + (numPlants << 16);
				tileLookup[numQuadLookupBlocks + i].offset = (t * numQuadsPerTile) + (i * 64);
			}
		}
		
		
		
		// *** and now build the lookup table for the plants compute shader
        //if ((unpackFrustum(t) >0) && tiles[t].lod > 5 && tiles[t].numPlants > 0)
        if ((unpackFrustum(t) > 0) && lod > 5 && tiles[t].numPlants > 0)
		//if (tiles[t].numPlants > 0)
		{
			uint totalPlants = tiles[t].numPlants;
			uint numB = (totalPlants >> 6) + 1;		// FIXME hardcoded

            uint numLookupBlocks = 0;
            InterlockedAdd(feedback[0].numLookupBlocks_Plants, numB, numLookupBlocks);

            uint slot = 0;
            InterlockedAdd(feedback[0].numPlantTiles, 1, slot);
            InterlockedAdd(DrawArgs_Plants[0].instanceCount, numB * 64, slot); // here the empty padding is a LOT worse and also breaks lodding so replace eventually one we have num,bers
            InterlockedAdd(feedback[0].numPlantBlocks, numB, slot);
            InterlockedAdd(feedback[0].numPlants, totalPlants, slot);
            InterlockedMax(feedback[0].maxPlants, totalPlants, slot);

            InterlockedAdd(DispatchArgs_Plants[0].numGroupX, numB, slot);
            

			for (uint i = 0; i < numB; i++)
			{
				uint numPlants = min(totalPlants, 64);  // number of plants ion this block
				totalPlants -= 64;
				plantLookup[numLookupBlocks + i].tile = t + (numPlants <<16);
				plantLookup[numLookupBlocks + i].offset = (t * numPlantsPerTile) + (i * 64);
			}
		}
		
		
		
		/*
			So the really interesting part is that doing frustum culling does almost nothing for rendering speed, the vertex shader is just not the bottlenect
			and its a hell of a lot easier to just have one surface to bother with

            TEST THIS AGAIN
		*/
		
		tiles[t].flags = 0;
		if ((unpackFrustum(t) & (1<<20)))
		{
			tiles[t].flags = 1<<31;
			
			uint totalTriangles = tiles[t].numTriangles;
			uint numB = (totalTriangles >> 6) + 1;									// FIXME hardcoded

            uint numLookupBlocks = 0;
            InterlockedAdd(feedback[0].numLookupBlocks_Terrain, numB, numLookupBlocks);

            uint slot = 0;
            InterlockedAdd(feedback[0].numTerrainTiles, 1, slot);
            InterlockedAdd(DrawArgs_Terrain[0].instanceCount, 64* numB, slot);
            InterlockedAdd(feedback[0].numTerrainBlocks, numB, slot);
            InterlockedAdd(feedback[0].numTerrainVerts, totalTriangles, slot);
            InterlockedMax(feedback[0].maxTriangles, totalTriangles, slot);
            
			for (uint i = 0; i < numB; i++)
			{
				uint numTriangles = min(totalTriangles, 64);  						// number of plants ion this block
				totalTriangles -= 64;

				terrainLookup[numLookupBlocks + i].tile = t + (numTriangles <<16);		// packing - mocve to function					;-) I think tiles[t].index  = t
				terrainLookup[numLookupBlocks + i].offset = (t * numVertPerTile) + (i * 64 * 3);	
			}
		}
	}
}


