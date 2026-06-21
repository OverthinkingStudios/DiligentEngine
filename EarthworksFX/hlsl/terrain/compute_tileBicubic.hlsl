


/*
	Bicubic interpolates the incoming elevation data to produce a smoother output to work from
	This is nessesary when the base data is 30m SRTM, but fast enough to leave it on for lidar
	
	try catmul clarke as well
	https://gist.github.com/TheRealMJP/c83b8c0f46b63f3a88a5986f4fa982b1
*/



#include "terrainDefines.hlsli"


SamplerState linearSampler;
Texture2D<float> gInput;
RWTexture2D<float> gOutput;
RWTexture2D<float4> gDebug;
//RWTexture2D<float> gOuthgt_TEMPTILLTR;		// just here to replicate hieghts untill I add the rende to texture pass

Texture2D<float4> gInputAlbedo;
RWTexture2D<float4> gOutputAlbedo;
RWTexture2D<float4> gOutputPermanence;

cbuffer gConstants
{
	float2 offset;
	float2 size;
	float hgt_offset;
	float hgt_scale;
    int isHeight;
};





float4 cubic(float v) {
    float4 n = float4(1.0, 2.0, 3.0, 4.0) - v;
    float4 s = n * n * n;
    float x = s.x;
    float y = s.y - 4.0 * s.x;
    float z = s.z - 4.0 * s.y + 6.0 * s.x;
    float w = 6.0 - x - y - z;
    return float4(x, y, z, w) * (1.0 / 6.0);
}

[numthreads(tile_cs_ThreadSize, tile_cs_ThreadSize, 1)]
void main(int2 crd : SV_DispatchThreadId)
{
	float2 texSize;
	gInput.GetDimensions(texSize.x, texSize.y);
	float2 invTexSize = 1.0 / texSize;


	

    if (isHeight == 1)
    {
        float2 iTc = ((crd - 4.0) * size + offset) * texSize - 0.5;
	//float2 tc = floor(iTc-0.5) + 0.5;											// This term is very diffirent in pixel shader   floor(iTc - 0.5) + 0.5; for alf pixel offsets
    //float2 tc = floor(iTc);
    //iTc.x -= 1;
        
        float2 f = frac(iTc); // iTc - tc;
        iTc -= f;

        float4 xcubic = cubic(f.x);
        float4 ycubic = cubic(f.y);

        float4 c = iTc.xxyy + float2(-0.5, +1.5).xyxy;

        float4 s = float4(xcubic.xz + xcubic.yw, ycubic.xz + ycubic.yw);
        float4 offsetB = c + float4(xcubic.yw, ycubic.yw) / s;

        offsetB *= invTexSize.xxyy;

        float sample0 = gInput.SampleLevel(linearSampler, offsetB.xz, 0).r;
        float sample1 = gInput.SampleLevel(linearSampler, offsetB.yz, 0).r;
        float sample2 = gInput.SampleLevel(linearSampler, offsetB.xw, 0).r;
        float sample3 = gInput.SampleLevel(linearSampler, offsetB.yw, 0).r;


        float sx = s.x / (s.x + s.y);
        float sy = s.z / (s.z + s.w);

        float H = lerp(lerp(sample3, sample2, sx), lerp(sample1, sample0, sx), sy);


        gOutput[crd] = (H * hgt_scale) + hgt_offset;
    }
    else
    {
        float2 iTc = offset + ((crd - 4.0) * size);
        //iTc = frac(crd / 256);
        //iTc.x = crd.x / 512;
        float4 col = gInputAlbedo.SampleLevel(linearSampler, iTc, 0);
        //col *= 0.9;
        col.r = pow(col.r, 0.8);
        col.g = pow(col.g, 0.95);
        col.b = pow(col.b, 0.8);
        //col = pow(col, 0.8);
        col *= 0.9;
        col = max(0.03, col);
        gOutputAlbedo[crd] = col;
        //gOutputAlbedo[crd].rg = iTc;
        //gOutputAlbedo[crd].b = 0;

    }

}











/*
// running this in double presicion has amazing results, proving the problem is presision, but its 10X slower on my 980 card
float CubicHermite(float A, float B, float C, float D, float3 t) {
    float a = -A + (3.0 * B) - (3.0 * C) + D;
    float b = A * 2.0 - (5.0 * B) + 4.0 * C - D;
    float c = -A + C;
    float d = B * 2.0;

    return (a * t.z + b * t.y + c * t.x + d) * 0.5;
}


[numthreads(tile_cs_ThreadSize, tile_cs_ThreadSize, 1)]
void main(uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadId)
{
    uint2 crd = (groupId.xy * tile_cs_ThreadSize) + groupThreadId.xy;

    float H[4][4];
    float2 texSize;
    gInput.GetDimensions(texSize.x, texSize.y);

    float2 F = frac((crd * size + frac(offset)) * texSize - 1.5);	// Using frac(offset) improves float presision
    int3 UV0 = int3(floor((crd * size + offset) * texSize - 1.5), 0);
    for (int y=0; y<4; y++) {
        for (int x = 0; x < 4; x++) {
            H[y][x] = gInput.Load(UV0 + int3(x, y, 0)) * hgt_scale;
        }
    }

    float3 t = F.x;
    t.y *= F.x;
    t.z = t.y * F.x;
    float CP0X = CubicHermite(H[0][0], H[0][1], H[0][2], H[0][3], t);
    float CP1X = CubicHermite(H[1][0], H[1][1], H[1][2], H[1][3], t);
    float CP2X = CubicHermite(H[2][0], H[2][1], H[2][2], H[2][3], t);
    float CP3X = CubicHermite(H[3][0], H[3][1], H[3][2], H[3][3], t);

    t = F.y;
    t.y *= F.y;
    t.z = t.y * F.y;
    float Hgt = CubicHermite(CP0X, CP1X, CP2X, CP3X, t)  + hgt_offset;

    gOutput[crd] = Hgt;
    gOuthgt_TEMPTILLTR[crd] = Hgt;

}
*/

