#include "groundcover_defines.hlsli"
#include "groundcover_functions.hlsli"		
#include "vegetation_defines.hlsli"



RWStructuredBuffer<t_DrawArguments> DrawArgs_Quads;
RWStructuredBuffer<t_DrawArguments> DrawArgs_Plants;


StructuredBuffer<plant> plant_buffer;
StructuredBuffer<plant_instance> instance_buffer;

RWStructuredBuffer<block_data> block_buffer;
RWStructuredBuffer<plant_instance> instance_buffer_billboard;

globallycoherent  RWStructuredBuffer<vegetation_feedback> feedback;
globallycoherent  RWStructuredBuffer<veg_sort> sort;

// Shadows
RWStructuredBuffer<t_DrawArguments> DrawArgs_Shadows_Quads;
RWStructuredBuffer<t_DrawArguments> DrawArgs_Shadows_Plants;

cbuffer gConstantBuffer
{
    float4x4 view;
    float4x4 frustum;

    float3 eyePos;
    float padd;

    int firstPlant = 0;
    int lastPlant = 10000; // just big
    int firstLod = 0;
    int lastLod = 100;

    float lodBias;
    float halfAngle_to_Pixels;

    // Shadows blocks
};


#if SHADOW_TILE

[numthreads(256, 1, 1)]
void main(uint idx : SV_DispatchThreadId)
{
}

#elif SHADOW_OUTER

[numthreads(256, 1, 1)]
void main(uint idx : SV_DispatchThreadId)
{
}

#else

// lets see AMD recommends 256, seems I have opick too small vlaues in tehpast - TEST
[numthreads(256, 1, 1)]
void main(uint idx : SV_DispatchThreadId)
{
    uint slot = 0;
    const plant_instance INSTANCE = instance_buffer[idx];
    const plant PLANT = plant_buffer[INSTANCE.plant_idx];
    InterlockedAdd(feedback[0].numPlantsType[INSTANCE.plant_idx], 1, slot);
    //const plant PLANT = plant_buffer[0];

    //if (INSTANCE.plant_idx >= firstPlant && INSTANCE.plant_idx < lastPlant)
    if (firstPlant == 0 || idx == 0)
    {
        float radius = 1 * (PLANT.size.x + PLANT.size.y); // or something like that, or precalc radius
        float4 viewPos = mul(float4(INSTANCE.position + float3(0, PLANT.size.y * 0.5, 0), 1), view); //??? maak seker
        float4 test = saturate(mul(frustum, viewPos) + float4(radius, radius, radius, radius));

        if (viewPos.z < -0.0 && all(test))
        {
            float distance = length(viewPos.xyz); // can use view.z but that causes lod changes on rotation and is less good, although mnore acurate
            //float pix = 1 * lodBias * PLANT.size.y * INSTANCE.scale / distance * 1080; // And add a user controlled scale in as well
            float pix = lodBias * halfAngle_to_Pixels * PLANT.size.y * INSTANCE.scale / distance;

            float3 viewDir = normalize(eyePos - INSTANCE.position);
            //if (viewDir.y > 0.98) pix *= 3;
            //
            // FIOXME reintroduce byt make sure oit doesnt mess with the basic pixel size
            pix *= (1.0 + smoothstep(0.97, 1.0, viewDir.y));

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
                    if (pix > PLANT.lods[i].pixSize)        lod = i;
                }

                if (idx == 0)
                {
                    feedback[0].plantZeroLod = lod;
                }

                if (firstLod < 0 || lod == firstLod)
                {
                    // testing for my new Z-sort

                    float fog_far_Start = 1.f;
                    float fog_far_End = 3000.f;
                    float fog_far_log_F = (339.f / 400.f / (log(fog_far_End / fog_far_Start)));
                    float fog_far_one_over_k = 1.0f / 400.f;
                    uint z_index = pow((distance - fog_far_Start) / (fog_far_End - fog_far_Start), 0.5) * 399;// (log(distance / fog_far_Start) * fog_far_log_F + fog_far_one_over_k) * 400;

                    float step = 1.07;
                    z_index = log(distance / fog_far_Start) / log(step);
                    z_index = clamp(z_index, 0, 127);

                    uint NUMBLOCKS = PLANT.lods[lod].numBlocks;


                    InterlockedAdd(sort[z_index].requested, NUMBLOCKS, slot);
                    if (slot + NUMBLOCKS >= sort[z_index].size)
                    {
                        z_index = 127;
                        InterlockedAdd(feedback[0].numInstRejected, 1, slot);
                    }

                    InterlockedAdd(feedback[0].numBlock_Z[z_index], NUMBLOCKS, slot);   // tHIS IS PURELY VISUAL FOR FEEDBACK
                    InterlockedAdd(feedback[0].numLod[lod + 1], 1, slot);

                    InterlockedAdd(feedback[0].numBlocks, NUMBLOCKS, slot);
                    InterlockedAdd(sort[z_index].current, NUMBLOCKS, slot);

                    for (int i = 0; i < NUMBLOCKS; i++)
                    {
                        block_buffer[sort[z_index].offset + slot + i].instance_idx = idx;
                        block_buffer[sort[z_index].offset + slot + i].vertex_offset = (PLANT.lods[lod].startVertex + i) * VEG_BLOCK_SIZE;
                    }

                    InterlockedAdd(feedback[0].numInstAdded, 1, slot);
                }
            }
        }
    }
}

#endif
