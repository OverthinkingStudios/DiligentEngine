
//#include "tree_feedback.h"
//#include "gpuLights_defines.hlsli"



// render view data
#define main_LEFT 1<<0
#define main_CENTER 1<<1
#define main_RIGHT 1<<2
#define rear_LEFT 1<<3
#define rear_CENTER 1<<4
#define rear_RIGHT 1<<5

#define cascade_0 1<<6
#define cascade_1 1<<7
#define cascade_2 1<<8
#define cascade_3 1<<9

#define cubeEnv_0 1<<10
#define cubeEnv_1 1<<11
#define cubeEnv_2 1<<12
#define cubeEnv_3 1<<13
#define cubeEnv_4 1<<14
#define cubeEnv_5 1<<15

#define parabolic_low 1<<16
#define parabolic_medium 1<<17

#define numRenderViews 18



 #pragma once

#define THREADSIZE 256	
#define MAX_LIGHTS_PER_TILE 64
#define MAX_LIGHTS_PER_TILE_ARRAY 32

#define COMPUTE_DEBUG_OUTPUT

struct t_DrawArguments
{
	uint vertexCountPerInstance;
    uint instanceCount;
    uint startVertexLocation;
    uint startInstanceLocation;
};

struct t_DispatchArguments
{
    uint numGroupX;
    uint numGroupY;
    uint numGroupZ;
    uint padd;              // but useful ,maybe
};


// SPRITE - uses instance_VERTEX


struct PLANT_PART
{
	float4 		boundingSphere;				// If we are nout going to do this test, then need to get rid of this
	uint		numSprites;
	uint		spriteIndex;
};

struct PLANT_LOD
{
	uint		numPlantParts;
	uint		plantPartIndex;
	float		pixelSize;
};

struct PLANT
{
	float4 		boundingSphere;
	uint		numLod;
	uint		lodIndex;
	uint		rootSpriteIndex;
};

/*	64 byte aligned post xform render vertex*/
/*
struct instance_RENDER_VERTEX
{
	float4 		proj_pos;		// [16]
	
	float3 		keyLightDir;	// [12]
	float		shadow;			//  [4]
	
	float3		light;			// [12]		per plantpart/particle lighting, then key light per pixel
	uint		light_params;	//  [4]
	
	float2		dXdY;			//  [8]
	uint 		UV;				//  [4]		remains packed
	uint		Placeholder_4;	//  [4]
};
*/
struct instance_VERTEX						// same mapping, name diffirence
{
	float4 		pos;			// [16]
	
	float3 		up;				// [12]
	float		width;			//  [4]
	
	float3		right;			// [12]		
	uint		placeholder;	//  [4]
	
	//float3		light;			// [12]		per plantpart lighting, then key light per pixel
	//uint		light_params;	//  [4]
	
	float2		blend_flutter;			//  [8]
	uint 		flags_UV;		//  [4]
	uint		animation_data;				// especially other matricis for humans later, and big trees
};




/*	Sub part of a plant, comprising 2 matricis and a lookup into the sprite buffer
	This is the level that actual animation takes place
	Most grasses, and small bushes, and all lower LOD's will only have 1
	The aim is to balance the number of sprites for better occupancy during animation, projection and lighting
	
	
	??? Maybe 2 vectors, keep this down for plants
	
struct instance_PLANT_PART
{
	float3 pos;				//[12]
	float scale;			//[4]
	
	float2 dXdZ;			//[8]
	uint	PLANTPART_input_index;	//[4]
	uint	X;				//[4]
	
	// Now this needs some anim information, expecailly if its nested, still not sure what
};
*/
struct instance_PLANT_PART
{
	uint xyz;		//10 10 12
	uint s_r_idx;	//8 8 16
};


struct xformed_PLANT
{
    float3 position;
    float scale;

    float2 rotation;
    uint index;
    uint padd;
};



/*	It is possible to get this down to 8 bytes with tight bit packing but the unpacking will probably be slower than the memory access, this way will build a matrix fast
	better plan with good large world support is to use half's everywhere and pack the last two values as a single index

struct instance_PLANT
{
	float3 pos;				//[12]
	float scale;			//[4]
	
	float2 dXdZ;			//[8]
	uint index_PLANT;		//[4]
	uint spriteIndex;		//[4] if >0 this is a billboard plant, treat it as such, points to the sprite itself
};
*/
struct instance_PLANT
{
	uint xyz;		//  .. 10 10 10   we have 2 bits free - probably on Y
	uint s_r_idx;	//	1:9:11:11  s:r:tile:plant	
	// and maybe position should get less and scale and rotation more bits 
};



