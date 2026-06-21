

#include "terrainDefines.hlsli"
#include "terrainFunctions.hlsli"

#include "groundcover_defines.hlsli"
#include "groundcover_functions.hlsli"		


SamplerState linearSampler; // for noise adnd colours

cbuffer gConstants
{
    int numEcotopes;
    int debug;
    float pixelSize;
    int lod;

    float2 lowResOffset;
    float lowResSize;
    uint tileIndex;

    int2 tileXY;
    float2 padd2;

    float4 ect[12][5]; // 16		2560
    float4 texScales[12]; // texture size, displacement scale, pixSize, 0
    //uint4 totalDensity[12][16];
    //uint4 plantIndex[12][64];  // 2576	5648        // u16 better but not in constant buffer
};


Buffer<uint> plantIndex;
Buffer<uint> plantDensity;
//Buffer<float> totalDensity;



RWTexture2D gHeight : register(u0);
RWTexture2D<float3> gAlbedo : register(u1);

Texture2D<float> gLowresHgt : register(t0);
Texture2D<float3> gInPermanence : register(t1);
Texture2D<float4> gInEct_0 : register(t2); //.. t5
Texture2D<float4> gInEct_1 : register(t3);
Texture2D<float4> gInEct_2 : register(t4);
Texture2D<float4> gInEct_3 : register(t5);

//Texture2D<float4> gAlbedoAlpha[12] : register(t6);	  // these are dowbn to 12 so we can consider moving them closer togetehr again
//Texture2D<float> gDisplacement[12] : register(t18);
//Texture2D<float3> gPBR[12] : register(t30);
//Texture2D<float3> gECTNoise[12] : register(t42);

Texture2D<uint> gNoise : register(t54);

// for plants
RWStructuredBuffer<gpuTile> tiles;
RWStructuredBuffer<instance_PLANT> quad_instance;
RWStructuredBuffer<GC_feedback> feedback;


struct myTextures
{
    Texture2D<float4> T[256];
};
ParameterBlock<myTextures>
gmyTextures;



