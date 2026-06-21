


// We have a large linear array of this, that gets filled from compute
struct block_data
{
    uint instance_idx = 0;          // -> plant     20 bits at least
    //uint section_idx = 0;           // DEPRECATED -> plant_section - xpbd sim
    uint vertex_offset = 0;         // -> ribbonVertex8 data. Stored in verticis for now, multiples of VEG_BLOCK_SIZE
    //uint plant_idx = 0;             // DEPRECATED can find this via instance but likely faster and keeps us aligned    16 bits enough
};


    // lights    
    // xpbd data, sub parts I guess    
    // I think wind vector should live in an external system and run as a lookup
struct plant_instance
{
    float3 position;
    float scale;

    float rotation;
    uint plant_idx;     // MAX 16 bits 65536 individual plnats is a ton
    uint padd1;
    uint padd2;         // I am not compressing since leaf instancing will require more data here, will pakc when everythign is there
};


struct plant_section
{
    // sub parts for xpbd
};

// Halve size 32, 16, 16 packed
struct _plant_lod
{
    float pixSize;          //12 biyts max in ints
    uint startVertex;       // make this count in blocks    at the very least 21 bits 2mb of blocks 64Mb of verts, might as well be 32
    uint numBlocks;         // 8000 is realyl large especialyl with sub parts, still only gets us smaller is I do somethign weird with pixSize
    uint reserved; 
};

struct _plant_anim_pivot
{
    float3 root;
    float frequency;        // scale inside the shader by sqrt(1/scale) as well ALL Frequencies

    float3 extent;          // vector towards the tip but lenght = 1/length - dot product with actual vertex is already on 0..1
    float stiffness;

    int offset;             // time offset
    float shift;            // getting misused for something
    int padd1;
    int padd2;
};

struct plant
{
    float2 size;        // half width but full height
    // packing
    float scale;
    float radiusScale;

    float3 offset;
    float unused_01;

    // lighting
    float Ao_depthScale = 0.3f; //??? unused
    float sunTilt = -0.2f;
    float bDepth = 20.0f;
    float bScale = 0.5f;

    // soft shadows
    float shadowUVScale = 1.f;
    float shadowSoftness = 0.15f;
    // flutter
    float flutter_stength;
    float flutter_freq;

    

    uint numLods;                       // most likely less than 32 but 8 bits 256 great
    uint billboardMaterialIndex;        // easily 8k
    int padd1;
    int padd2;

    // lodding
    _plant_lod lods[16];            // 16 should do, has to be fixed, 8 might also but careful for big trees, although they should really sub-lod

    // pivot points for animation
    _plant_anim_pivot rootPivot;
};


struct ribbonVertex8
{
    uint a;
    uint b;
    uint c;
    uint d;
    uint e;
    uint f;
    uint g;
    uint h;
};


#define VEG_BLOCK_SIZE 32



struct vegetation_feedback
{
    // plant zero
    float plantZero_pixeSize;
    uint plantZeroLod;
    uint numBillboard;
    uint numPlant;

    uint numLod[32];
    uint numPlantsType[32];

    uint numBlocks;
    uint numInstanceAddedComputeClipLod;
    uint numFrustDiscard;
};