struct triangleVertex
{
    float3 pos;
    float u;
    float3 norm;
    float v;
};


//#define t_LU_blocksize	64
//#define t_LU_shift		6
//#define t_LU_mask		0x3f
#define tileLookupStruct uint



/*we can pack this into 32 bytes but not much gain to do so immediately, leave that as a final optimization - EVO does not use the lights, so 46 bytes already*/
// uint lights[MAX_LIGHTS_PER_TILE_ARRAY];
// split out
	
struct gpuTile
{
	uint lod;				// split
	uint Y;					// split
	uint X;					// split
	uint flags;				// cpu every frame, so keep unused and make diffirent flags buffer
	
	float3 origin;			// split + update in compute
	float scale_1024;		// split 						tileSize / 1024  - scale from internal 10-bit data to float
	
	uint numQuads;			// compute
	uint numPlants;			// compute
	uint numTriangles;		// compute
	uint numVerticis;		// compute
};

	
struct tileForSplit
{
	uint index;
	uint lod;
	uint y;
	uint x;

	float3 origin;
	float scale;
};

// pass information into the GPU about the current tile to extract
// also use this as the split information on GPU, and maybe pass in the same infor to different shader for a merge
struct tileExtract
{
	float3 origin;
	float scale;
	
	uint index;
	uint lod;
	uint Y;
	uint X; 
	
	uint c1;
	uint c2;
	uint c3;
	uint c4;	// maybe pack this better for faster extract
};





/*	Far Plant instance, 32 byte
	see if I can compress to 16, its possible
	This is reference for future expansion. In a  full quadtree structure, its probably good to keep the 1 sprite billboards more compact, with a direct refernce to bottom most sprite
	and just a scale value, no rotation needed yet*/
struct instance_SPRITE
{
	float3 		position;
	uint 		scale_index;		//[2][2]
};

struct GC_variables
{
	float4x4 	view;			//??? not sure we use either
	float4x4 	proj;
	float3 		eye_position;
	float4		frustum_1;
	float4		frustum_2;
	float4		frustum_3;
	float4		frustum_4;
	// info here about the texture atlas layout
	
	float 		halfAngle_to_Pixels;
	
	uint		numPlants;			// ??? should it be here or in feedback or other
	uint		numPlantParts;

	uint startIndex;
};

// NOTE (Diligent/Vulkan port): this struct is compiled by BOTH MSVC (via
// terrafector.h) and DXC->SPIR-V. SPIR-V aligns float4x4 and float3 members to
// 16 bytes while glm packs them tightly, so the two layouts silently diverged
// (GPU stride 1888 vs C++ sizeof 1868) and every CPU readback past p_world was
// shifted. The padd_align_* members below make the C++ layout match the SPIR-V
// layout exactly; static_asserts at the end of this file verify it.
struct GC_feedback			// to log and read back to debug and test the process
{
	uint 	plants_in_frustum;
	uint	plants_culled;
	
	uint 	plantparts_in_frustum;
	uint	plantparts_culled;			// maybe unused
	
	uint 	particles_in_frustum;
	uint	particles_culled;			// mayeb unused
	
	int		xmin;
	int		xmax;
	int		zmin;
	int		zmax;
	
	uint	padd_align_0;		// SPIR-V aligns the following float4x4 to 16 bytes (offset 48)
	uint	padd_align_1;
	
	float4x4	p_world;
	float4	p_proj;
	float4	p_up;
	float	p_width;
	
	// uint numPlantShaderCalls; DEPRECATED
	
	// lookups
    uint numQuadTiles[numRenderViews];
    uint numQuadBlocks[numRenderViews];
    uint numQuads[numRenderViews];
    uint maxQuads[numRenderViews];

    uint numPlantTiles[numRenderViews];
    uint numPlantBlocks[numRenderViews];
    uint numPlants[numRenderViews];
    uint maxPlants[numRenderViews];
	
    uint numTerrainTiles[numRenderViews];
    uint numTerrainBlocks[numRenderViews];
    uint numTerrainVerts[numRenderViews];
    uint maxTriangles[numRenderViews];
	
	// lookup params
    uint numLookupBlocks_Quads[numRenderViews];     // These are not really DDBUG they are critical for build to work
    uint numLookupBlocks_Plants[numRenderViews];
    uint numLookupBlocks_Terrain[numRenderViews];       
	
