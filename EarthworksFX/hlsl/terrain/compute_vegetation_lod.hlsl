#include "groundcover_defines.hlsli"
#include "groundcover_functions.hlsli"		
#include "vegetation_defines.hlsli"



RWStructuredBuffer<t_DrawArguments> DrawArgs_Quads;
RWStructuredBuffer<t_DrawArguments> DrawArgs_Plants;

StructuredBuffer<plant> plant_buffer;
StructuredBuffer<plant_instance> instance_buffer;

RWStructuredBuffer<block_data> block_buffer;
RWStructuredBuffer<plant_instance> instance_buffer_billboard;

RWStructuredBuffer<vegetation_feedback> feedback;


cbuffer gConstantBuffer
{
    float4x4 view;
    float4x4 frustum;

    int firstPlant = 0;
    int lastPlant = 10000; // just big
    int firstLod = 0;
    int lastLod = 100;

    float lodBias;
    float halfAngle_to_Pixels;
};



// lets see AMD recommends 256, seems I have opick too small vlaues in tehpast - TEST
[numthreads(256, 1, 1)]
void main(uint idx : SV_DispatchThreadId)
{
    uint slot = 0;
    const plant_instance INSTANCE = instance_buffer[idx];
    const plant PLANT = plant_buffer[INSTANCE.plant_idx];
    InterlockedAdd(feedback[0].numPlantsType[INSTANCE.plant_idx], 1, slot);
    //const plant PLANT = plant_buffer[0];

    if (INSTANCE.plant_idx >= firstPlant && INSTANCE.plant_idx < lastPlant)
    {
        float radius = 1 * (PLANT.size.x + PLANT.size.y); // or something like that, or precalc radius
        float4 viewPos = mul(float4(INSTANCE.position + float3(0, PLANT.size.y * 0.5, 0), 1), view); //??? maak seker
        float4 test = saturate(mul(frustum, viewPos) + float4(radius, radius, radius, radius));
        
        if (all(test))
        {
            float distance = length(viewPos.xyz); // can use view.z but that causes lod changes on rotation and is less good, although mnore acurate
            //float pix = 1 * lodBias * PLANT.size.y * INSTANCE.scale / distance * 1080; // And add a user controlled scale in as well
            float pix = lodBias * halfAngle_to_Pixels * PLANT.size.y * INSTANCE.scale / distance;
            

            if (idx == 0)
            {
                feedback[0].plantZero_pixeSize = pix;
            }

            if (pix < PLANT.lods[0].pixSize)        // do billboards
            {
                InterlockedAdd(DrawArgs_Quads[0].vertexCountPerInstance, 1, slot);
                instance_buffer_billboard[slot] = INSTANCE;
                InterlockedAdd(feedback[0].numLod[0], 1, slot);
            }
            else
            {
                int lod = 0;
            
                for (int i = 0; i < PLANT.numLods; i++)
                {
                    if (pix >= PLANT.lods[i].pixSize)        lod = i;
                }

                if (idx == 0)
                {
                    feedback[0].plantZeroLod = lod;
                }
                //if (firstLod < 0 || lod == firstLod)
                //if (lod >=6)
                {
                    InterlockedAdd(feedback[0].numLod[lod + 1], 1, slot);
            
                    
            
                    InterlockedAdd(feedback[0].numBlocks, PLANT.lods[lod].numBlocks, slot);
                    InterlockedAdd(DrawArgs_Plants[0].instanceCount, PLANT.lods[lod].numBlocks, slot);
 
            
                    for (int i = 0; i < PLANT.lods[lod].numBlocks; i++)
                    {
                        block_buffer[slot + i].instance_idx = idx;
                        //block_buffer[slot + i].plant_idx = INSTANCE.plant_idx;
                        //block_buffer[slot + i].section_idx = 0; // FIXME add later
                        block_buffer[slot + i].vertex_offset = (PLANT.lods[lod].startVertex + i) * VEG_BLOCK_SIZE;
                    }
                }
            }
        }
    }
}
