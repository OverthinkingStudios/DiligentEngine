
#include "groundcover_defines.hlsli"
#include "groundcover_functions.hlsli"	
#include "vegetation_defines.hlsli"


StructuredBuffer<gpuTile>           tiles;
StructuredBuffer<tileLookupStruct>  tileLookup;
StructuredBuffer<instance_PLANT>    plantBuffer;

RWStructuredBuffer<xformed_PLANT>   output;
RWStructuredBuffer<t_DrawArguments> drawArgs_Plants;
RWStructuredBuffer<GC_feedback>		feedback;

RWStructuredBuffer<plant_instance> instance_out;

// new
StructuredBuffer<plant> plant_buffer;
RWStructuredBuffer<block_data> block_buffer;
RWStructuredBuffer<vegetation_feedback> feedback_Veg;

cbuffer gConstantBuffer
{
    float4x4 view;
    float4x4 clip;

    float halfAngle_to_Pixels;
};


//32 LIKELY BETETR
[numthreads(64, 1, 1)]			
void main(uint plantId : SV_GroupThreadID, uint blockId : SV_GroupID)
{
    const uint tileIDX = tileLookup[blockId].tile & 0xffff;
    const uint numQuad = tileLookup[blockId].tile >> 16;


    if (plantId < numQuad)
    {
        // clip
        instance_PLANT instance = plantBuffer[tileLookup[blockId].offset + plantId];
        const uint idx = PLANT_INDEX(instance.s_r_idx);
        const plant PLANT = plant_buffer[idx];
        float3 position = unpack_pos(instance.xyz, tiles[tileIDX].origin, tiles[tileIDX].scale_1024);
        float scale = SCALE(instance.s_r_idx);
        float4 viewBS = mul(float4(position + float3(0, PLANT.size.y * 0.5, 0), 1), view);
        float radius = 1 * (PLANT.size.x + PLANT.size.y); // or something like that, or precalc radius
        float4 test = saturate(mul(clip, viewBS) + float4(radius, radius, radius, radius));
        bool inFrust = all(test);
        // FIXME move middle upwards, expand on thsi a bit, as well as teh 4 4 4 4 above

        
        
        feedback_Veg[0].numBillboard = feedback[0].numQuads;
        // extract and save
        
        if (inFrust)
        {
            uint slot = 0;
            InterlockedAdd(feedback_Veg[0].numPlant, 1, slot);

            uint slotInst;
            InterlockedAdd(feedback_Veg[0].numInstanceAddedComputeClipLod, 1, slotInst);
            instance_out[slotInst].position = position;
            instance_out[slotInst].scale = scale;
            instance_out[slotInst].rotation = ROTATION(instance.s_r_idx);
            instance_out[slotInst].plant_idx = idx;

            
            

            float distance = length(viewBS.xyz); // can use view.z but that causes lod changes on rotation and is less good, although mnore acurate
            float pix = halfAngle_to_Pixels * PLANT.size.y * scale / distance;
            //lodBias

            int lod = 0;
            
            for (int i = 0; i < PLANT.numLods; i++)
            {
                if (pix >= PLANT.lods[i].pixSize)
                    lod = i;
            }


            //if (firstLod < 0 || lod == firstLod)
            {

                //uint slot = 0;
                InterlockedAdd(feedback_Veg[0].numLod[lod + 1], 1, slot);
                InterlockedAdd(feedback[0].numPostClippedPlants, 1, slot); // duplicates numInstanceAddedComputeClipLod just for feedback
                
            
                if (idx == 0)
                {
                    //feedback[0].plantZero_pixeSize = pix;
                    //feedback[0].plantZeroLod = lod;
                }
            

            
                InterlockedAdd(feedback_Veg[0].numBlocks, PLANT.lods[lod].numBlocks, slot);
                InterlockedAdd(drawArgs_Plants[0].instanceCount, PLANT.lods[lod].numBlocks, slot);
                drawArgs_Plants[0].vertexCountPerInstance = VEG_BLOCK_SIZE; // FIXME move to a clear shader once
            
 
            
                for (int i = 0; i < PLANT.lods[lod].numBlocks; i++)
                {
                    block_buffer[slot + i].instance_idx = slotInst;
                    //block_buffer[slot + i].plant_idx = idx;
                    //block_buffer[slot + i].section_idx = 0; // FIXME add later
                    block_buffer[slot + i].vertex_offset = (PLANT.lods[lod].startVertex + i) * VEG_BLOCK_SIZE;
                }

            }

/*
old code
            uint slot = 0;
            InterlockedAdd(drawArgs_Plants[0].instanceCount, 1, slot);

            output[slot].position = position;
            output[slot].scale = 4 * scale;
            output[slot].rotation = ROTATIONxy(instance.s_r_idx);
            output[slot].index = PLANT_INDEX(instance.s_r_idx); // just write 1 function that fills it all

            InterlockedAdd(feedback[0].numPostClippedPlants, 1, slot);
*/
        }
        else
        {
            InterlockedAdd(feedback_Veg[0].numFrustDiscard, 1);
        }
    }

}
