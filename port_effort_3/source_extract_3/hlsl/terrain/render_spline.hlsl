#define CALLEDFROMHLSL
#include "materials.hlsli"

SamplerState gSmpPoint : register(s0);
SamplerState gSmpLinear : register(s1);
SamplerState gSmpAniso : register(s2);
SamplerState gSmpLinearClamp : register(s3);

StructuredBuffer<TF_material> materials;
StructuredBuffer<cubicDouble> splineData;
StructuredBuffer<bezierLayer> indexData;

struct myTextures
{
    Texture2D<float4> T[4096];
};
ParameterBlock<myTextures> gmyTextures;

cbuffer gConstantBuffer : register(b0)
{
	float4x4 viewproj;
    float alpha;
};


struct splineVSOut
{
    float4 pos : SV_POSITION;
	float3 posW : POSITION;
    float3 texCoords : TEXCOORD;
	uint4 flags : TEXCOORD1;
	float4 colour : COLOR;
};


struct cubicDouble
{
	float4 data[2][4];	//[center, outside][p0, p1, p2, p3]  //128 bytes
};
// [2][4] fully contained  float 128, u16 64

// If we save this as a left and a right matrix. and save a float4(t)  Its a single matrix multiply for the answer
// 200,000 beziers -> (128) 25Mb, lot but not too bad, and probably perfect if split
// expanded 32X -> 2xfloat4 (32) X 32 = (1024)
// SO this is SUPER interesting - Turns out beziers MAY NOT be the best way to do this at runtime It is only a 1:8 improvement on memory [32 splits],
// we could optimally sample on curvature, and use significantly less than 32 splits on average
// However 4 splits would be same data rate, so maybe still good

struct bezierLayer
{
	uint A;			// flags, material, index [4][14][16]			combine with rootindex in constant buffer
	uint B;			// w0, w1 [4][14][14]
};







// Cateljau
inline float4 quadratic_Casteljau(float t, float4 P0, float4 P1, float4 P2)
{
	float4 pA = lerp(P0, P1, t);
	float4 pB = lerp(P1, P2, t);
	
	return lerp(pA, pB, t);
}


inline float4 cubic_Casteljau(float t, float4 P0, float4 P1, float4 P2, float4 P3)
{
	float4 pA = lerp(P0, P1, t);
	float4 pB = lerp(P1, P2, t);
	float4 pC = lerp(P2, P3, t);
	
	float4 pD  = lerp(pA, pB, t);
	float4 pE  = lerp(pB, pC, t);
	
	return lerp(pD, pE, t);
}


#define outsideLine 	(indexData[ iId ].A >> 31) & 0x1
#define insideLine 		(indexData[ iId ].A >> 30) & 0x1
#define materialFlag  	(indexData[ iId ].A >> 17) & 0x7ff		    // 11 bits left 2048 materials
#define index  			indexData[ iId ].A & 0x1ffff

#define isStartOverlap 	(indexData[ iId ].B >> 31) & 0x1
#define isEndOverlap 	(indexData[ iId ].B >> 30) & 0x1
#define isQuad 			(indexData[ iId ].B >> 29) & 0x1
#define isFlipped 		(indexData[ iId ].B >> 28) & 0x1
#define isOverlap		(isStartOverlap) || (isEndOverlap)





splineVSOut vsMain(uint vId : SV_VertexID, uint iId : SV_InstanceID)
{
    splineVSOut output;
	
	float4 points[2];
    float t = (vId >> 1) / 64.0;
    const cubicDouble s0 = splineData[index];
	points[0] = cubic_Casteljau(t, s0.data[0][0], s0.data[0][1], s0.data[0][2], s0.data[0][3]);
	points[1] = cubic_Casteljau(t, s0.data[1][0], s0.data[1][1], s0.data[1][2], s0.data[1][3]);

    if (isQuad)
    {
        points[0] = lerp(s0.data[0][0], s0.data[0][3], t);
        points[1] = lerp(s0.data[1][0], s0.data[1][3], t);;
    }

    float3 perpendicular = normalize(points[1].xyz - points[0].xyz);

	if (vId & 0x1 ) {			// outside
        const float w1 = (indexData[iId].B & 0x3fff) * 0.002f - 16.0f;
        output.posW = points[outsideLine].xyz + w1 * perpendicular;
	} else {					// inside
        const float w0 = ((indexData[iId].B >> 14) & 0x3fff) * 0.002f - 16.0f;			// -32 .. 33.536m in mm resolution
        output.posW = points[insideLine].xyz + w0 * perpendicular;
	}
	
	output.texCoords = float3(1 - (vId & 0x1), points[outsideLine].w, t );
    if (isFlipped) output.texCoords.x *= -1;
	output.pos =  mul( float4(output.posW, 1), viewproj);
	output.flags.y = materialFlag;
    output.colour = 1;
    output.colour.a = alpha;

	return output;
}









