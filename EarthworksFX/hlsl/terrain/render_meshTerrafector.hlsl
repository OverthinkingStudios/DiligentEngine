//#include "terrainCommon.hlsli"
#include "../render_Common.hlsli"

#define CALLEDFROMHLSL
#include "materials.hlsli"


struct triVertex
{
    float3 pos;
    float alpha; // might have to be full float4 colour   but look at float16

    float2 uv; // might have to add second UV
    uint material;
    float buffer; // now 32
};


//SamplerState  SMP : register(s0);
StructuredBuffer<TF_material> materials;
StructuredBuffer<triVertex> vertexData;
StructuredBuffer<uint> indexData;


struct myTextures
{
    Texture2D<float4> T[4096];
};
ParameterBlock<myTextures>
gmyTextures;





cbuffer gConstantBuffer : register(b0)
{
    float4x4 viewproj;
    float overlayAlpha;
};



struct splineVSOut
{
    float4 pos : SV_POSITION;
    float3 posW : POSITION;
    float3 texCoords : TEXCOORD;
    nointerpolation uint4 flags : TEXCOORD1;
    float4 colour : COLOR;
};



splineVSOut vsMain(uint vId : SV_VertexID, uint iId : SV_InstanceID)
{
    splineVSOut vsOut;

    uint idx = (iId * 128 * 3) + vId;
        //uint idx = vId;
    triVertex V = vertexData[indexData[idx]];
        //triVertex V = vertexData[vId];

    vsOut.pos = mul(float4(V.pos, 1), viewproj);
    vsOut.posW = V.pos;
    vsOut.flags.y = V.material;
    vsOut.colour = V.alpha;
    vsOut.texCoords = float3(V.uv, 0);
    //vsOut.texCoords.y = 0;
    //if (vId%3 ==  2)
      //  vsOut.texCoords.y = 1;

    return vsOut;
}











PS_OUTPUT_Terrafector fixedMaterials(const uint material, const float2 uv, const float4 color, const uint4 flags, const float height)
{
    PS_OUTPUT_Terrafector output = (PS_OUTPUT_Terrafector) 0;

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
                output.Elevation.r = color.a * height;
                return output;
            }
            break;
        case MATERIAL_CURVATURE:
    {
                output.Elevation.a = 0;
                output.Elevation.r = 0.1 * cos((abs(uv.x)) * 1.57079632679);
                return output;
            }
            break;
    }

    return output;
}







PS_OUTPUT_Terrafector psMain(splineVSOut vIn) : SV_TARGET
{
    PS_OUTPUT_Terrafector output = (PS_OUTPUT_Terrafector) 0;
    uint material = vIn.flags.y;
    TF_material MAT = materials[material];

    if (material > 2030)
    {
        return fixedMaterials(material, vIn.texCoords.xz, vIn.colour, vIn.flags, vIn.posW.y);
    }



    _uv uv;
    solveUV(MAT, vIn.posW.xz, vIn.texCoords.xy, uv);
    float alpha = solveAlpha(MAT, uv, vIn.colour.r) * vIn.colour.a * overlayAlpha;



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

                PS_OUTPUT_Terrafector subOutput = (PS_OUTPUT_Terrafector) 0;
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


