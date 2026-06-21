
// material from c++
#define MAT_TEX_DIFF 0x0001
#define MAT_TEX_SPEC 0x0002
#define MAT_TEX_NORM 0x0004
#define MAT_TEX_F0 	 0x0008
#define MAT_TEX_RGH  0x0010
#define MAT_TEX_AO 	 0x0020
#define MAT_TEX_CAVITY 0x0040
#define MAT_TEX_MASK 0x0080
#define MAT_TEX_EMISSIVE 0x0100


#define MAX_LAYERS 5
#define MAX_TEXTURES 20




struct SH_Attributes
{
	float3    P;		// world position
    float3    E;    	// eye direction
    float3    N_mesh;   // Mesh interpolated normal
	float3    N;   		// normal map adjusted normal - cleaned up for backfacing
    float3    T;        // normal map adjusted tangent
    float3    B;        // normal map adjusted bitangent
	float3	  CURVATURE;
    float2    uv;		// texture coordinates
	
	float3    R_adj;    // FIXME remove from here
	
	float3	diffuse;
	float 	alpha;
	float3	specular;
	float	frenel;
	float	mesh_AO;
	float	mesh_Cavity;
};

struct layer_material
{
	float4	diff;
	
	float4 	spec;
	
	float 	f0;
	float	roughness;
	float	AO;
	float	cavity;
	float 	mask;
	float	fresnel;
	
	float3    N;   		// normal map adjusted normal - cleaned up for backfacing
    float3    T;        // normal map adjusted tangent
    float3    B;        // normal map adjusted bitangent
	float3 	  R;
};

struct eval_mat
{
	float4 diff_alpha;
	
	float4 norm_roughness;
	
	float alpha;
	
	float2 roughness;
	float F0;
	float AO;
	float ClearCoat;
	float3 dirtNrmAlpha;
};



struct	MAT_LAYER
{
	float4 diff;
	float4 spec;
	
	float 	f0;
	float	roughness;
	float	AO;
	float	cavity;
	
	float2	uv_scale;
	float	normal_scale;
	uint	uv_set;

	uint4 	texSlot[3];	// 12 packs betetr , into 3x 4 componenbt
};





cbuffer JohanMaterialCB : register(b1)
{
	uint		gNumLayers;
	uint		gUseMeshNormalMap;
	uint		gUseMeshCurvatureMap;
	uint		gFILLER;
	MAT_LAYER 	g_material_Layer[MAX_LAYERS];
	
};
Texture2D 	matTextures[MAX_TEXTURES];





// FIXME is daar n blok metode vir assign, dalk met structs
void prepareMaterialLayer( in SH_Attributes Attr, in MAT_LAYER M, inout layer_material MAT )
{
	MAT.diff 		= M.diff;
	MAT.spec 		= M.spec;
	MAT.f0 			= M.f0;
	MAT.roughness 	= M.roughness;
	MAT.AO 			= 1;
	MAT.cavity 		= 1;
	MAT.mask		= 1;
	
	MAT.N = Attr.N; 
	MAT.T = Attr.T;  
	MAT.B = Attr.B;
	MAT.R = 0;
	MAT.fresnel = 0;
	
	float2 uv = Attr.uv * M.uv_scale;
	if (M.texSlot[0].y)		MAT.mask = matTextures[M.texSlot[0].y].Sample(gSmpAniso, Attr.uv).r;		// masks use the base UV
	
	if (MAT.mask > 0.001)
	{
		if (M.texSlot[0].x)	MAT.diff = 			matTextures[M.texSlot[0].x].Sample(gSmpAniso, uv);
		if (M.texSlot[0].z)	MAT.spec = 			matTextures[M.texSlot[0].z].Sample(gSmpAniso, uv);
		if (M.texSlot[1].x)	MAT.roughness = 	matTextures[M.texSlot[1].x].Sample(gSmpAniso, uv).r;
		if (M.texSlot[1].y)	MAT.f0 = 			matTextures[M.texSlot[1].y].Sample(gSmpAniso, uv).r;
		if (M.texSlot[1].z)	MAT.AO = 			matTextures[M.texSlot[1].z].Sample(gSmpAniso, uv).r;
		if (M.texSlot[1].w)	MAT.cavity = 		matTextures[M.texSlot[1].w].Sample(gSmpAniso, uv).r;
		
		
		// FIXME at the moment this leaves the trtangent base messed up if we adjust the normal but I think that matters almost none
		if (M.texSlot[0].w)
		{
			float3 normal = RGBtoNormal( matTextures[M.texSlot[0].w].Sample(gSmpAniso, uv).rgb );
			normal = lerp( float3(0, 0, 1), normal, M.normal_scale );
			applyNormalMap(normal, MAT.N, MAT.T, MAT.B);
		}
		

		// reflect --------------------------------------------------------------------------------------------
		MAT.R = normalize(Attr.E - 2 * MAT.N * dot(Attr.E, MAT.N));	
		float NmeshoR = dot(MAT.R, Attr.N);
		if (NmeshoR > 0)
		{
			MAT.R = normalize(MAT.R - 2*MAT.N*NmeshoR);

			float dC = 1-NmeshoR;
			MAT.cavity *= dC;
			//MAT.R = float3(0, 0, 0);
		}
		
		//MAT.R = Attr.E;//

	
		// fixme, also adjust the refection towards the eye depending on roughness
		
		// block bacWARDS FACING Normals
		float dForward = saturate(dot(-Attr.E, MAT.N)) + 0.0;
		MAT.N = normalize(MAT.N + dForward * Attr.E);
		
		

		// fixme CHEAP BUT TRreat bottom of alpha a smask.
		float specularAlpha = saturate((MAT.diff.a) * 10.0f);
		MAT.cavity *= specularAlpha;
		MAT.diff.a = pow(saturate((MAT.diff.a - 0.1) * 1.16f), 0.7);

		// FIXME calc frenel from my reflection vector rather later
		// using abs instead of saturate is interesting
		MAT.fresnel = saturate( schlick(MAT.f0, (0.96 - saturate(dot(Attr.E, MAT.N)))));			// 45us - its the dot products
		MAT.diff.a = lerp( MAT.diff.a, 1, MAT.fresnel);
		MAT.diff.a *= MAT.mask;
	}
}
