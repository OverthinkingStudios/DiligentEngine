
#include "groundcover_defines.hlsli"
#include "groundcover_functions.hlsli"
#include "terrainDefines.hlsli"
#include "terrainFunctions.hlsli"
#include "../PBR.hlsli"
#include "gpuLights_functions.hlsli"
//#include "../render_Common.hlsli"

Texture2DArray gNormArray;
Texture2DArray gAlbedoArray;
Texture2DArray gPBRArray;
Texture2D gGISAlbedo;
//Texture2D gPreviousFrame;





cbuffer gConstantBuffer
{
    float4x4 view;
	float4x4 proj;
	float4x4 viewproj;
	float3 eye;
	//float pixSize;
	uint tileIndex;
};


cbuffer PerFrameCB
{
    bool gConstColor;
    float3 gAmbient;
	

	float gisOverlayStrength;
	int showGIS;
	float redStrength;
	float redScale;
	float4 gisBox;
	
	float redOffset;
	float3 padding;
	
};


float2 vectorRotate(float2 vec, float sinAngle, float cosAngle)
{
    float2 result;
    result.x = vec.x * cosAngle - vec.y * sinAngle;
    result.y = vec.y * cosAngle + vec.x * sinAngle;
    return result;
}

/*
This does allow for double indirection to save even more space and pack it all tighter, but I think we are below 64Mb already so why bother
*/





StructuredBuffer<Terrain_vertex> VB;
RWStructuredBuffer<gpuTile> 			tiles;

StructuredBuffer<tileLookupStruct>		tileLookup;



terrainVSOut vsMain(uint vId : SV_VertexID, uint iId : SV_InstanceID)
{
    terrainVSOut output;

    uint tileIDX = lu_Tile(tileLookup[iId >> 6]);
    uint numQuad = lu_Used(tileLookup[iId >> 6]);
    uint tileStart = lu_Index(tileLookup[iId >> 6], numVertPerTile, 64 * 3);
	uint blockID = (iId & 0x3f);    // bottom 64

	if (blockID < numQuad)
	{
        uint newID = tileStart + (blockID * 3) + vId;
		Terrain_vertex V = VB[ newID ];

        float x = (V.idx & 0x7f) * 2;
		float y = ((V.idx >> 7) & 0x7f) * 2;
		
		
		float4 position = float4(0, 0, 0, 1);
		position.xz = tiles[tileIDX].origin.xz + float2( x-tile_BorderPixels, y-tile_BorderPixels ) * tiles[tileIDX].scale_1024 * 4.0f * tile_toBorder;
		position.y = V.hgt;
		
		output.worldPos = position.xyz;
		output.eye = position.xyz - eye;
		output.pos =  mul( position, viewproj);
        /*
        float4 tmp = mul(position, view);
        //output.pos.xyz = tmp.x / length(tmp.xyz) * 2556;
        output.pos.xyz = normalize(tmp.xyz);
        float SGN = -sign(output.pos.z);
        //output.pos.z = 1 - output.pos.z;
        output.pos.z = length(tmp.xyz) / 40000 * SGN;
        output.pos.y *= 2550.f / 1080.f;
        //output.pos.xy *= 1.8;
        //output.pos.z = 0.1;
        output.pos.w = 1;
        */
        //output.pos.w = 0.010;
        /// FISHEY
        //if (output.pos.w > 0)
        /*
        {
        
            float2 XY = output.pos.xy / output.pos.w;
            float2 N = normalize(XY);
            float Dist = length(XY) * 0.5;
            float scale = 1;
            if (Dist < 1.9)
            {
            

                scale = Dist * 2  - pow(Dist, 1.8);
                output.pos.xy = N * scale * output.pos.w / 0.5;
            }
        }
        */
        //output.pos.x += 1000.4;// = sign(output.pos.x) * pow(output.pos.x, 2.0);
		output.texCoords = float3( x / tile_numPixels, y / tile_numPixels, tileIDX);
	}
    
	else
	{
		output.pos = float4(0, 0, 0, 0);		// should be enough to kill the triangle
		output.texCoords = float3(0, 0, 0);
	}
	
	return output;
	

}












