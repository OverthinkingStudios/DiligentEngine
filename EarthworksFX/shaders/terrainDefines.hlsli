#ifndef EARTHWORKSFX_TERRAIN_DEFINES_HLSLI
#define EARTHWORKSFX_TERRAIN_DEFINES_HLSLI

// Ported verbatim (constants only) from
// docs/source_extract/shaders/terrainDefines.hlsli.
// DO NOT change these without editing the CPU side (earthworks::TerrainTypes.h)
// and re-baking — the whole tile pipeline is keyed off them.

#define tile_cs_ThreadSize 8
#define tile_cs_ThreadSize_Generate 8

#define tile_numPixels 256
#define tile_BorderPixels 4
#define tile_InnerPixels 248
#define tile_toBorder 256.0f/248.0f

#define numVertPerTile  32768
#define numQuadsPerTile  32768
#define numPlantsPerTile  4096

struct Terrain_vertex
{
    uint  idx;
    float hgt;
};

struct centerFeedback
{
    float min;
    float max;
    float A;
    float B;
};

#endif // EARTHWORKSFX_TERRAIN_DEFINES_HLSLI