	uint	padd_align_2;		// SPIR-V aligns the following float3 to 16 bytes (offset 1232)
	
	// terrain under mouse
	float3 	tum_Position;
	uint 	tum_idx;
	float3 	tum_Normal;
	uint 	tum_lastKnownTile;
	
	float3 	colourUnderMouse;
	float	heightUnderCamera;
	
	// new cull feedback
	float4x4 frustum;
	float4x4 view;
	float4 bS;
	float4 bStoView;
	float4 bsSaturate;
	float size;
	float2 paddS;
    uint    numPostClippedPlants;

    uint vegRibbonClippedPixels;
    uint vegRibbonOKPixels;

    uint numTiles[20];
    uint numSprite[20];
    uint numPlantsLOD[20];
    uint numTris[20];

    uint numPix[20];

	uint	padd_align_3;		// SPIR-V rounds the struct size up to a 16-byte multiple (stride 1888)
	uint	padd_align_4;
};

#ifdef __cplusplus
// Guard against the C++ mirror of GC_feedback drifting from the SPIR-V layout
// again. The expected offsets come from the DXC SPIR-V OpMemberDecorate output
// (see BRINGUP_NOTES.md). If one of these fires after editing the struct,
// re-dump the offsets with:
//   dxc -T cs_6_0 -E main -spirv -fspv-reflect compute_tileClear.hlsl -Fc out.spvasm
static_assert(offsetof(GC_feedback, p_world) == 48, "GC_feedback::p_world must sit at SPIR-V offset 48");
static_assert(offsetof(GC_feedback, numTerrainTiles) == 724, "GC_feedback::numTerrainTiles must sit at SPIR-V offset 724");
static_assert(offsetof(GC_feedback, tum_Position) == 1232, "GC_feedback::tum_Position must sit at SPIR-V offset 1232");
static_assert(offsetof(GC_feedback, frustum) == 1280, "GC_feedback::frustum must sit at SPIR-V offset 1280");
static_assert(offsetof(GC_feedback, numTiles) == 1480, "GC_feedback::numTiles must sit at SPIR-V offset 1480");
static_assert(sizeof(GC_feedback) == 1888, "GC_feedback size must match the SPIR-V structured buffer stride (1888)");
#endif



struct sprite_material
{
    // NOTE: HLSL/DXC (unlike the original Slang toolchain) does not allow
    // default member initializers on struct members. Defaults removed; this
    // struct is filled from the CPU side, so the values are supplied there.
    int alphaTexture; // a
    int albedoTexture; // rgb
    int normalTexture;
    int pbrTexture;            // this is the one not in use - keep it for now

    //???
    int translucencyTexture;
    float translucency;
    float alphaPow;

    float3 albedoScale[2]; // front and back
    float roughness[2];
    
};



/*
	if( dot(viewpos, gv.frustum_1) < -R ) return false;
	if( dot(viewpos, gv.frustum_2) < -R ) return false;
	if( dot(viewpos, gv.frustum_3) < -R ) return false;
	if( dot(viewpos, gv.frustum_4) < -R ) return false;
	
	return true;
*/


























/*	32 byte aligned input sprite vertex
	- sensible sizes 64, 32, 16
	- pos can easily go down to packed 16 bit - would give 64m at mm resolution
	- Up and Width are packed 16 bits
	- Flags  -  Ribbon, 
				Fixed, 
				Fade
				U
				V
	- Light  - 	Ambient occlusion, 
				Translucense, 
				Specular  for per particle lighting
	- Animation - 	Bend, interpolate between the two matricis
					Flutter, small movements in the wind
					mat_1, first matrix
					mat_2, second matrix
					
	// somehere in the near future I may pack this
	// INPUT side 256K 64byte		: 16Mb still small
	// OUTPUT side 1Mn @64			: 64Mb
	// compressing to 32 is good but get it going first
	float3 		pos;			// [12]
	uint		Uxy;			// [4]
	uint		UzW;			// [4]
	
	uint		flags_UV;		// [4]			
	uint		light;			// [4]
	
	uint		animation;		// [4]
	
	
	struct instance_VERTEX
{
	float3 	pos;
	uint	X0;
	
	float3 	up;
	float 	width;
	
	uint 	flags;
	uint 	uv;
	uint	X1;
	uint	X2;
	
	float3 	light;
	uint 	light_params;
};
	*/






