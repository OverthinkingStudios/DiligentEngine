#include "groundcover_defines.hlsli"
#include "groundcover_functions.hlsli"		
#include "vegetation_defines.hlsli"



RWStructuredBuffer<t_DrawArguments> DrawArgs_Plants;


RWStructuredBuffer<block_data> pre_block_buffer;
RWStructuredBuffer<block_data> post_block_buffer;
globallycoherent  RWStructuredBuffer<veg_sort> sort;


globallycoherent  RWStructuredBuffer<vegetation_feedback> feedback;






#if _COPY

[numthreads(256, 1, 1)]
void main(uint idx : SV_DispatchThreadId)
{
    uint idx = 0;
    uint cnt = 0;
    uint total = 0;
    DrawArgs_Plants[0].instanceCount = sort[i].current;
}


#else

// lets see AMD recommends 256, seems I have opick too small vlaues in tehpast - TEST
[numthreads(1, 1, 1)]
void main(uint idx : SV_DispatchThreadId)
{
    uint slot = 0;
    uint newPos = 0;
    for (int i = 0; i < 128; i++)
    {
        DrawArgs_Plants[i].startInstanceLocation = sort[i].offset;
        DrawArgs_Plants[i].instanceCount = sort[i].current;
    }


    uint offset = 0;
    for (int i = 0; i < 128; i++)
    {
        sort[i].size = sort[i].requested * 2 + 2024;    // 1024 halps us naqviagte movement when some sectiosn go from zero to something
        sort[i].offset = offset;
        offset += sort[i].size;
        sort[i].current = 0;
        sort[i].requested = 0;
    }
}

#endif
