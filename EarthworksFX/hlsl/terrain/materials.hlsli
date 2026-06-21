

#define MATERIAL_TYPE_STANDARD 0
#define MATERIAL_TYPE_MULTILAYER 1
#define MATERIAL_TYPE_PUDDLE 2
#define MATERIAL_TYPE_RUBBER 3

struct TF_material
{
		int	materialType;
		float2  uvScale;
		float	worldSize;
		
		float2  uvScaleClampAlpha;
		uint	debugAlpha;
		float	uvRotation;

		float	useAlpha;
		float	vertexAlphaScale;
		float	baseAlphaScale;
		float	detailAlphaScale;

		uint	baseAlphaTexture;
		float	baseAlphaBrightness;
		float	baseAlphaContrast;
		uint	baseAlphaClampU;

		uint	detailAlphaTexture;
		float	detailAlphaBrightness;
		float	detailAlphaContrast;
		float	useAbsoluteElevation;

		uint	useElevation;
		float	useVertexY;
		float	YOffset;
		uint	baseElevationTexture;

		float	baseElevationScale;
		float	baseElevationOffset;
		uint	detailElevationTexture;
		float	detailElevationScale;

		float	detailElevationOffset;
		float3	buf_____02;

		int		standardMaterialType;
		float3	buf_____03;

		uint	useColour;
		uint	baseAlbedoTexture;
		float	albedoBlend;
		uint	detailAlbedoTexture;

		float3	albedoScale;
		float	uvWorldRotation;

		uint	baseRoughnessTexture;
		float	roughnessBlend;
		uint	detailRoughnessTexture;
		float	roughnessScale;

		float	porosity;
		float3	buf_____05;

		uint	useEcotopes;
		float	permanenceElevation;
		float	permanenceColour;
		float	permanenceEcotopes;

		float	cullA;
		float	cullB;
		float	cullC;
		uint	ecotopeTexture;

        uint    subMaterials[8];

		float4	ecotopeMasks[15];
};


struct _uv
{
    float2 object; // object space
    float2 side; // modified object for solving sides of beziers
    float2 world; // world space scale only - about 1mm presision at 16km should be ok for now
};




#define MATERIAL_EDIT_SELECT 2040
#define MATERIAL_EDIT_GREEN 2041
#define MATERIAL_EDIT_BLUE 2042
#define MATERIAL_EDIT_WHITEDOT 2043
#define MATERIAL_EDIT_REDDOT 2044


#define MATERIAL_BLEND 2045
#define MATERIAL_SOLID 2046
#define MATERIAL_CURVATURE 2047


#if defined(CALLEDFROMHLSL)

void solveUV(const TF_material _mat, const float2 worldPos, const float2 uvIn, inout _uv uv)
{
    uv.side = uvIn;

    float2x2 rotate;
    sincos(_mat.uvRotation, rotate[0][1], rotate[0][0]);
    rotate[1][0] = -rotate[0][1];
    rotate[1][1] = rotate[0][0];
    uv.object = mul(rotate, uv.side * _mat.uvScale);

    sincos(_mat.uvWorldRotation, rotate[0][0], rotate[0][1]);
    rotate[1][0] = -rotate[0][1];
    rotate[1][1] = rotate[0][0];
    uv.world = mul(rotate, worldPos / _mat.worldSize);
}


float solveAlpha(const TF_material _mat, _uv uv, float _vertexAlpha)
{
    float alpha = 1;

    if (_mat.useAlpha)
    {
        if (_mat.baseAlphaClampU)
        {
            float sideAlpha = smoothstep(0, _mat.uvScaleClampAlpha.x, abs(uv.side.x)) * (1 - smoothstep(_mat.uvScaleClampAlpha.y, 1, abs(uv.side.x)));


            //sideAlpha += range * (alphaDetail-0.5);

            if (_mat.baseAlphaScale > 0.05)
            {
                uv.side.xy *= _mat.uvScale;    // without rotate
                uv.side.x = clamp(sideAlpha, 0.1, 0.9);
                float alphaBase = gmyTextures.T[_mat.baseAlphaTexture].SampleLevel (gSmpLinear, uv.side, 3).r;

                sideAlpha = lerp(1, saturate((alphaBase + _mat.baseAlphaBrightness) * _mat.baseAlphaContrast), _mat.baseAlphaScale);
            }

            //float range = min(1 - sideAlpha, sideAlpha);
            float alphaDetail = gmyTextures.T[_mat.detailAlphaTexture].Sample(gSmpLinear, uv.world).r;
            alpha *= lerp(1, saturate((sideAlpha + alphaDetail + _mat.detailAlphaBrightness) * _mat.detailAlphaContrast), _mat.detailAlphaScale);

            alpha *= sideAlpha;
        }
        else
        {
            float alphaBase = gmyTextures.T[_mat.baseAlphaTexture].Sample(gSmpLinear, uv.object).r;
            float alphaDetail = gmyTextures.T[_mat.detailAlphaTexture].Sample(gSmpLinear, uv.world).r;

            alpha *= lerp(1, smoothstep(0, 1, _vertexAlpha), _mat.vertexAlphaScale);
            alpha *= lerp(1, saturate((alphaBase + _mat.baseAlphaBrightness) * _mat.baseAlphaContrast), _mat.baseAlphaScale);
            alpha *= lerp(1, saturate((alphaDetail + _mat.detailAlphaBrightness) * _mat.detailAlphaContrast), _mat.detailAlphaScale);
        
        }
    }

    return saturate(alpha);

}




