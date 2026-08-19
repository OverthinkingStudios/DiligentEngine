#include "vegetation_defines.hlsli"
#include "groundcover_defines.hlsli"


RWStructuredBuffer<t_DrawArguments> DrawArgs_Quads;
RWStructuredBuffer<t_DrawArguments> DrawArgs_Plants;
RWStructuredBuffer<vegetation_feedback> feedback_Veg;




// lets see AMD recommends 256, seems I have opick too small vlaues in tehpast - TEST
[numthreads(1, 1, 1)]
void main(uint idx : SV_DispatchThreadId)
{
    for (int i = 0; i < 128; i++)
    {
        DrawArgs_Plants[i].instanceCount = 0;
        DrawArgs_Plants[i].vertexCountPerInstance = VEG_BLOCK_SIZE;
        DrawArgs_Plants[i].startInstanceLocation = 0;
        DrawArgs_Plants[i].startVertexLocation = 0;
    }

    DrawArgs_Quads[0].instanceCount = 1;
    DrawArgs_Quads[0].vertexCountPerInstance = 0;
    DrawArgs_Quads[0].startInstanceLocation = 0;
    DrawArgs_Quads[0].startVertexLocation = 0;

    for (int i = 0; i < 32; i++)
    {
        feedback_Veg[0].numLod[i] = 0;
        feedback_Veg[0].numPlantsType[i] = 0;

    }
    feedback_Veg[0].numBlocks = 0;


    feedback_Veg[0].numInstanceAddedComputeClipLod = 0; // since passtherough is before clipLod, bur dedicated is betetrfeedback_Veg[0].numBillboard
    feedback_Veg[0].numFrustDiscard = 0;
    feedback_Veg[0].numBillboard = 13;
    feedback_Veg[0].numPlant = 33;
    feedback_Veg[0].numBlocks = 0;

    feedback_Veg[0].numPixClip = 0;
    feedback_Veg[0].numPixPass = 0;
    feedback_Veg[0].numInstAdded = 0;
    feedback_Veg[0].numInstRejected = 0;

    for (int i = 0; i < 128; i++)
    {
        feedback_Veg[0].numBlock_Z[i] = 0;
    }
}
