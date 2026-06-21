

#include "terrainDefines.hlsli"
#include "terrainFunctions.hlsli"



RWTexture2D	gAlbedo;
RWTexture2D gNormal;
RWTexture2D gTranslucency;
RWTexture2D gpbr;



cbuffer gConstants
{
	uint step;
};


[numthreads(4, 4, 1)]
void main(int2 crd : SV_DispatchThreadId)
{

    float4 a = gAlbedo.Load(crd);
    if (!any(a.rgb))
    {
        float4 bA = 0;
        float4 bN = 0;
        float4 bT = 0;
        float cnt = 0;

        for (int y = -3; y <= 3; y++)
        {
            for (int x = -3; x <= 3; x++)
            {
                float4 c = gAlbedo.Load(crd + int2(x, y));
                if (any(c.rgb))
                {
                    bA += c;
                    bN += gNormal.Load(crd + int2(x, y));
                    bT += gTranslucency.Load(crd + int2(x, y));
                    cnt++;
                }
            }
        }

        if (cnt > 0)
        {
            gAlbedo[crd] = bA / cnt;
            gAlbedo[crd].a = 0.0;

            gNormal[crd] = bN / cnt;

            gTranslucency[crd] = bT / cnt;
        }
    }



/*

    a = (gNormal.Load(crd));//    -float4(0.5, 0.5, 1.0, 0));
    if (a.b == 1)
    {
        float4 b = 0;
        float cnt = 0;

        for (int y = -3; y <= 3; y++)
        {
            for (int x = -3; x <= 3; x++)
            {
                float4 c = gNormal.Load(crd + int2(x, y));
                if (c.b < 1)
                {
                    b += c;
                    cnt++;
                }
            }
        }

        if (cnt > 0)    gNormal[crd] = b / cnt;
        


    }




    a = gTranslucency.Load(crd);
    if (a.r == 1)
    {
        float4 b = 0;
        float cnt = 0;

        for (int y = -3; y <= 3; y++)
        {
            for (int x = -3; x <= 3; x++)
            {
                float4 c = gTranslucency.Load(crd + int2(x, y));
                if (c.r < 1)
                {
                    b += c;
                    cnt++;
                }
            }
        }

        if (cnt > 0)
        {
            gTranslucency[crd] = b / cnt;
        }
    }
*/
}