float shevrons(float2 UV)
{
    if (UV.x < 0.02) return 0.9;
    if (UV.x > 0.98) return 0.9;

    float newU = abs(0.5 - UV.x) * 3;
    if (newU < 0.2) {
        float newV = frac(3 * (UV.y + newU * 0.3));
        if (newV > 0.93) {
            return 0.5;
        }
    }


    if (frac(UV.y * 32) > 0.5)
    {
        if (frac(UV.x * 2) > 0.5)return 0.04;
    }
    else
    {
        if (frac(UV.x * 2) <= 0.5)return 0.04;
    }

    return 0;
}


float4 dots(float2 UV)
{
    if (frac(UV.y * 5) < 0.25) return float4(1, 1, 1, 1);
    if (UV.x > 0.4 && UV.x < 0.6)return float4(1, 1, 1, 1);
    return float4(0, 0, 0, 0.5);
}



float4 editingMaterials(const uint material, const float2 uv, const float alpha, const uint4 flags)
{
    if (material >= 2045) {      // Curvature, slids and blends ignore them, do nothing in here
        return 0;
    }

    switch (material)
    {
    case MATERIAL_EDIT_SELECT:
    {
        return dots(uv * 3) * float4(1, 1, 0, alpha);
    }
    break;
    case MATERIAL_EDIT_GREEN:
    {
        float A = shevrons(abs(uv));
        //if (A < 0.01f) return float4(saturate((flags.z) / 150.0f - 0.5) * 10, saturate((flags.w) / 50.0f - 0.0), saturate((flags.w) / 50.0f - 0.0), 0.19 * alpha);
        return float4(0.0, 0.6, 0.0, A * alpha);
    }
    break;
    case MATERIAL_EDIT_BLUE:
    {
        float A = shevrons(uv);
        //if (A < 0.01) return float4(saturate((flags.z) / 150.0f - 0.5) * 10, saturate((flags.w) / 50.0f - 0.0), saturate((flags.w) / 50.0f - 0.0), 0.19 * alpha);
        return float4(0.0, 0.0, 2.0, A * alpha);
    }
    break;
    case MATERIAL_EDIT_WHITEDOT:
    {
        return dots(uv) * float4(1, 1, 1, alpha);
    }
    break;
    case MATERIAL_EDIT_REDDOT:
    {
        return dots(uv) * float4(1, 0, 0, alpha);
    }
    break;
    }

    return 0;
}



float4 solveColor(const TF_material _mat, const _uv uv, const float alpha)
{
    float4 colour;

    if (_mat.useColour)
    {
        float3 albedo = gmyTextures.T[_mat.baseAlbedoTexture].Sample(gSmpLinear, uv.object).rgb;
        float3 albedoDetail = gmyTextures.T[_mat.detailAlbedoTexture].Sample(gSmpLinear, uv.world).rgb;

        float3 A = lerp(albedo, 0.5, saturate(_mat.albedoBlend));
        float3 B = lerp(albedoDetail, 0.5, saturate(-_mat.albedoBlend));

        colour.rgb = clamp(0.04, 0.9, A * B * 4 * _mat.albedoScale);		// 0.04, 0.9 charcoal to fresh snow
        colour.a = alpha;
    }

    if (_mat.debugAlpha)
    {
        colour.rgb = float3(1, 0, 0);
    }

    colour.a = alpha;

    return colour;
}





float4 psMain(splineVSOut vIn) : SV_TARGET0
{
	float4 colour = vIn.colour;
	uint material = vIn.flags.y;
    TF_material MAT = materials[material];

    if (material > 2030) {
        return editingMaterials(material, vIn.texCoords.xz, vIn.colour.a, vIn.flags);
    }

	
	// uv's
    _uv uv;
    solveUV(MAT, vIn.posW.xz, vIn.texCoords.xy, uv);
    float alpha = solveAlpha(MAT, uv, vIn.colour.r) * vIn.colour.a;

    if (MAT.materialType == MATERIAL_TYPE_STANDARD)
    {
        return solveColor(MAT, uv, alpha);
    }

    if (MAT.materialType == MATERIAL_TYPE_MULTILAYER)
    {
        float4 colour = 0;
        for (uint i = 0; i < 8; i++)
        {
            uint subMat = MAT.subMaterials[i] & 0xffff;
            float alphaSubA = ((MAT.subMaterials[i] >> 16) & 0xff) / 255.0f;
            if (subMat > 0)
            {
                _uv uvSub;
                solveUV(materials[subMat], vIn.posW.xz, vIn.texCoords.xy, uvSub);
                float alphaSub = alphaSubA * alpha * solveAlpha(materials[subMat], uvSub, 1);
                float4 layerColour = solveColor(materials[subMat], uvSub, alphaSub);
                //return layerColour;
                // blend
                colour = lerp(colour, layerColour, layerColour.a);
            }
        }
        return colour;
    }

    return 0.8;
}


	
