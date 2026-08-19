
#include "groundcover_defines.hlsli"
#include "groundcover_functions.hlsli"
#include "terrainDefines.hlsli"
				

/*
RWStructuredBuffer<tileLookupStruct>	tileLookup;				// output
RWStructuredBuffer<tileLookupStruct>	plantLookup;			// output
RWStructuredBuffer<tileLookupStruct>	terrainLookup;			// output
*/
RWStructuredBuffer<t_DrawArguments> 	DrawArgs_Quads;
RWStructuredBuffer<t_DrawArguments> 	DrawArgs_Terrain;
RWStructuredBuffer<t_DispatchArguments> DispatchArgs_Plants;
//RWStructuredBuffer<t_DrawArguments> 	DrawArgs_Plants;

RWStructuredBuffer<gpuTile> 			tiles;              // RW since we seem to use this tro transfer frustumflags
//StructuredBuffer<uint> 					visibility;

RWStructuredBuffer<centerFeedback> 	    tileCenters;
RWStructuredBuffer<GC_feedback>			feedback;




struct views
{
    RWStructuredBuffer<tileLookupStruct> terrainLookup[numRenderViews];
    RWStructuredBuffer<tileLookupStruct> plantLookup[numRenderViews];
    RWStructuredBuffer<tileLookupStruct> quadLookup[numRenderViews];
};
ParameterBlock<views> viewRenderData;



cbuffer gConstants
{
	uint4 frustumflags[1024];
};

/*
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
*/
void packTile(uint _t, uint _view)
{
    uint startBlock = 0;
    uint slot = 0;
    uint totalTriangles = tiles[_t].numTriangles;
    uint numBlocks = (totalTriangles >> 6) + 1;									// FIXME hardcoded
    
    InterlockedAdd(feedback[0].numLookupBlocks_Terrain[_view], numBlocks, startBlock);
    InterlockedAdd(DrawArgs_Terrain[_view].instanceCount, 64 * numBlocks, slot);    // calc starblock off this number
    // cant we just use numBlocks, and tehn have the vertexcoutn 3*64 ?

    // can we pre-create all of these blocks ad do a larger memcopy here instead of a loop
    for (uint i = 0; i < numBlocks; i++)
    {
        viewRenderData.terrainLookup[_view][startBlock + i] = lu_Pack(_t, i, min(totalTriangles, 64));
        totalTriangles -= 64;
    }

#if defined COMPUTE_DEBUG_OUTPUT
    InterlockedAdd(feedback[0].numTerrainTiles[_view], 1, slot);
    InterlockedAdd(feedback[0].numTerrainBlocks[_view], numBlocks, slot);
    InterlockedAdd(feedback[0].numTerrainVerts[_view], tiles[_t].numTriangles, slot);
    InterlockedMax(feedback[0].maxTriangles[_view], tiles[_t].numTriangles, slot);
#endif   
}



void packBillboard(uint _t, uint _view)
{
    uint startBlock = 0;
    //uint slot = 0;
    uint totalQuads = tiles[_t].numQuads;
    uint numBlocks = (totalQuads >> 6) + 1;		// FIXME hardcoded move to header

    InterlockedAdd(feedback[0].numLookupBlocks_Quads[_view], numBlocks, startBlock);
    InterlockedAdd(DrawArgs_Quads[_view].instanceCount, numBlocks, startBlock);

    // can we pre-create all of these blocks ad do a larger memcopy here instead of a loop
    for (uint i = 0; i < numBlocks; i++)
    {
        viewRenderData.quadLookup[_view][startBlock + i] = lu_Pack(_t, i, min(totalQuads, 64));
        totalQuads -= 64;
    }

#if defined COMPUTE_DEBUG_OUTPUT
    uint slot = 0;
    InterlockedAdd(feedback[0].numQuadTiles[_view], 1, slot);
    InterlockedAdd(feedback[0].numQuadBlocks[_view], numBlocks, slot);
    InterlockedAdd(feedback[0].numQuads[_view], tiles[_t].numQuads, slot);
    InterlockedMax(feedback[0].maxQuads[_view], tiles[_t].numQuads, slot);
#endif   
}


void packPlants(uint _t, uint _view)
{
    uint startBlock = 0;
    uint slot = 0;
    uint totalPlants = tiles[_t].numPlants;
    uint numBlocks = (totalPlants >> 6) + 1;		// FIXME hardcoded

    InterlockedAdd(feedback[0].numLookupBlocks_Plants[_view], numBlocks, startBlock);
    InterlockedAdd(DispatchArgs_Plants[_view].numGroupX, numBlocks, slot);

    // can we pre-create all of these blocks ad do a larger memcopy here instead of a loop
    for (uint i = 0; i < numBlocks; i++)
    {
        viewRenderData.plantLookup[_view][startBlock + i] = lu_Pack(_t, i, min(totalPlants, 64));
        totalPlants -= 64;
    }

#if defined COMPUTE_DEBUG_OUTPUT
    InterlockedAdd(feedback[0].numPlantTiles[_view], 1, slot);
    InterlockedAdd(feedback[0].numPlantBlocks[_view], numBlocks, slot);
    InterlockedAdd(feedback[0].numPlants[_view], tiles[_t].numPlants, slot);
    InterlockedMax(feedback[0].maxPlants[_view], tiles[_t].numPlants, slot);
#endif   
}








[numthreads(32, 1, 1)]
void main(uint dispatchId : SV_DispatchThreadId)
{
	const uint t = dispatchId.x;


    uint viewMask = main_CENTER | cascade_0 | cascade_1 | cascade_2 | parabolic_low | parabolic_medium;


	if ((t>0) && (t < 1000))		// FIXME hardcoded
	{
        uint surfaceFlags = frustumflags[t].x;
        uint visibleFlags = frustumflags[t].y;
        tiles[t].flags = surfaceFlags;// unpackFrustum(t);

        if (tiles[t].flags == 0)
        {
            tileCenters[t].min = 0; // clear unused
        }

#if defined COMPUTE_DEBUG_OUTPUT
        if (tiles[t].flags & main_CENTER)
        {
            uint S = 0;
            const uint lod = tiles[t].lod;
            InterlockedAdd(feedback[0].numTiles[lod], 1, S);
            InterlockedAdd(feedback[0].numSprite[lod], tiles[t].numQuads, S);
            InterlockedMax(feedback[0].numPlantsLOD[lod], tiles[t].numPlants, S);
            InterlockedMax(feedback[0].numTris[lod], tiles[t].numTriangles, S);
        }
# endif

        for (int view = 0; view < numRenderViews; view++)
        {
            if (surfaceFlags & viewMask & (1 << view))
            {
                packTile(t, view);
            }

            if ((surfaceFlags & viewMask & (1 << view)) && (tiles[t].numQuads > 0))
            {
                packBillboard(t, view);
            }

            if ((visibleFlags & viewMask) && (tiles[t].lod > 5) && (tiles[t].numPlants > 0))
            {
                packPlants(t, view);
            }
        }
	}
}
