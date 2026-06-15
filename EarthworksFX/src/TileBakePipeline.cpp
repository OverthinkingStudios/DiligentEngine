// EarthworksFX terrain — per-tile GPU bake chain.
// Ported from docs/source_extract/src/TerrainTileBake.cpp::splitChild
// (elevation bicubic -> normals -> adaptive vertices -> jump flood -> delaunay).
// The orthophoto/albedo, terrafector overlay and BC6H passes are later phases.

#include "TerrainSystemImpl.hpp"

#include <algorithm>

#include "MapHelper.hpp"

namespace earthworksfx
{

void TerrainSystem::Impl::BakeTile(const earthworks::TileBakeRequest& req)
{
    IDeviceContext* ctx = immediate;

    const uint32_t csW = earthworks::kTileNumPixels / earthworks::kTileCsThreadSize; // 32

    const float tileSize  = worldSize; // single LOD-0 tile spans the world
    const float originX   = -worldSize * 0.5f;
    const float originZ   = -worldSize * 0.5f;
    const float outerSize = tileSize * earthworks::kTileNumPixels / float(earthworks::kTileInnerPixels);
    const float pixelSize = outerSize / float(earthworks::kTileNumPixels);

    // --- seed the GPU tile record (origin/scale/lod + zeroed counters) ---
    {
        earthworks::GpuTile gt{};
        gt.lod          = req.lod;
        gt.Y            = req.y;
        gt.X            = req.x;
        gt.flags        = 0;
        gt.origin[0]    = originX;
        gt.origin[1]    = 0.f;
        gt.origin[2]    = originZ;
        gt.scale_1024   = tileSize / 1024.f;
        gt.numQuads     = 0;
        gt.numPlants    = 0;
        gt.numTriangles = 0;
        gt.numVerticis  = 0;
        ctx->UpdateBuffer(bufTiles, Uint64{req.tileIndex} * sizeof(earthworks::GpuTile),
                          sizeof(earthworks::GpuTile), &gt, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    auto dispatch = [&](ComputePass& pass, uint32_t gx, uint32_t gy) {
        DispatchComputeAttribs da;
        da.ThreadGroupCountX = gx;
        da.ThreadGroupCountY = gy;
        da.ThreadGroupCountZ = 1;
        ctx->SetPipelineState(pass.pPSO);
        ctx->CommitShaderResources(pass.pSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        ctx->DispatchCompute(da);
    };

    // --- elevation bicubic -> texHeight ---
    {
        MapHelper<BicubicConstants> c(ctx, cbBicubic, MAP_WRITE, MAP_FLAG_DISCARD);
        const float elevSize = worldSize;
        const float S        = pixelSize / elevSize;
        c->offset[0]  = (originX - originX) / elevSize; // 0 for the root tile
        c->offset[1]  = (originZ - originZ) / elevSize;
        c->size[0]    = S;
        c->size[1]    = S;
        c->hgt_offset = 0.f;
        c->hgt_scale  = 1.f;
        c->isHeight   = 1;
        c->pad0       = 0.f;
    }
    dispatch(bicubic, csW, csW);

    // --- normals -> texNormals ---
    {
        MapHelper<NormalsConstants> c(ctx, cbNormals, MAP_WRITE, MAP_FLAG_DISCARD);
        c->pixSize = pixelSize;
        c->pad[0] = c->pad[1] = c->pad[2] = 0.f;
    }
    dispatch(normals, csW, csW);

    // --- adaptive vertices -> texVertsA ---
    {
        float vertScale = 1.f;
        if (req.lod < 7) vertScale = 1.3f;
        if (req.lod >= 16) vertScale = 3.2f;
        vertScale *= 2.5f;

        MapHelper<VerticesConstants> c(ctx, cbVertices, MAP_WRITE, MAP_FLAG_DISCARD);
        c->constants[0] = pixelSize * vertScale;
        c->constants[1] = 0.f;
        c->constants[2] = 0.f;
        c->constants[3] = float(req.tileIndex);
    }
    dispatch(vertices, csW / 2, csW / 2);

    // --- jump flood: A->B (step 4), B->A (step 2), A->B (step 1); final = B ---
    {
        struct Step { ComputePass* pass; IBuffer* cb; uint32_t step; };
        Step steps[3] = {
            {&jumpFlood, cbJumpFlood, 4},
            {&jumpFloodB, cbJumpFloodB, 2},
            {&jumpFlood, cbJumpFlood, 1},
        };
        for (auto& s : steps)
        {
            {
                MapHelper<JumpFloodConstants> c(ctx, s.cb, MAP_WRITE, MAP_FLAG_DISCARD);
                c->step = s.step;
                c->pad[0] = c->pad[1] = c->pad[2] = 0;
            }
            dispatch(*s.pass, csW / 2, csW / 2);
        }
    }

    // --- delaunay -> bufVB + tiles[].numTriangles ---
    {
        MapHelper<DelaunayConstants> c(ctx, cbDelaunay, MAP_WRITE, MAP_FLAG_DISCARD);
        c->tileIndex = req.tileIndex;
        c->pad[0] = c->pad[1] = c->pad[2] = 0;
    }
    dispatch(delaunay, csW / 2, csW / 2);

    // --- publish normals into the per-tile array slice ---
    {
        CopyTextureAttribs copy{texNormals, RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                arrNormals, RESOURCE_STATE_TRANSITION_MODE_TRANSITION};
        copy.SrcSlice = 0;
        copy.DstSlice = req.tileIndex;
        ctx->CopyTexture(copy);
    }
}

} // namespace earthworksfx
