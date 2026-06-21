
#include "groundcover_defines.hlsli"
#include "terrainDefines.hlsli"
#include "terrainFunctions.hlsli"
#include "../PBR.hlsli"
#include "gpuLights_functions.hlsli"


Texture2DArray gNormArray;
Texture2DArray gAlbedoArray;
Texture2DArray gPBRArray;
Texture2D gGISAlbedo;
Texture2D gHalfBuffer;




float4 cubic(float v)
{
    float4 n = float4(1.0, 2.0, 3.0, 4.0) - v;
    float4 s = n * n * n;
    float x = s.x;
    float y = s.y - 4.0 * s.x;
    float z = s.z - 4.0 * s.y + 6.0 * s.x;
    float w = 6.0 - x - y - z;
    return float4(x, y, z, w) * (1.0 / 6.0);
}

float4 textureBicubic(float3 texCoords)
{
    float4 texSize;
    gAtmosphereInscatter.GetDimensions(0, texSize.x, texSize.y, texSize.z, texSize.w);
    float2 invTexSize = 1.0 / texSize.xy;
   
    texCoords.xy = texCoords.xy * texSize.xy - 0.5;

   
    float2 fxy = frac(texCoords.xy);
    texCoords.xy -= fxy;

    float4 xcubic = cubic(fxy.x);
    float4 ycubic = cubic(fxy.y);

    float4 c = texCoords.xxyy + float2(-0.5, +1.5).xyxy;
    
    float4 s = float4(xcubic.xz + xcubic.yw, ycubic.xz + ycubic.yw);
    float4 offset = c + float4(xcubic.yw, ycubic.yw) / s;
    
    offset *= invTexSize.xxyy;

    
    float4 sample0 = gAtmosphereInscatter.Sample(gSmpLinearClamp, float3(offset.xz, texCoords.z));
    float4 sample1 = gAtmosphereInscatter.Sample(gSmpLinearClamp, float3(offset.yz, texCoords.z));
    float4 sample2 = gAtmosphereInscatter.Sample(gSmpLinearClamp, float3(offset.xw, texCoords.z));
    float4 sample3 = gAtmosphereInscatter.Sample(gSmpLinearClamp, float3(offset.yw, texCoords.z));


    float sx = s.x / (s.x + s.y);
    float sy = s.z / (s.z + s.w);

    return lerp(lerp(sample3, sample2, sx), lerp(sample1, sample0, sx), sy);
}




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
	
	// volume fog parameters
	float2 	screenSize;
	float 	fog_far_Start;
	float	fog_far_log_F;			//(k-1 / k) / log(far)		// FIXME might be k-2 to make up for half pixel offsets
	
	float 	fog_far_one_over_k;		// 1.0 / k
	float 	fog_near_Start;
	float	fog_near_log_F;			//(k-1 / k) / log(far)		// FIXME might be k-2 to make up for half pixel offsets
	float 	fog_near_one_over_k;		// 1.0 / k
	
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



float4 sunLight(float3 posKm)
{
    float2 sunUV;
    float dX = dot(posKm.xz, normalize(sunDirection.xz));
    sunUV.x = saturate(0.5 - (dX / 1600.0f));
    sunUV.y = 1 - saturate(posKm.y / 100.0f);
    return SunInAtmosphere.SampleLevel(gSmpLinearClamp, sunUV, 0) * 0.07957747154594766788444188168626;
}


StructuredBuffer<Terrain_vertex> VB;
RWStructuredBuffer<gpuTile> 			tiles;

StructuredBuffer<tileLookupStruct>		tileLookup;



terrainVSOut vsMain(uint vId : SV_VertexID, uint iId : SV_InstanceID)
{
    terrainVSOut output;

    uint tileIDX = tileLookup[ iId >> 6 ].tile &0xffff;
	uint numQuad = tileLookup[ iId >> 6 ].tile >>16;	
	uint blockID = (iId & 0x3f);
	
	
	if (blockID < numQuad)
	{
		uint newID = tileLookup[ iId >> 6 ].offset + (blockID * 3) + vId;
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
    Attr.diffuse = 0.2;//    gAlbedoArray.SampleLevel(gSmpAniso, vIn.texCoords, 0).rgb;
	
	float3 PBR = gPBRArray.SampleLevel(gSmpAniso, vIn.texCoords, 0).rgb;
	Attr.alpha = 1.0;
	Attr.specular = 0;
	Attr.frenel = PBR.r;
	Attr.mesh_AO = 1;
	Attr.mesh_Cavity = 1;
	Attr.CURVATURE = 0;
	
    mat.roughness = saturate(PBR.g);
	
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
    float4 sunColor = sunLight(vIn.worldPos * 0.001);
    
    lightLayer(Attr, mat, sunDirection, sunColor.rgb * S, diffuse, specular); //	40us  - redelik vinnig maar tel op oor ligte - if() is worth it
	//diffuse += float3(0.03, 0.04, 0.05) * 0.3;

	diffuse *= (1 - mat.fresnel);
	specular *= mat.fresnel;

    diffuse += float3(0.01, 0.02, 0.04) * 1.32 * mat.diff.rgb;
	float3 colour = diffuse + specular * 0.1;
	
	/*
    float edgeBlend = smoothstep(0, 0.01, -dot(Attr.E, Attr.N));

    if (edgeBlend > 0.1)
    {
        //colour = float3(edgeBlend, 0, 0);
        
        float2 buffSize;
        gHalfBuffer.GetDimensions(buffSize.x, buffSize.y);
        float2 uv = vIn.pos.xy / (buffSize * 2.f);
        float3 prev = saturate(gHalfBuffer.Sample(gSmpAniso, uv).rgb);
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