[numthreads(16, 16, 1)]
void main(int2 crd : SV_DispatchThreadId)
{

    gpuTile tile = tiles[tileIndex];
    float OH = gHeight[uint2(128, 128)].r - (tile.scale_1024 * 2048); // Its corner origin rather than middle
    tile.origin.y = OH;

    float3 permanence = gInPermanence[crd];
    if (any(permanence))
    {
    //if (permanence.b > 0.0)   {

        // calculate the normal
        uint2 crd_clamped = clamp(crd, 1, tile_numPixels - 2);
        float dx = gHeight[crd_clamped + int2(-1, 0)].r - gHeight[crd_clamped + int2(1, 0)].r;
        float dy = gHeight[crd_clamped + int2(0, -1)].r - gHeight[crd_clamped + int2(0, 1)].r;
        float3 n = normalize(float3(dx, 2.0 * pixelSize, dy));

        const float hgt = gHeight[crd].r;
        float2 lowResUV = ((crd - 4) * lowResSize) + lowResOffset;
        

        float weights[12];
        float ectWsum = 0;
        const float4 inEct1 = gInEct_0[crd];
        const float4 inEct2 = gInEct_1[crd];
        const float4 inEct3 = gInEct_2[crd];
        const float4 inEct4 = gInEct_3[crd];
        weights[0] = inEct1.r + 0.001;
        weights[1] = inEct1.g + 0.001;
        weights[2] = inEct1.b + 0.001;

        weights[3] = inEct2.r + 0.001;
        weights[4] = inEct2.g + 0.001;
        weights[5] = inEct2.b + 0.001;

        weights[6] = inEct3.r + 0.001;
        weights[7] = inEct3.g + 0.001;
        weights[8] = inEct3.b + 0.001;

        weights[9] = inEct4.r + 0.001;
        weights[10] = inEct4.g + 0.001;
        weights[11] = inEct4.b + 0.001;

        if (debug < numEcotopes)
        {
            //weights[debug] = 1;
        }



        int i;


        float2 World = (tileXY + ((crd - 4.0f) / 248.0f)) * pixelSize * 248.0f;


        float hgt_1 = 0;
        float hgt_2 = 0;

        if (permanence.b > 0.0 || (debug < numEcotopes))
        {

            for (i = 0; i < numEcotopes; i++) // ecotope weights calculation -----------------------------------------------------------------------------------
            {
                float2 UV = World / texScales[i].x;
                float2 UV2 = frac(World / 248.0f / 2 );
                float MIP = log2(pixelSize / 0.005f);
                
                //height
                weights[i] *= lerp(1, smoothstep(ect[i][0].g - ect[i][0].a, ect[i][0].g + ect[i][0].a, hgt) * smoothstep(ect[i][0].b + ect[i][0].a, ect[i][0].b - ect[i][0].a, hgt), ect[i][0].r);

                //concavity
                float rel = ect[i][1].b + hgt - gLowresHgt.SampleLevel(linearSampler, lowResUV, ect[i][1].a);
                weights[i] *= lerp(1, saturate((rel) / ect[i][1].g), ect[i][1].r);
                
                //flatness
                //weights[i] *= lerp(1, 1.0 - saturate(abs(rel) / ect[i][1].a), ect[i][1].b);
                
                //slope
                weights[i] *= lerp(1, smoothstep(ect[i][2].g - ect[i][2].a, ect[i][2].g + ect[i][2].a, (1 - n.y)) * smoothstep(ect[i][2].b + ect[i][2].a, ect[i][2].b - ect[i][2].a, (1 - n.y)), ect[i][2].r);

                // aspect
                weights[i] *= lerp(1, saturate(dot(n, float3(ect[i][3].g, ect[i][3].b, ect[i][3].a))), ect[i][3].r);
                //weights[i] *= lerp(1, saturate(n.x), ect[i][3].r);


                // texture
                float txWeight = 4 * gmyTextures.T[12 + i].SampleLevel(linearSampler, UV2, 0).r;
                weights[i] *= lerp(1, txWeight, ect[i][4].r);
                
                // also scale by the weighs, and offset around 0.5

                weights[i] *= weights[i]; // sqr to exagerate the result, play with this some more, the idea seems good
                
                ectWsum += weights[i];
            }
        }
        else
        {
            for (i = 0; i < numEcotopes; i++) // ecotope weights calculation -----------------------------------------------------------------------------------
            {
                ectWsum += weights[i];
            }
        }

        for (i = 0; i < numEcotopes; i++) // normalize the weights --------------------------------------------------------------------------------------
        {
            weights[i] /= ectWsum;
            if (debug == i)
            {
                gAlbedo[crd] = float3(lerp(0.0, 1.0, saturate((weights[i] * 2) - 1)), lerp(1.0, 0.0, abs(0.5 - weights[i]) * 2), lerp(0.3, 0.0, saturate(weights[i] * 2)));
            }
            
        }


        float3 col_new = 0;
        for (i = 0; i < numEcotopes; i++)
        {
            //float2 UV = frac(World / texScales[i].x);
            float2 UV = frac(World / 2);
            float MIP = log2(pixelSize / 0.001f); // There is a BUG in here I assume with the 0.005

            col_new += weights[i] * gmyTextures.T[i].SampleLevel(linearSampler, UV, MIP).rgb;
            float hgtTex = gmyTextures.T[i].SampleLevel(linearSampler, UV, MIP).g;
            hgt_2 += weights[i] * (hgtTex - 0.5) * texScales[i].g * 0.4;
        }




        //plants
        
        uint x_idx = (tileXY.x * 0.02356 + crd.x);
        uint y_idx = (tileXY.y * 3.1267 + crd.y);
        uint rnd = gNoise.Load(int3(x_idx & 0xff, y_idx & 0xff, 0));

        int sum = 0;
        int ecotopeForPlants = 200; // just large
        // FIXME, This shoudl be done with some interlocked add function on a wider tile bases but per plant
        // problem wiuth that is that its not repeatble, order of writes matter

        for (i = 0; i < numEcotopes; i++)
        {
            uint density = plantDensity.Load((i * 16) + lod);
            sum += (int) ((float) density * weights[i] * permanence.b);
            if ((sum * permanence.b) > rnd)
            {
                ecotopeForPlants = i;
                break;
            }
        }



        if (crd.x < 4)
            ecotopeForPlants = 200;
        if (crd.x > 252)
            ecotopeForPlants = 200;
        if (crd.y < 4)
            ecotopeForPlants = 200;
        if (crd.y > 252)
            ecotopeForPlants = 200;      //??? ??? I need to check we now only drop 3 at the far side, maybe not enough


        
        int offset = rnd & 0x3ff;       // 1024
        if (ecotopeForPlants < numEcotopes)
        {
            const int thisplantIndex = plantIndex.Load((ecotopeForPlants * 16 * 64) + (lod * 64) + (offset >> 4));
            if (thisplantIndex < 65535)
            {
                uint uHgt = (gHeight[crd].r - OH) / tile.scale_1024;
                uint slot = 0;
                InterlockedAdd(tiles[tileIndex].numQuads, 1, slot);
                feedback[0].plants_culled = slot;
                quad_instance[tileIndex * numQuadsPerTile + slot].xyz = pack_pos(crd.x - 4, crd.y - 4, uHgt, rnd); // FIXME - redelik seker die is verkeerd -ek dink ek pak 2 extra sub pixel bits 
                quad_instance[tileIndex * numQuadsPerTile + slot].s_r_idx = pack_SRTI(1, rnd, tileIndex, thisplantIndex);
            }
        }
        
        // write final colours
        if (debug < 0)
        {
            //gHeight[crd] = hgt + (hgt_1 + hgt_2) * (1 - permanence.r); //			*permanence.r;
            float l = saturate((lod - 7.f) / 10.f) * permanence.b;

            gAlbedo[crd] = lerp(gAlbedo[crd], col_new * 2, l); //permanence.g
  //          gAlbedo[crd] = gInEct[1][crd].rgb;
        }
    }

}
