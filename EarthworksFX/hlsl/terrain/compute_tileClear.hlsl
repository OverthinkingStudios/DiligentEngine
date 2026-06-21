#include "groundcover_defines.hlsli"
#include "vegetation_defines.hlsli"


RWStructuredBuffer<GC_feedback>			feedback;

RWStructuredBuffer<t_DrawArguments> 	DrawArgs_Quads;
RWStructuredBuffer<t_DrawArguments> 	DrawArgs_Terrain;
RWStructuredBuffer<t_DrawArguments> 	DrawArgs_Plants;
//RWStructuredBuffer<t_DrawArguments> 	DrawArgs_ClippedLoddedPlants;   // 
RWStructuredBuffer<t_DispatchArguments> DispatchArgs_Plants;

RWStructuredBuffer<vegetation_feedback> feedback_Veg;




[numthreads(1, 1, 1)]
void main(uint dispatchId : SV_DispatchThreadId)
{
	// Clear the feedback
	// -----------------------------------------------------------------------------------------------------
	
    feedback[0].numPostClippedPlants = 0;
	
	
	
    feedback[0].vegRibbonClippedPixels = 9;
    feedback[0].vegRibbonOKPixels = 900;


    // Clear the draw arguments
    // -----------------------------------------------------------------------------------------------------
    for (int i = 0; i < numRenderViews; i++)
    {
        feedback[0].numLookupBlocks_Quads[i] = 0;
        feedback[0].numLookupBlocks_Plants[i] = 0;
        feedback[0].numLookupBlocks_Terrain[i] = 0;

        feedback[0].numTerrainTiles[i] = 0;
        feedback[0].numTerrainBlocks[i] = 0;
        feedback[0].numTerrainVerts[i] = 0;
        feedback[0].maxTriangles[i] = 0;

        feedback[0].numQuadTiles[i] = 0;
        feedback[0].numQuadBlocks[i] = 0;
        feedback[0].numQuads[i] = 0;
        feedback[0].maxQuads[i] = 0;

        feedback[0].numPlantTiles[i] = 0;
        feedback[0].numPlantBlocks[i] = 0;
        feedback[0].numPlants[i] = 0;
        feedback[0].maxPlants[i] = 0;

        DrawArgs_Terrain[i].instanceCount = 0;
        DrawArgs_Terrain[i].vertexCountPerInstance = 3;

        DrawArgs_Quads[i].instanceCount = 0;
        DrawArgs_Quads[i].vertexCountPerInstance = 64;

        DrawArgs_Plants[i].instanceCount = 0;
        DrawArgs_Plants[i].vertexCountPerInstance = VEG_BLOCK_SIZE;// 128;

        //DrawArgs_ClippedLoddedPlants[i].instanceCount = 0;
        //DrawArgs_ClippedLoddedPlants[i].vertexCountPerInstance = 0;// 1598;//    2114; //542; // 768 for triangles 2114;//

        DispatchArgs_Plants[i].numGroupX = 0;
        DispatchArgs_Plants[i].numGroupY = 1;
        DispatchArgs_Plants[i].numGroupZ = 1;
        DispatchArgs_Plants[i].padd = 0;
    }


    for (int j = 0; j < 20; j++)
    {
        feedback[0].numTiles[j] = 0;
        feedback[0].numSprite[j] = 0;
        feedback[0].numPlantsLOD[j] = 0;
        feedback[0].numTris[j] = 0;
        feedback[0].numPix[j] = 0;
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

    feedback_Veg[0].numPixClip = 0;
    feedback_Veg[0].numPixPass = 0;
    feedback_Veg[0].numInstAdded = 0;
    feedback_Veg[0].numInstRejected = 0;

    for (int i = 0; i < 128; i++)
    {
        feedback_Veg[0].numBlock_Z[i] = 0;
    }
}