struct PS_OUTPUT_Terrafector
{
    float4 Elevation: SV_Target0;
    float4 Albedo: SV_Target1;
    float4 PBR: SV_Target2;
    float4 Alpha: SV_Target3;
    float4 Ecotope1: SV_Target4;
    float4 Ecotope2: SV_Target5;
    float4 Ecotope3: SV_Target6;
    float4 Ecotope4: SV_Target7;
};



void solveElevationColour(inout PS_OUTPUT_Terrafector output, const TF_material MAT, const _uv uv, const float alpha, const float vertex_heigth)
{
    
    // Elevation
    output.Elevation = 0;
    if (MAT.useElevation)
    {
        
        output.Elevation.r += MAT.YOffset;
        output.Elevation.r += (gmyTextures.T[MAT.baseElevationTexture].Sample(gSmpLinear, uv.object).r - MAT.baseElevationOffset) * MAT.baseElevationScale;
        output.Elevation.r += (gmyTextures.T[MAT.detailElevationTexture].Sample(gSmpLinear, uv.world).r - MAT.detailElevationOffset) * MAT.detailElevationScale;
        output.Elevation.r *= alpha;

        if (MAT.useAbsoluteElevation > 0.5)
        {
            if (MAT.useVertexY)
            {
                output.Elevation.r += vertex_heigth * alpha;
            }
            output.Elevation.a = alpha;
            
        }
        else {
            //output.Elevation.r *= alpha;
            output.Elevation.a = 0;  // since that causes OneMinusSrcAlpha to 1
        }

    }





    if (MAT.useColour)
    {
        float3 albedo = gmyTextures.T[MAT.baseAlbedoTexture].Sample(gSmpLinear, uv.object).rgb;
        float3 albedoDetail = gmyTextures.T[MAT.detailAlbedoTexture].Sample(gSmpLinear, uv.world).rgb;

        float3 A = lerp(albedo, 0.5, saturate(MAT.albedoBlend));
        float3 B = lerp(albedoDetail, 0.5, saturate(-MAT.albedoBlend));

        output.Albedo.rgb = clamp(0.04, 0.9, A * B * 4 * MAT.albedoScale);		// 0.04, 0.9 charcoal to fresh snow
        output.Albedo.a = alpha;


        float baseRoughness = gmyTextures.T[MAT.baseRoughnessTexture].Sample(gSmpLinear, uv.object).r;
        float detailRoughness = gmyTextures.T[MAT.detailRoughnessTexture].Sample(gSmpLinear, uv.world).r;

        float rA = lerp(baseRoughness, 0.5, saturate(MAT.roughnessBlend));
        float rB = lerp(detailRoughness, 0.5, saturate(-MAT.roughnessBlend));

        output.PBR.rgb = saturate(pow(rA * rB * 2, MAT.roughnessScale));
        output.PBR.a = alpha;
    }
    else
    {
        output.PBR.a = 0;
    }


    // FIXME this has to deal with alpha properly
    output.Alpha = float4(1-MAT.permanenceElevation, 1-MAT.permanenceColour, 1-MAT.permanenceEcotopes, alpha);
    //output.Alpha = float4(1, 1, 1, 0);

    if (MAT.useEcotopes)
    {
        float3 ecotopeTex = gmyTextures.T[MAT.ecotopeTexture].Sample(gSmpLinear, uv.object).rgb;
        output.Ecotope1 = float4(dot(MAT.ecotopeMasks[0].rgb, ecotopeTex), dot(MAT.ecotopeMasks[1].rgb, ecotopeTex), dot(MAT.ecotopeMasks[2].rgb, ecotopeTex), alpha * MAT.ecotopeMasks[0].a);
        output.Ecotope2 = float4(dot(MAT.ecotopeMasks[3].rgb, ecotopeTex), dot(MAT.ecotopeMasks[4].rgb, ecotopeTex), dot(MAT.ecotopeMasks[5].rgb, ecotopeTex), alpha * MAT.ecotopeMasks[3].a);
        output.Ecotope3 = float4(dot(MAT.ecotopeMasks[6].rgb, ecotopeTex), dot(MAT.ecotopeMasks[7].rgb, ecotopeTex), dot(MAT.ecotopeMasks[8].rgb, ecotopeTex), alpha * MAT.ecotopeMasks[6].a);
        output.Ecotope4 = float4(dot(MAT.ecotopeMasks[9].rgb, ecotopeTex), dot(MAT.ecotopeMasks[10].rgb, ecotopeTex), dot(MAT.ecotopeMasks[11].rgb, ecotopeTex), alpha * MAT.ecotopeMasks[9].a);
    }
    else
    {
        output.Ecotope1 = float4(0, 0, 0, alpha);
        output.Ecotope2 = float4(0, 0, 0, alpha);
        output.Ecotope3 = float4(0, 0, 0, alpha);
        output.Ecotope4 = float4(0, 0, 0, alpha);
    }
}




#endif
