#ifndef EARTHWORKSFX_TERRAIN_FUNCTIONS_HLSLI
#define EARTHWORKSFX_TERRAIN_FUNCTIONS_HLSLI

// Ported verbatim from docs/source_extract/shaders/terrainFunctions.hlsli.
// Tile-local vertex coordinate packing (8 bits per axis).

uint pack_tile_pos(uint2 crd)
{
    return (crd.x << 8) + (crd.y & 0xff);
}

int2 unpack_tile_pos(uint V)
{
    return int2(V & 0x7f, (V >> 7) & 0x7f);
}

#endif // EARTHWORKSFX_TERRAIN_FUNCTIONS_HLSLI
