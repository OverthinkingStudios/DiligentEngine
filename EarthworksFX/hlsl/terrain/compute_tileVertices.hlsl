

#include "terrainDefines.hlsli"
#include "terrainFunctions.hlsli"
#include "groundcover_defines.hlsli"



SamplerState linearSampler;		// for noise and colours

Texture2D<float> 					gInHgt;
RWTexture2D<uint> 					gOutVerts;
RWTexture2D<float4> 				gDebug;
RWStructuredBuffer<centerFeedback> 	tileCenters;
RWStructuredBuffer<gpuTile> 		tiles;



cbuffer gConstants
{
	float4 constants;			// .x pixsize
};
#define _PIXSIZE constants.x


groupshared uint vCount = 0;



void debugInputGrid(int2 crd)
{
    if (gOutVerts[crd] > 0)
    {
        gDebug[crd * 2] = float4(0.3, 0.3, 0.3, 1);
    }
}



bool testPixel(const int2 crd, const uint size, const float cutoff)
{
    int3 hCrd = int3(crd * 2, 0);
    float avs = gInHgt.Load(hCrd + int3(-size, -size, 0)) +
        gInHgt.Load(hCrd + int3(-size, size, 0)) +
        gInHgt.Load(hCrd + int3(size, -size, 0)) +
        gInHgt.Load(hCrd + int3(size, size, 0));

    float mid = gInHgt.Load(hCrd, 0);

#if defined(COMPUTE_DEBUG_OUTPUT)
    if (abs(mid - (avs * 0.25)) > cutoff)
    {
        float4 colour = float4(0.7, 0.7, 0.7, 1);
        if (size == 2)  colour = float4(0, 1.0, 0, 1);
        if (size == 1)  colour = float4(0, 1.0, 1.0, 1);
        gDebug[crd * 2] = colour;
    }
#endif

    return (abs(mid - (avs * 0.25)) > cutoff);
}



[numthreads(tile_cs_ThreadSize, tile_cs_ThreadSize, 1)]	
void main(int2 coord : SV_DispatchThreadId)
{
#if defined(COMPUTE_DEBUG_OUTPUT)
    debugInputGrid(coord);
#endif
    
    // FIXME move somehwre else it does not belong here
    if (coord.x == 0 && coord.y == 0) {
        uint tileIdx = constants.w;
        float centerHeight = gInHgt.SampleLevel(linearSampler, float2(0.5, 0.5), 0);
        tileCenters[tileIdx].min = centerHeight;
        tiles[tileIdx].origin.y = centerHeight - (tiles[tileIdx].scale_1024 * 2048);	// Its corner origin rather than middle
        //tiles[tileIdx].origin.y = gHeight[uint2(128, 128)].r - (tile.scale_1024 * 2048);
    }
    

    uint idx;

    if (coord.x <= 28 && coord.y <= 28)
    {
        uint2 pixCrd = coord * 4 + 9;
        if (testPixel(pixCrd, 4, _PIXSIZE * 0.1))
        {
            InterlockedAdd(tiles[constants.w].numVerticis, 1, idx);
            gOutVerts[pixCrd] = (pixCrd.y << 7) + pixCrd.x;
        }
    }
    GroupMemoryBarrierWithGroupSync();


    if (coord.x <= 61 && coord.y <= 61)
    {
        uint2 pixCrd = coord * 2 + 3;
        if (testPixel(pixCrd, 2, _PIXSIZE * 0.15))
        {
            InterlockedAdd(tiles[constants.w].numVerticis, 1, idx);
            gOutVerts[pixCrd] = (pixCrd.y << 7) + pixCrd.x;
        }
    }
    GroupMemoryBarrierWithGroupSync();
    
    float lastscale = 0.1;
    if (idx > 20) lastscale = 0.15;

    if (coord.x <= 124 && coord.y <= 124)
    {
        uint2 pixCrd = coord + 2;
        if (testPixel(pixCrd, 1, _PIXSIZE * lastscale))
        {
            
            InterlockedAdd(tiles[constants.w].numVerticis, 1, idx);
            //if (idx < 2000)
            {
                gOutVerts[pixCrd] = (pixCrd.y << 7) + pixCrd.x;
            }

#if defined(COMPUTE_DEBUG_OUTPUT)
            if (idx > 2000)
            {
                gDebug[pixCrd * 2] = float4(1, 0, 0, 1);
            }
#endif
        }
    }

}





