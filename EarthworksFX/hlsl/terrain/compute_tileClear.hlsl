#include "groundcover_defines.hlsli"
#include "vegetation_defines.hlsli"


RWStructuredBuffer<GC_feedback>			feedback;

RWStructuredBuffer<t_DrawArguments> 	DrawArgs_Quads;
RWStructuredBuffer<t_DrawArguments> 	DrawArgs_Terrain;
RWStructuredBuffer<t_DrawArguments> 	DrawArgs_Plants;
RWStructuredBuffer<t_DrawArguments> 	DrawArgs_ClippedLoddedPlants;
RWStructuredBuffer<t_DispatchArguments> DispatchArgs_Plants;

RWStructuredBuffer<vegetation_feedback> feedback_Veg;




[numthreads(1, 1, 1)]
void main(uint dispatchId : SV_DispatchThreadId)
{
	// Clear the feedback
	// -----------------------------------------------------------------------------------------------------
	feedback[0].numQuadTiles = 0;
	feedback[0].numQuadBlocks = 0;
	feedback[0].numQuads = 0;
    feedback[0].maxQuads = 0;

	feedback[0].numPlantTiles = 0;
	feedback[0].numPlantBlocks = 0;
	feedback[0].numPlants = 0;
    feedback[0].maxPlants = 0;
    feedback[0].numPostClippedPlants = 0;
	
	feedback[0].numTerrainTiles = 0;
	feedback[0].numTerrainBlocks = 0;
	feedback[0].numTerrainVerts = 0;
    feedback[0].maxTriangles = 0;
	
	feedback[0].numLookupBlocks_Quads = 0;
	feedback[0].numLookupBlocks_Plants = 0;
	feedback[0].numLookupBlocks_Terrain = 0;


    // Clear the draw arguments
    // -----------------------------------------------------------------------------------------------------
    DrawArgs_Terrain[0].instanceCount = 0;
    DrawArgs_Terrain[0].vertexCountPerInstance = 3;

    DrawArgs_Quads[0].instanceCount = 0;
    DrawArgs_Quads[0].vertexCountPerInstance = 64;

    DrawArgs_Plants[0].instanceCount = 0;
    DrawArgs_Plants[0].vertexCountPerInstance = 128;

    DrawArgs_ClippedLoddedPlants[0].instanceCount = 0;
    DrawArgs_ClippedLoddedPlants[0].vertexCountPerInstance = 1598;//    2114; //542; // 768 for triangles 2114;//

    DispatchArgs_Plants[0].numGroupX = 0;
    DispatchArgs_Plants[0].numGroupY = 1;
    DispatchArgs_Plants[0].numGroupZ = 1;
    DispatchArgs_Plants[0].padd = 0;



    for (int j = 0; j < 20; j++)
    {
        feedback[0].numTiles[j] = 0;
        feedback[0].numSprite[j] = 0;
        feedback[0].numPlantsLOD[j] = 0;
        feedback[0].numTris[j] = 0;
    }

// veg feedback
    feedback_Veg[0].numInstanceAddedComputeClipLod = 0; // since passtherough is before clipLod, bur dedicated is betetrfeedback_Veg[0].numBillboard
    feedback_Veg[0].numFrustDiscard = 0;
    feedback_Veg[0].numBillboard = 13;
    feedback_Veg[0].numPlant = 33;
    feedback_Veg[0].numBlocks = 0;
    feedback_Veg[0].numLod[0] = 0;
    feedback_Veg[0].numLod[1] = 0;
    feedback_Veg[0].numLod[2] = 0;
    feedback_Veg[0].numLod[3] = 0;
    feedback_Veg[0].numLod[4] = 0;
    feedback_Veg[0].numLod[5] = 0;
    feedback_Veg[0].numLod[6] = 0;
    feedback_Veg[0].numLod[7] = 0;
    feedback_Veg[0].numLod[8] = 0;
    feedback_Veg[0].numLod[9] = 0;
    feedback_Veg[0].numLod[10] = 0;
    feedback_Veg[0].numLod[11] = 0;
    feedback_Veg[0].numLod[12] = 0;
    feedback_Veg[0].numLod[13] = 0;
    feedback_Veg[0].numLod[14] = 0;
    feedback_Veg[0].numLod[15] = 0;

}