void prepareMaterialLayerTerrain( in SH_Attributes Attr, inout layer_material MAT )
{
	MAT.diff 		= float4(Attr.diffuse, 1);
	MAT.spec 		= float4(Attr.specular, 1);
	MAT.f0 			= Attr.frenel;
	//MAT.roughness 	= 0.1;  IT IS PASSSED IN DO NOT OVERRIFE _ BAD BAD BAD
	MAT.AO 			= 1;
	MAT.cavity 		= 1;
	MAT.mask		= 1;
	
	MAT.N = Attr.N; 
	MAT.T = Attr.T;  
	MAT.B = Attr.B;
	MAT.R = 0;
	MAT.fresnel = 0;
	

	// reflect --------------------------------------------------------------------------------------------
	MAT.R = normalize(Attr.E - 2 * MAT.N * dot(Attr.E, MAT.N));	
	float NmeshoR = dot(MAT.R, Attr.N);
	if (NmeshoR > 0)
	{
		MAT.R = normalize(MAT.R - 2*MAT.N*NmeshoR);

		float dC = 1-NmeshoR;
		MAT.cavity *= dC;
	}
		
	
	// fixme, also adjust the refection towards the eye depending on roughness
	// block bacWARDS FACING Normals
	//float dForward = saturate(dot(-Attr.E, MAT.N)) + 0.0;
	//MAT.N = normalize(MAT.N + dForward * Attr.E);
		

	// FIXME calc frenel from my reflection vector rather later
	// using abs instead of saturate is interesting
	//MAT.fresnel = schlick(MAT.f0, (0.96 - saturate(dot(Attr.E, MAT.N))));
	MAT.diff.a = lerp( MAT.diff.a, 1, MAT.fresnel);
	MAT.diff.a *= MAT.mask;
	
	MAT.fresnel  = schlick_gloss( 0.01, 1 - saturate(dot(Attr.E, MAT.N)), 1.0 - MAT.roughness );
}