/*


[numthreads(tile_cs_ThreadSize, tile_cs_ThreadSize, 1)]
void main(uint2 coord : SV_DispatchThreadId)
{

    if (coord.x == 127 && coord.y == 127) {
        uint tileIdx = constants.w;
        float centerHeight = gInHgt.SampleLevel(linearSampler, float2(0.5, 0.5), 0);
        tileCenters[tileIdx].min = centerHeight;
        tiles[tileIdx].origin.y = centerHeight - (tiles[tileIdx].scale_1024 * 2048);	// Its corner origin rather than middle
    }



    float pixSize = constants.x;
    float scale = constants.y;
    int offset = constants.z;

    int STEP = 4;

    for (int lod=0; lod<3; lod++)
    {
        int mask = 0x3 >> lod;
        int2 Vcrd = coord + mask;

        if (gOutVerts[Vcrd] > 0)
        {
            uint2 dCrd = Vcrd * 2 + 1;
            gDebug[dCrd] = float4(1, 1, 1, 1);
        }

        if (!any(coord & mask) && Vcrd.x < 127 && Vcrd.y < 127) {
            float2 uv = (Vcrd + 1.0) / 128.0f;
            float mip = 2-lod;

            int3 Hcrd = int3(Vcrd * 2 + 1, 0);
            float hAVS = 0;
            //  H[y][x] = gInput.Load(UV0 + int3(x, y, 0)) * hgt_scale;
            hAVS += gInHgt.Load(int3(Hcrd.x - 1, Hcrd.y - 1, 0));
            hAVS += gInHgt.Load(int3(Hcrd.x + 1, Hcrd.y - 1, 0));
            hAVS += gInHgt.Load(int3(Hcrd.x - 1, Hcrd.y + 1, 0));
            hAVS += gInHgt.Load(int3(Hcrd.x + 1, Hcrd.y + 1, 0));

            float hgt = gInHgt.Load(int3(Hcrd.x, Hcrd.y, 0));

            //if (abs( gInHgt.SampleLevel(linearSampler, uv, mip) - gInHgt.SampleLevel(linearSampler, uv, mip+1) ) > pixSize/5)
            if (abs(hgt - (hAVS / 4.0)) > pixSize / 15)
            {
                uint idx;
                InterlockedAdd(tiles[constants.w].numVerticis, 1, idx);
                if (idx < 1000)
                {

                    // test just pull to the edge
                    int edge = mask + 2; //???
                    if (Vcrd.x < edge)				Vcrd.x = 0;
                    if ((126 - Vcrd.x) < edge) 		Vcrd.x = 126;
                    if ((Vcrd.y) < edge) 			Vcrd.y = 0;
                    if ((126 - Vcrd.y) < edge) 		Vcrd.y = 126;

                    gOutVerts[Vcrd] = pack_tile_pos(Vcrd * 2 + 2);

#if defined(COMPUTE_DEBUG_OUTPUT)
                    uint2 dCrd = Vcrd * 2 + 1;
                    if (lod == 0) {
                        //gDebug[dCrd] = float4(0, 0.6, 1, 1);
                    }
                    if (lod == 1) {
                        //gDebug[dCrd] = float4(0, 1, 0.0, 1);
                    }
                    if (lod == 2) {
                        //gDebug[dCrd] = float4(1, 0.2, 0.0, 1);
                    }
#endif
                }
            }
        }
    }
}

*/
