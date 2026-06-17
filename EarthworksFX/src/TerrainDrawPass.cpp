// EarthworksFX terrain — per-frame GPU work: reset indirect args, build the
// per-tile draw lookup, then issue the indirect terrain draw.
// Ported from docs/source_extract/src/TerrainFrame.cpp (update / onFrameRender).

#include "TerrainSystemImpl.hpp"

#include <cstring>

#include "MapHelper.hpp"

namespace earthworksfx
{

void TerrainSystem::Impl::Render(IDeviceContext* ctx, const TerrainFrameAttribs& attribs)
{
    // --- draw constants ---
    {
        MapHelper<DrawConstants> c(ctx, cbDraw, MAP_WRITE, MAP_FLAG_DISCARD);
        c->viewProj = attribs.ViewProj;
        c->eye      = attribs.CameraPos;
        c->pad0     = 0.f;
        c->sunDir   = attribs.SunDir;
        c->pad1     = 0.f;
    }

    // --- frustum/visibility flags (bit0 = resident, bit20 = surface tile) ---
    // Draw every resident *leaf* tile (a node with children is an interior LOD
    // node and is superseded by its four children). Frustum culling on the CPU
    // is deferred — the GPU only emits triangles for flagged tiles, so off-screen
    // leaves cost a flag, not a draw of their geometry beyond the clip test.
    {
        MapHelper<BuildLookupConstants> c(ctx, cbBuildLookup, MAP_WRITE, MAP_FLAG_DISCARD);
        std::memset(c->frustumFlags, 0, sizeof(c->frustumFlags));
        for (const earthworks::QuadtreeTile* tile : quadtree.usedTiles())
        {
            if (tile->child[0] != nullptr)
                continue; // interior node — its children are drawn instead
            const uint32_t slot = GpuSlot(tile->index);
            if (slot < kNumTiles)
                c->frustumFlags[slot] = 1u | (1u << 20);
        }
    }

    auto dispatch = [&](ComputePass& pass, uint32_t gx) {
        DispatchComputeAttribs da;
        da.ThreadGroupCountX = gx;
        da.ThreadGroupCountY = 1;
        da.ThreadGroupCountZ = 1;
        ctx->SetPipelineState(pass.pPSO);
        ctx->CommitShaderResources(pass.pSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        ctx->DispatchCompute(da);
    };

    // reset draw args + lookup allocator, then build the terrain lookup table.
    dispatch(clearPass, 1);
    dispatch(buildLookup, (kNumTiles + 31) >> 5);

    // --- indirect terrain draw ---
    ITextureView* rtvs[] = {attribs.pColorRTV};
    ctx->SetRenderTargets(1, rtvs, attribs.pDepthDSV, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    const TextureDesc& rtDesc = attribs.pColorRTV->GetTexture()->GetDesc();
    Viewport vp;
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    vp.Width    = static_cast<float>(rtDesc.Width);
    vp.Height   = static_cast<float>(rtDesc.Height);
    vp.MinDepth = 0;
    vp.MaxDepth = 1;
    ctx->SetViewports(1, &vp, rtDesc.Width, rtDesc.Height);

    ctx->SetPipelineState(drawPSO);
    ctx->CommitShaderResources(drawSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    DrawIndirectAttribs draw;
    draw.pAttribsBuffer                   = bufDrawArgs;
    draw.DrawArgsOffset                   = 0;
    draw.DrawCount                        = 1;
    draw.Flags                            = DRAW_FLAG_VERIFY_ALL;
    draw.AttribsBufferStateTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    ctx->DrawIndirect(draw);
}

} // namespace earthworksfx
