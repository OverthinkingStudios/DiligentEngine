
//#include "tree_feedback.h"
//#include "gpuLights_defines.hlsli"


 #pragma once

#define THREADSIZE 256	
#define MAX_LIGHTS_PER_TILE 64
#define MAX_LIGHTS_PER_TILE_ARRAY 32

//#define COMPUTE_DEBUG_OUTPUT

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


// but keep it a struct for now in case we need a bit more info
// this is a shortcut lookup between vertex and tile, given SV_InstanceId , what is tile
//#define t_LU_blocksize	64
//#define t_LU_shift		6
//#define t_LU_mask		0x3f
struct tileLookupStruct
{
	uint tile;
	uint offset;
};


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
	
	float4x4	p_world;
	float4	p_proj;
	float4	p_up;
	float	p_width;
	
	uint numPlantShaderCalls;
	
	// lookups
	uint numQuadTiles;
	uint numQuadBlocks;
	uint numQuads;
    uint maxQuads;
	uint numPlantTiles;
	uint numPlantBlocks;
	uint numPlants;
    uint maxPlants;
	
	uint numTerrainTiles;		// FXIME these wil ahve te be per camera later - maybe allow a max of 16 cameras or somethign like htta
	uint numTerrainBlocks;
	uint numTerrainVerts;
    uint maxTriangles;
	
	// lookup params
	uint numLookupBlocks_Quads;
	uint numLookupBlocks_Plants;
	uint numLookupBlocks_Terrain;
	
	// terrain under mouse
	float3 	tum_Position;
	uint 	tum_idx;
	float3 	tum_Normal;
	uint 	tumPadd;
	
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


    uint numTiles[20];
    uint numSprite[20];
    uint numPlantsLOD[20];
    uint numTris[20];
};



struct sprite_material
{
    int alphaTexture = -1; // a
    int albedoTexture = -1; // rgb
    int normalTexture = -1;
    int pbrTexture = -1;            // this is the one not in use - keep it for now

    //???
    int translucencyTexture = -1;
    float translucency = 1.f;
    float alphaPow = 1.f;

    float3 albedoScale[2] = { { 0.5f, 0.5f, 0.5f }, { 0.6f, 0.5f, 0.6f } }; // front and back
    float roughness[2] = { 0.02f, 0.3f };
    
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
