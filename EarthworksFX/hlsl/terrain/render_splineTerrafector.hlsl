
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
    uint startOffset;
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
	float4 data[2][4];			//[middle, outside][p0, p1, p2, p3]
};


struct bezierLayer
{
	uint A;						// flags, material, index [3][13][16]			combine with rootindex in constant buffer
	uint B;						// w0, w1 [16][16]
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


#define outsideLine 	(indexData[ iId + startOffset ].A >> 31) & 0x1
#define insideLine 		(indexData[ iId + startOffset ].A >> 30) & 0x1
#define materialFlag  	(indexData[ iId + startOffset ].A >> 17) & 0x7ff		    // 11 bits left 2048 materials
#define index  			indexData[ iId + startOffset ].A & 0x1ffff

#define isStartOverlap 	(indexData[ iId + startOffset ].B >> 31) & 0x1
#define isEndOverlap 	(indexData[ iId + startOffset ].B >> 30) & 0x1
#define isQuad 			(indexData[ iId + startOffset ].B >> 29) & 0x1
#define isFlipped 		(indexData[ iId + startOffset ].B >> 28) & 0x1
#define isOverlap		(isStartOverlap) || (isEndOverlap)



splineVSOut vsMain(uint vId : SV_VertexID, uint iId : SV_InstanceID)
{
    splineVSOut output;
    output.posW = 0;

    float4 points[2];
    float t = (vId >> 1) / 64.0;
    const cubicDouble s0 = splineData[index];
    points[0] = cubic_Casteljau(t, s0.data[0][0], s0.data[0][1], s0.data[0][2], s0.data[0][3]);
    points[1] = cubic_Casteljau(t, s0.data[1][0], s0.data[1][1], s0.data[1][2], s0.data[1][3]);

    float3 perpendicular = normalize(points[1].xyz - points[0].xyz);

    float overlapAlpha = 1.0f;
    if (isOverlap) overlapAlpha = 0.0f;
    if (isStartOverlap)
    {
        overlapAlpha = saturate(1 - ((vId >> 1) * 0.2));
    }

    if (isEndOverlap && vId > 64)
    {
        overlapAlpha = saturate(1 - ((64 - (vId >> 1)) * 0.2));
    }

    if (vId & 0x1) {			// outside
        const float w1 = (indexData[iId + startOffset].B & 0x3fff) * 0.002f - 16.0f;
        output.posW = points[outsideLine].xyz + w1 * perpendicular;
    }
    else {					// inside
        const float w0 = ((indexData[iId + startOffset].B >> 14) & 0x3fff) * 0.002f - 16.0f;			// -32 .. 33.536m in mm resolution
        output.posW = points[insideLine].xyz + w0 * perpendicular;
    }

    output.texCoords = float3(1 - (vId & 0x1), points[outsideLine].w, t);
    if (isFlipped) output.texCoords.x *= -1;
    output.pos = mul(float4(output.posW, 1), viewproj);
    output.flags.y = materialFlag;
    output.colour.r = 1- abs(output.texCoords.x);
    output.colour.a = overlapAlpha;

    return output;
}

	
	


	
	
	
	
	
	
	




PS_OUTPUT_Terrafector fixedMaterials(const uint material, const float2 uv, const float4 color, const uint4 flags, const float height)
{
    PS_OUTPUT_Terrafector output = (PS_OUTPUT_Terrafector)0;
    
    switch (material)
    {
    case MATERIAL_BLEND:
    {
        output.Elevation.a = color.a * smoothstep(0, 1, color.r);
        output.Elevation.r = height * output.Elevation.a;
        return output;
    }
    break;
    case MATERIAL_SOLID:
    {
        output.Elevation.a = color.a;
        output.Elevation.r = height * color.a;
        return output;
    }
    break;
    case MATERIAL_CURVATURE:
    {
        output.Elevation.a = 0;
                output.Elevation.r = 0.1 * pow(cos((abs(uv.x)) * 1.57079632679), 0.85);
        return output;
    }
    break;
    }

    return output;
}







PS_OUTPUT_Terrafector psMain(splineVSOut vIn)  : SV_TARGET
{
    PS_OUTPUT_Terrafector output = (PS_OUTPUT_Terrafector)0;
    uint material = vIn.flags.y;
    TF_material MAT =  materials[material];

    if (material > 2030) {
        return fixedMaterials(material, vIn.texCoords.xz, vIn.colour, vIn.flags, vIn.posW.y);
    }



    _uv uv;
    solveUV(MAT, vIn.posW.xz, vIn.texCoords.xy, uv);
    float alpha = solveAlpha(MAT, uv, vIn.colour.r) * vIn.colour.a;
	
    
    if (MAT.materialType == MATERIAL_TYPE_STANDARD)
    {
        solveElevationColour(output, MAT, uv, alpha, vIn.posW.y);
        return output;
    }

    if (MAT.materialType == MATERIAL_TYPE_MULTILAYER)
    {
        for (uint i = 0; i < 8; i++)
        {
            uint subMat = MAT.subMaterials[i] & 0xffff;
            float alphaSubA = ((MAT.subMaterials[i] >> 16) & 0xff) / 255.0f;
            if (subMat > 0)
            {
                _uv uvSub;
                solveUV(materials[subMat], vIn.posW.xz, vIn.texCoords.xy, uvSub);
                float alphaSub = alphaSubA * alpha * solveAlpha(materials[subMat], uvSub, 1);

                PS_OUTPUT_Terrafector subOutput = (PS_OUTPUT_Terrafector)0;
                solveElevationColour(subOutput, materials[subMat], uvSub, alphaSub, vIn.posW.y);

                // blend
                output.Elevation = lerp(output.Elevation, subOutput.Elevation, subOutput.Elevation.a);
                output.Albedo = lerp(output.Albedo, float4(subOutput.Albedo.rgb, 1), subOutput.Albedo.a);
                output.PBR = lerp(output.PBR, subOutput.PBR, subOutput.PBR.a);
                output.Alpha = lerp(output.Alpha, subOutput.Alpha, subOutput.Alpha.a);
                output.Ecotope1 = lerp(output.Ecotope1, subOutput.Ecotope1, subOutput.Ecotope1.a);
                output.Ecotope2 = lerp(output.Ecotope2, subOutput.Ecotope2, subOutput.Ecotope2.a);
                output.Ecotope3 = lerp(output.Ecotope3, subOutput.Ecotope3, subOutput.Ecotope3.a);
                output.Ecotope4 = lerp(output.Ecotope4, subOutput.Ecotope4, subOutput.Ecotope4.a);

                
            }
        }
        return output;
    }

    
	
	return output;
}
	

