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
    {
        MapHelper<BicubicConstants> c(ctx, cbBicubic, MAP_WRITE, MAP_FLAG_DISCARD);
        BicubicConstants bc{};
        if (!BindElevationForBake(req, bc, pixelSize))
        {
            // No JP2 entry / decode: fall back to the LOD-0 root grid sub-region.
            const float elevSize = worldSize;
            const float S        = pixelSize / elevSize;
            bc.offset[0]         = (originX - rootOriginX) / elevSize;
            bc.offset[1]         = (originZ - rootOriginZ) / elevSize;
            bc.size[0]           = S;
            bc.size[1]           = S;
            bc.hgt_offset        = 0.f;
            bc.hgt_scale         = 1.f;
            bc.useRoot           = 1;
            bc.inputSlice        = 0;
        }
        bc.isHeight = 1;
        bc.pad0     = 0;
        *c          = bc;
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