float4 psMain(terrainVSOut vIn) : SV_TARGET0
{
    
    
	SH_Attributes 	Attr;
	eval_mat		Values;
	layer_material	mat;
	float3 			diffuse = float3(0, 0, 0);
	float3 			specular = float3(0, 0, 0);
		
	
	//vIn.texCoords.xy *= frac(vIn.texCoords.xy * 4) / 4;
	//vIn.texCoords.z /= 2;
	
	//if (vIn.texCoords.x < tile_BorderPixels / 256.0) return float4(1, 0, 0, 1);
	//if (vIn.texCoords.x > (1.0 - tile_BorderPixels / 256.0)) return float4(0, 1, 0, 1);
	
	//if (vIn.texCoords.y < tile_BorderPixels / 256.0) return float4(1, 0, 0.1, 1);
	//if (vIn.texCoords.y > (1.0 - tile_BorderPixels / 256.0)) return float4(0, 1, 0.1, 1);
	
	//return float4(1, 0, 0, 0.5);

	
	// prepare shading attributes ------------------------------------------------------------------------------------------------
	Attr.P = vIn.worldPos;
	Attr.E = normalize(-vIn.eye);
    Attr.N_mesh = gNormArray.Sample(gSmpAniso, vIn.texCoords).rgb *2 - 1;
    Attr.N_mesh.y = sqrt(1 - (Attr.N_mesh.x * Attr.N_mesh.x) - (Attr.N_mesh.z * Attr.N_mesh.z));
	Attr.N = Attr.N_mesh;
	Attr.T = float3(1, 0, 0);
	Attr.B = float3(0, 0, 1);






	
	Attr.B = Attr.B -Attr.N * dot(Attr.B, Attr.N);						// Orthogonalize tangent frame, 
    Attr.T = cross(Attr.B, Attr.N);
	
	Attr.uv = vIn.texCoords.xy;
    Attr.diffuse = 0.0;//    gAlbedoArray.SampleLevel(gSmpAniso, vIn.texCoords, 0).rgb;
	
	float3 PBR = gPBRArray.SampleLevel(gSmpAniso, vIn.texCoords, 0).rgb;
	Attr.alpha = 1.0;
	Attr.specular = 0;
	Attr.frenel = PBR.r;
	Attr.mesh_AO = 1;
	Attr.mesh_Cavity = 1;
	Attr.CURVATURE = 0;
	
    mat.roughness = 0.001;// saturate(PBR.g);
	
	prepareMaterialLayerTerrain( Attr, mat );
	
	//return PBR.x + Attr.N_mesh.x;
	
	//mat.AO = PBR.b;
	
    mat.diff = gAlbedoArray.Sample(gSmpAniso, vIn.texCoords);
   // mat.diff.rgb = 0.5;
    //float3 light = saturate(   dot(Attr.N_mesh, float3(0.5, 0.9, 0.0)) * float3(2.02, 1.53, 1.05)   + float3(0.07, 0.1, 0.2)     );
    float3 light = float3(0.04, 0.08, 0.15) + saturate(dot(Attr.N_mesh, normalize(float3(0.5, 0.2, 0.0))));
	
	if (showGIS)
	{
		float2 GIS_UV = (vIn.worldPos.xz - gisBox.xy) / gisBox.z;
		if (GIS_UV.x > 0 && GIS_UV.x < 1 && GIS_UV.y > 0 && GIS_UV.y < 1)
		{
            float4 GIS = gGISAlbedo.Sample(gSmpAniso, GIS_UV) * float4(1, 0.8, 0.8, 1);
			mat.diff = lerp(mat.diff, GIS, gisOverlayStrength);
		}
	}
	
	
	// add shadows 
/*
	float2 GIS_UV = (vIn.worldPos.xz - float2(-6000.0, -6000.0)) / 12000.0;
	if (GIS_UV.x > 0 && GIS_UV.x < 1 && GIS_UV.y > 0 && GIS_UV.y < 1)
	{
		Attr.diffuse = gGISAlbedo.Sample(gSmpAniso, GIS_UV).rgb; 
	}
	
	Attr.diffuse.rg = GIS_UV;
*/
	//lightIBL( Attr, mat, diffuse, specular );						// 	170us
    float S = pow(shadow(vIn.worldPos, 0), 0.25);
    //S *= S;
    float4 sunColor = sunLight(vIn.worldPos * 0.001);   // This ome is quote esxpensive but hardish to avoid

    // also very expensive
    lightLayer(Attr, mat, sunDirection, sunColor.rgb * S, diffuse, specular); //	40us  - redelik vinnig maar tel op oor ligte - if() is worth it
	//diffuse += float3(0.03, 0.04, 0.05) * 0.3;

    diffuse *= (1 - mat.fresnel);
	specular *= mat.fresnel;

    //diffuse += float3(0.01, 0.02, 0.04) * 1.32 * mat.diff.rgb;    // FUCK ambient
	float3 colour = diffuse + specular * 1.0;


    // Completely custom
    // -------------------------------------------------------------------------------------------------------------
    float3 albedo = mat.diff.rgb;
    float3 h = normalize(-normalize(sunDirection) - normalize(vIn.eye));
    float3 n = gNormArray.Sample(gSmpAniso, vIn.texCoords).rgb * 2 - 1;
    n.y = sqrt(1 - (n.x * n.x) - (n.z * n.z));
    
    float3 n_soft = normalize(n - sunDirection * 0.0001302);
    float ndoth = pow(saturate(dot(n, h)), 2);
    //colour = sunColor.rgb * saturate(ndoth);
    colour = S * sunColor.rgb * saturate(dot(n_soft, -sunDirection));// *mat.diff.rgb;

    float TRANS = ndoth;
    float gray = dot(albedo, float3(0.299, 0.587, 0.114));
    float3 T = albedo * albedo / gray * 0.5;
    

    colour += float3(0.3, 0.5, 0.9) * 0.05;

    colour*= albedo;


    float3 R = reflect(-normalize(vIn.eye), n);
    float transClip = saturate((albedo.g - albedo.r) * 30) * 1;
    colour += transClip * S * sunColor.rgb * pow(saturate(dot(R, sunDirection)), 4) * saturate(dot(n_soft, -sunDirection)) * T;


    // TRY 2
    // ------------------------------------------------------------------------------------------------------------------
    {
        float softness = 0.2;
        float3 n = gNormArray.Sample(gSmpAniso, vIn.texCoords).rgb * 2 - 1;
        float EOS = dot(normalize(vIn.eye), normalize(sunDirection));
        float EOSnorm = EOS * 0.5 + 0.5;
        float NOS = dot(n, -sunDirection);
        float EON = dot(n, -normalize(vIn.eye));

        //float sun = pow(EOSnorm, 1.3);
        //float trans = pow(1 - EOSnorm, 1.3);
        //float shade = 1 - sun - trans;

        float shade = 0.25 + 0.75 * pow((1.f - saturate(NOS)), 2.5f);
        shade *= 1.f - 0.25f * pow(saturate(EOS), 100.f);
        shade *= 1.f - 0.85f * pow(1.f - EON, 5.f) ;

        //float S2 

        float split = EOS * 0.5f + 0.5f;
        float sun = (1 - shade) * split * 0.8;
        float trans = (1 - shade) * (1 - split) * 0.8;

        float sunlightSpread = lerp(1, NOS, pow(1 - abs(EOS), 0.2));
        colour = sunlightSpread;

        float transClip = saturate((albedo.g - albedo.r) * 10) * 1;

        //colour.r = sun * sunlightSpread;
        //colour.g = trans * sunlightSpread * transClip;
        //colour.b = shade;
        //S = 1;

        colour = S * sunColor.rgb * sun;// *sunlightSpread;
        colour += float3(0.3, 0.5, 0.9) * 0.03;
        colour *= albedo;

        float gray = dot(albedo, float3(0.299, 0.587, 0.114));
        float3 T = albedo * albedo / gray;
        colour += S * sunColor.rgb * T * trans;

        float3 colour_hard = (saturate(NOS) * S * sunColor.rgb + (float3(0.3, 0.5, 0.9) * 0.03)) * albedo;
        colour = lerp(colour, colour_hard, 1 - transClip);
    }



    //colour = ndoth * 0.1;
	/*
    float edgeBlend = smoothstep(0, 0.01, -dot(Attr.E, Attr.N));

    if (edgeBlend > 0.1)
    {
        //colour = float3(edgeBlend, 0, 0);
        
        float2 buffSize;
        gPreviousFrame.GetDimensions(buffSize.x, buffSize.y);
        float2 uv = vIn.pos.xy / (buffSize * 2.f);
        float3 prev = saturate(gPreviousFrame.Sample(gSmpAniso, uv).rgb);
        colour = lerp(colour, prev, edgeBlend);

    }
*/


    //colour.rgb = mat.diff.rgb * light;

    // MARK the steep slopes
    
    float slopeMark = saturate((1.0 - Attr.N_mesh.y - redOffset) * redScale);

    if (slopeMark > 0.01)
    {
        colour.r = lerp(colour.r, slopeMark, redStrength);
    }

    // also plenty expensive, can we do this per vertex as an option?
	// Now for my atmospeher code --------------------------------------------------------------------------------------------------------
	{

		//far one
		float3 atmosphereUV;
        atmosphereUV.xy = vIn.pos.xy / screenSize;
		atmosphereUV.z = log( length( vIn.eye.xyz ) / fog_far_Start ) * fog_far_log_F + fog_far_one_over_k;
        atmosphereUV.z = max(0.01, atmosphereUV.z);
        atmosphereUV.z = min(0.99, atmosphereUV.z);
		colour.rgb *= gAtmosphereOutscatter.Sample( gSmpLinearClamp, atmosphereUV ).rgb;

        float4 inscatter = textureBicubic(atmosphereUV);
        colour.rgb += inscatter.rgb;
	}
		

    

    //colour.rgb = gNormArray.Sample(gSmpAniso, vIn.texCoords).rgb;
    //colour.g = 1 - Attr.N_mesh.g;
	return float4(colour, 1);
}









