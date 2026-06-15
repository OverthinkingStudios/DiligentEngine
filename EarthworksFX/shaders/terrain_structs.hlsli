#ifndef EARTHWORKSFX_TERRAIN_STRUCTS_HLSLI
#define EARTHWORKSFX_TERRAIN_STRUCTS_HLSLI

// Terrain-only subset of docs/source_extract/shaders/groundcover_defines.hlsli.
// Layouts are byte-for-byte identical to the originals (and to the CPU mirrors
// in earthworks/TerrainTypes.h). The vegetation / lighting structs are dropped
// for the phase-1 port; only what the terrain bake + draw needs is kept here.

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
    uint padd;
};

// SV_InstanceID -> (tile, vertex offset) shortcut lookup, see compute_tileBuildLookup.
struct tileLookupStruct
{
    uint tile;
    uint offset;
};

struct gpuTile
{
    uint lod;
    uint Y;
    uint X;
    uint flags;

    float3 origin;
    float  scale_1024; // tileSize / 1024

    uint numQuads;
    uint numPlants;
    uint numTriangles;
    uint numVerticis;
};

struct tileForSplit
{
    uint index;
    uint lod;
    uint y;
    uint x;

    float3 origin;
    float  scale;
};

#endif // EARTHWORKSFX_TERRAIN_STRUCTS_HLSLI
