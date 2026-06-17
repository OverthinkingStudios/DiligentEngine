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

    // World placement of this quadtree node: tiles tessellate the root extent, a
    // node at (lod, x, y) covers a 1/2^lod sub-square.
    const uint32_t slot       = GpuSlot(req.tileIndex);
    const float    lodScale   = 1.0f / float(1u << req.lod);
    const float    tileSize   = worldSize * lodScale;
    const float    originX    = rootOriginX + float(req.x) * tileSize;
    const float    originZ    = rootOriginZ + float(req.y) * tileSize;
    const float    outerSize  = tileSize * earthworks::kTileNumPixels / float(earthworks::kTileInnerPixels);
    const float    pixelSize  = outerSize / float(earthworks::kTileNumPixels);

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
        ctx->UpdateBuffer(bufTiles, Uint64{slot} * sizeof(earthworks::GpuTile),
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
    // The source is the whole-world root elevation texture; this tile samples the
    // sub-region [origin, origin+tileSize] of it. (Once JP2 streaming lands, the
    // source swaps to the decoded per-tile texture with elevSize = tileSize and
    // offset 0 — see docs/terrain/05-real-terrain-data.md step 3.)
    {
        MapHelper<BicubicConstants> c(ctx, cbBicubic, MAP_WRITE, MAP_FLAG_DISCARD);
        const float elevSize = worldSize;
        const float S        = pixelSize / elevSize;
        c->offset[0]  = (originX - rootOriginX) / elevSize; // = x / 2^lod
        c->offset[1]  = (originZ - rootOriginZ) / elevSize; // = y / 2^lod
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
        c->constants[3] = float(slot);
    }
    dispatch(vertices, csW / 2, csW / 2);

    // --- jump flood: A->B (step 4), B->A (step 2), A->B (step 1); final = B ---
    // Three iterations with halving step — see docs/terrain/03-tile-gpu-pipeline.md.
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
        c->tileIndex = slot;
        c->pad[0] = c->pad[1] = c->pad[2] = 0;
    }
    dispatch(delaunay, csW / 2, csW / 2);

    // --- publish normals into the per-tile array slice ---
    {
        CopyTextureAttribs copy{texNormals, RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                arrNormals, RESOURCE_STATE_TRANSITION_MODE_TRANSITION};
        copy.SrcSlice = 0;
        copy.DstSlice = slot;
        ctx->CopyTexture(copy);
    }
}

} // namespace earthworksfx
