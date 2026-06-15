// Terrain draw. The vertex shader is ported as-is from
// docs/source_extract/shaders/render_Tiles.hlsl (indirect, instanced, one
// triangle per instance, tile resolved through the lookup table). The pixel
// shader is the phase-1 simplified version from KICKOFF step 4: directional sun
// + normal map + ambient, with the atmosphere / PBR includes stubbed out.

#include "terrainDefines.hlsli"
#include "terrainFunctions.hlsli"
#include "terrain_structs.hlsli"

StructuredBuffer<Terrain_vertex>  VB;
StructuredBuffer<gpuTile>         tiles;
StructuredBuffer<tileLookupStruct> tileLookup;

Texture2DArray<float4> gNormArray;
SamplerState           gSmpAniso;

cbuffer gConstantBuffer
{
    float4x4 viewproj;
    float3   eye;
    float    _pad0;
    float3   sunDirection;
    float    _pad1;
};

struct terrainVSOut
{
    float4 pos       : SV_Position;
    float3 worldPos  : TEXCOORD0;
    float3 eyeVec    : TEXCOORD1;
    float3 texCoords : TEXCOORD2;
};

terrainVSOut vsMain(uint vId : SV_VertexID, uint iId : SV_InstanceID)
{
    terrainVSOut output;

    uint tileIDX = tileLookup[iId >> 6].tile & 0xffff;
    uint numQuad = tileLookup[iId >> 6].tile >> 16;
    uint blockID = (iId & 0x3f);

    if (blockID < numQuad)
    {
        uint            newID = tileLookup[iId >> 6].offset + (blockID * 3) + vId;
        Terrain_vertex  V     = VB[newID];

        float x = (V.idx & 0x7f) * 2;
        float y = ((V.idx >> 7) & 0x7f) * 2;

        float4 position = float4(0, 0, 0, 1);
        position.xz = tiles[tileIDX].origin.xz + float2(x - tile_BorderPixels, y - tile_BorderPixels) * tiles[tileIDX].scale_1024 * 4.0f * tile_toBorder;
        position.y  = V.hgt;

        output.worldPos  = position.xyz;
        output.eyeVec    = position.xyz - eye;
        output.pos       = mul(position, viewproj);
        output.texCoords = float3(x / tile_numPixels, y / tile_numPixels, tileIDX);
    }
    else
    {
        output.pos       = float4(0, 0, 0, 0); // degenerate -> culled
        output.worldPos  = float3(0, 0, 0);
        output.eyeVec    = float3(0, 0, 0);
        output.texCoords = float3(0, 0, 0);
    }

    return output;
}

float4 psMain(terrainVSOut vIn) : SV_TARGET0
{
    float3 N = gNormArray.Sample(gSmpAniso, vIn.texCoords).rgb * 2.0 - 1.0;
    N.y = sqrt(max(0.0, 1.0 - (N.x * N.x) - (N.z * N.z)));
    N = normalize(N);

    float3 L      = normalize(sunDirection);
    float  NdotL  = saturate(dot(N, L));

    // Simple base albedo: a height/slope tinted earthy palette so the mesh
    // reads clearly without the orthophoto/material passes.
    float3 grass = float3(0.18, 0.27, 0.11);
    float3 rock  = float3(0.30, 0.27, 0.24);
    float  slope = saturate((1.0 - N.y) * 3.0);
    float3 albedo = lerp(grass, rock, slope);

    float3 sunColor = float3(1.0, 0.96, 0.88);
    float3 ambient  = float3(0.20, 0.26, 0.34);

    float3 colour = albedo * (sunColor * NdotL + ambient);

    return float4(colour, 1.0);
}
