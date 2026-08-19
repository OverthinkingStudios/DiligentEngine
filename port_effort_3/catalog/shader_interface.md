# Shader Interface — Per-Pass Binding Table (authoritative mechanical contract)

Source of truth: `port_effort_3/source_extract_3/` (paths below relative to that root unless
absolute). Drift reference: `EarthworksFX/hlsl/` + `EarthworksFX/src/compat/FalcorGpu.cpp`.
All file:line references verified 2026-08-02 against the current extract.

---

## 1. Purpose & data flow

Every draw in this renderer is **bufferless**: no vertex buffers exist anywhere. Geometry is
generated in VS from `SV_VertexID`/`SV_InstanceID` plus `StructuredBuffer` fetches. The only
index buffer in the whole engine is the shared static **128-quad-pattern IB** created once in
`pixelShader.cpp:26-38` (R16Uint, indices `0,1,2, 1,3,2, 2,3,4, …` — 2 tris per quad over a
vertex *strip*), used only by the `drawIndexedInstanced(64*6, n)` spline draws.

Two wrapper classes are the entire CPU shader API:

- `computeShader` (`computeShader.h/.cpp`): entry point always `"main"`; defines added via
  `add()` **before** `load()`; `dispatch(ctx,w,h,slices)` passes *group counts* directly;
  `dispatchIndirect(ctx, argBuffer, byteOffset)`. A dummy define `CHUNK_SIZE=256` is always
  added (`computeShader.cpp:8`).
- `pixelShader` (`pixelShader.h/.cpp`): `load(path, vs, ps, topology, gs="")`; if a GS entry is
  given, shader model is forced to **6_5** (`pixelShader.cpp:12`). Draw modes:
  - `renderIndirect(ctx, argBuf, blendState, startArg, numArgs)` →
    `drawIndirect(..., numArgs, argBuf, startArg*16, nullptr, 0)` — **NON-INDEXED, 16-byte
    stride** `t_DrawArguments` (`pixelShader.cpp:47`, args struct
    `hlsl/terrain/groundcover_defines.hlsli:42-48`). Verified: every indirect pass in this doc
    uses this same non-indexed 4-uint layout.
  - `drawIndexedInstanced(ctx, indexCount, instanceCount)` — uses the 128-quad IB.
  - `drawInstanced(ctx, vertexCount, instanceCount)` — non-indexed **even though the VAO
    carries the IB** (Falcor semantics; violating this scrambled every triangle list — see
    BRINGUP F22).

Frame flow (per frame, `terrain.cpp onFrameRender` + `Earthworks_4.cpp:255-282`):
CPU quadtree split/merge decisions → `compute_tileClear` → CPU uploads frustum flags →
`compute_tileBuildLookup` (builds ALL indirect args + lookups for 18 views) →
`compute_clipLodAnimatePlants` (indirect dispatch) → render passes (all indirect or
fixed-count) → tonemapper → blit previous-frame copy. Tile *bake* chain (streaming +
terrafector stamping + ecotopes + vertex extraction) runs **async, per tile, on
split/stream events**, not per frame.

---

## 2. Core tricks a rewriter MUST NOT lose

1. **64-slot lookup-block indirection** (`groundcover_functions.hlsli:5-20`,
   `compute_tileBuildLookup.hlsl:56-134`, consumed in `render_Tiles.hlsl:76-83`):
   one `uint` per block packs `tile(12b) | blockOffset(12b) | used(8b)`. Draw args count
   *instances*, VS does `tileLookup[iId >> 6]`, `blockID = iId & 0x3f`, and kills
   out-of-range blocks with `if (blockID < numQuad)`. This turns "draw N tiles of wildly
   varying triangle counts" into ONE non-indexed indirect draw per view. Terrain:
   `instanceCount += 64*numBlocks`, vertexCount=3 (`compute_tileClear.hlsl:55-56`,
   `compute_tileBuildLookup.hlsl:64`). Billboards: `instanceCount += numBlocks`,
   vertexCount=64 (`:58-59`, `:92`). Plants go to a **DispatchArgs** buffer instead
   (`:67-69`, `:119`) feeding the indirect clip/LOD compute.
2. **18 render views in one pass** (`groundcover_defines.hlsli:8-30`): bitmask flags
   (`main_CENTER`, `cascade_0..3`, `cubeEnv_0..5`, `parabolic_*`); CPU uploads
   `uint4 frustumflags[1024]` (x=surface, y=visible) as a **16 KB raw cbuffer blob**
   (`terrain.cpp:1923-1924`); `compute_tileBuildLookup` fans out lookups + args for every
   view in one 32-thread-per-tile dispatch. `viewMask` hardcoded at
   `compute_tileBuildLookup.hlsl:149`.
3. **Terrafectors bake by alpha-blending into an 8-MRT FBO** — materials.hlsli
   `PS_OUTPUT_Terrafector` (8 targets, `materials.hlsli:169-179`) with per-target blend
   `blendstateRoadsCombined` (`terrain.cpp:1127-1137`: RT0 elevation uses
   One/OneMinusSrcAlpha, RT1-7 SrcAlpha/OneMinusSrcAlpha; alpha channel SrcAlpha/OneMinus —
   RT0 exception is deliberate: elevation is pre-multiplied by alpha in the shader,
   `materials.hlsli:194`). `Elevation.a = 0` vs `= alpha` switches relative/absolute
   elevation semantics (`materials.hlsli:196-208`).
4. **Spline rendering without meshes**: `drawIndexedInstanced(64*6, numSplines)` — the shared
   quad-pattern IB walks a 65-vertex strip; VS evaluates the cubic bezier
   (`StructuredBuffer<cubicDouble>` 128 B = `float4 data[2][4]`, inner/outer edge) at
   t=vertexIndex/64, `bezierLayer{uint A,B}` packs flags/material/index and widths
   (`render_spline.hlsl:36-53`, CPU mirror `earthworks_scene/roads_bezier.h:24,226`).
5. **Vegetation ribbon vertices are 8 uints** (`ribbonVertex8`,
   `vegetation_defines.hlsli:97-107`) with bit-unpack macros at
   `render_vegetation_ribbons.hlsl:111-114` (`isCameraFacing (v.a>>31)`,
   `unpackPosition()` 14/16/14-bit) and `:285-289`. Drawn as **LineStrip → GS** expands each
   segment into ribbon quads; `_BILLBOARD` variant is PointList → GS. Blocks of
   `VEG_BLOCK_SIZE`=32 vertices; `block_data{instance_idx, vertex_offset}` is the draw unit.
6. **GPU Z-binning for vegetation draw order** (`compute_vegetation_lod.hlsl:122-148`):
   instances binned into 128 logarithmic distance bins (`z_index = log(distance)/log(1.07)`),
   each bin owns a slice of `block_buffer` (`veg_sort{current,size,offset,requested}`),
   `_PRE` sortCombine copies bin sizes into 128 consecutive `DrawArgs_Plants` entries,
   POST variant re-allocates bin slices for next frame with hysteresis
   (`compute_vegetation_sortCombine.hlsl:40-62`: `size = requested*2 + 2024`). CPU then
   issues up to 128 `renderIndirect(drawArgs_vegetation, idx, 1)` calls back-to-front
   (`vegetationBuilder.cpp:4256-4266`). Note `startInstanceLocation` is intentionally 0
   ("does nothing until SM 6.8", `:46`) — the *offset* lives in the bin's block_buffer slice.
7. **Multi-resolution vertex selection** for terrain tiles
   (`compute_tileVertices.hlsl:65-131`): three passes at 4/2/1-pixel steps with
   `GroupMemoryBarrierWithGroupSync()` between them and a curvature test
   (`testPixel`, cutoff scaled by `_PIXSIZE`=`constants.x`); dynamic threshold bump at `:107-108`
   (`if (idx > 20) lastscale = 0.15`). Output = sparse `gOutVerts` R32Uint pixels holding
   `(y<<7)+x`; then jump-flood (`compute_tileJumpFlood`, ping-pong A/B textures,
   `terrain.cpp:2509-2520`) and Delaunay-ish triangulation (`compute_tileDelaunay`) emit the
   `Terrain_vertex{uint idx; float hgt}` VB via `InterlockedAdd(tiles[].numVerticis)` +
   UAV-counter misuse (`terrain.cpp:2541`: counter counts 1s but writes 3s — load-bearing).
   Tile center height + origin.y are written by thread (0,0) (`compute_tileVertices.hlsl:73-79`).
8. **The tile FBO is simultaneously RTV target (terrafector bake) and UAV target (compute
   chain)** — 8 color planes, formats below §5.1. Compute writes into FBO colors require
   UAV binding on RT textures (BRINGUP F10).
9. **`GC_feedback` is the single GPU→CPU debug/introspection channel** (one 1888-byte
   struct, `groundcover_defines.hlsli:282-354`) — mouse picking (`tum_*`), height under
   camera, per-view lookup block counts (**critical, not debug**: `numLookupBlocks_*` are
   read at `compute_tileBuildLookup.hlsl:63,91,118` as allocation cursors).
10. **Full-cbuffer blob uploads**: three cbuffers are set as raw blobs, so C++ struct layout
    == HLSL cbuffer layout byte-for-byte: frustumflags (16 KB, `terrain.cpp:1924`),
    `tileForSplit child[4]` (`terrain.cpp:2252-2253`), `ecotopeGpuConstants`
    (`terrain.cpp:2416`, C++ `earthworks_scene/ecotope.h:149`).

---

## 3. Invariants & conventions

- Compute entry point is always `main`; graphics always `vsMain`/`gsMain`/`psMain`.
- Indirect args: `t_DrawArguments` = 4 uints, non-indexed; `t_DispatchArguments` = 3 uints +
  `padd`. One element per render view (18), except vegetation: `numRenderViews*128`
  (`vegetationBuilder.cpp:2394`).
- Tile constants (`terrainDefines.hlsli:3-17`): `tile_cs_ThreadSize` 8, `tile_numPixels` 256,
  border 4 px (`tile_BorderPixels`), inner 248, `tile_toBorder = 256/248`;
  `numVertPerTile`/`numQuadsPerTile` 32768, `numPlantsPerTile` 4096. Tile compute dispatches
  use `cs_w = 256/8 = 32` groups (variants: `/2` for the 128² vertex passes, `/4` for BC6H).
- `COMPUTE_DEBUG_OUTPUT` is **defined unconditionally** in `groundcover_defines.hlsli:40` —
  all feedback counters are live; several passes bind `gDebug` textures only under it.
- Positions inside a tile are 10-bit x/z + 12-bit height packed uints
  (`groundcover_functions.hlsli:24-74`); `unpack_pos` scale factor `1.032258 = 32/31`
  (magic; do not "fix").
- `scale_1024` in `gpuTile` = tileSize/1024 — scale from internal 10-bit ints to meters;
  world position math in `render_Tiles.hlsl:87` uses `scale_1024 * 4 * tile_toBorder`.
- Blend/depth/raster state is set imperatively per draw on the wrapper's GraphicsState and
  **sticks** until overwritten (Falcor retained-state model). Depth funcs are RH/[0,1]-era:
  `depthstateAll` = Always+no-write for bakes (`terrain.cpp:1104-1116`).
- Samplers are set once at init in most passes; names are per-shader
  (`linearSampler`, `gSampler`, `gSmp{Point,Linear,Aniso,LinearClamp}`, `gSamplerDepth`).
  `render_Common.hlsli:5-9` fixes registers s0-s4 for the PBR family.
- Register annotations in the sources are frequently wrong or absent
  (e.g. `compute_tonemapper.hlsl:6` declares an SRV `hdr : register(u0)` — it is a plain
  `Texture2D`); Falcor bound by *name via reflection*, never by register. A Diligent re-port
  must also bind by name and ignore the register decorations.

---

## 4. Performance-critical details

- The whole culling/LOD/draw-arg generation path is GPU-resident; the CPU only uploads
  16 KB of frustum flags and issues a fixed number of indirect calls. Reintroducing CPU
  readback into that loop (other than the existing async `buffer_feedback_read` /
  `buffer_tiles_readback` copies) kills the design.
- `compute_clipLodAnimatePlants` is dispatched **indirectly** (`terrain.cpp:2896`) with 64
  threads/group = one lookup block; it transforms only in-frustum plants into
  `xformed_PLANT output` and bumps `drawArgs_Plants` — sizing is exact, no over-dispatch.
- Vegetation LOD: pixel-size metric `lodBias * halfAngle_to_Pixels * size.y * scale /
  distance` (`compute_vegetation_lod.hlsl:81`); `halfAngle_to_Pixels =
  resolution * length(proj[1]) / 2` (`terrain.cpp:2889-2890`). Same metric feeds billboard
  cutover. Changing it desyncs LOD selection from the authored `plant.lods[].pixSize` tables.
- The 128-bin Z sort exists to get **approximate back-to-front alpha blending without any
  per-instance sort**; bins are drawn far→near by the CPU loop. `globallycoherent` on
  `sort`/`feedback` UAVs (`compute_vegetation_sortCombine.hlsl:12,15`) is required.
- `render_vegetation_ribbons` supports `_EARLY_Z` (writes `SV_DepthGreaterEqual`,
  `:958-962`) and `_Z_ONLY`; the default PS also does JHFAA-style previous-frame alpha
  blending (`render_Common.hlsli:215-223`) — needs `gPreviousFrame` re-bound every frame
  (`terrain.cpp:1758-1759`, `vegetationBuilder.cpp:4000-4001`).
- Terrain shadow is a CPU-solved 2-channel heightfield ray-march lookup
  (`render_Common.hlsli:78-85`, hardcoded world size `4096*9.765625` m) — cheap, no shadow
  maps for terrain. Vegetation high-res shadow is one 8192² R8Unorm FBO rendered by the
  `_DEPTH` variant (`vegetationBuilder.cpp:2375-2377,4456-4466`).
- BC6H compression of the tile PBR plane runs on-GPU per tile
  (`compute_bc6h.hlsl`, `terrain.cpp:2531-2532`) into an RGBA32Uint texture aliased as
  BC6HU16 array slice (`terrain.cpp:637,983-984`).
- Atmosphere volumes are tiny (64×32×256 R11G11B10F, `atmosphere.cpp:73`) and sampled with a
  **manual bicubic** in the terrain PS (`render_Common.hlsli:132-200`) — 4 taps + cubic
  weights; VS variant for per-vertex fog. Slice depth is logarithmic
  (`fog_far_log_F` family, `render_Common.hlsli:50-57`).

---

## 5. Per-pass binding tables

Legend: **CB** cbuffer (members in §5.9 or inline), **SB** StructuredBuffer, **RWSB** RW,
**Tex/RWTex** textures, **Feeder** = file:line of the CPU code binding it. Dispatch numbers
are *group counts*. All indirect draws: non-indexed 16-byte `t_DrawArguments`.

### 5.1 Shared render-target setups

| FBO | Where | Formats |
|---|---|---|
| `split.tileFbo` / `split.bakeFbo` | terrain.cpp:612-623 | D24S8; C0 **R32Float** elevation (UAV); C1 R11G11B10F albedo; C2 R11G11B10F pbr; C3 R11G11B10F alpha/permanence; C4-C7 RGBA8Unorm ecotopes. 256² (bake: `bakeSize`²) |
| `bakeFbo_plants` | terrain.cpp:626-632 | D24S8; C0,C1 RGBA8, C2,C3 R11G11B10F. 1024² |
| `hdrFbo` (main scene) | Earthworks_4.cpp:347 | D24S8 + C0 **R11G11B10Float** |
| veg `shadowFbo` | vegetationBuilder.cpp:2375-2377 | D24S8 + C0 R8Unorm(UAV), **8192²** |
| veg `rgbFbo` | vegetationBuilder.cpp:2379-2380 | C0 RGBA8Unorm(UAV), 1024×256 |
| veg bake fbo | vegetationBuilder.cpp:3806-3812 | C0 RGBA8 albedo, C1 RGBA16F normal, C2 RGBA8 normal8, C3 RGBA8 pbr, C4 RGBA8 extra, 4 samples |
| textureTool fbo | vegetationBuilder.cpp:2992-2999 | 5×RGBA8, 4 samples |
| Atmosphere volumes | atmosphere.cpp:10-16,73 | in/outscatter Texture3D 64×32×256 R11G11B10F (UAV); cloudbase/sky planes RGBA16F; `sunlightTexture` 512×256 RGBA32F |

Key buffers (terrain.cpp:640-662): `drawArgs_{quads,tiles}` = SB `t_DrawArguments`×18
(UAV|IndirectArg); `dispatchArgs_plants` = `t_DispatchArguments`×18; `buffer_tiles` =
`gpuTile`×numTiles; `buffer_instance_{quads,plants}` = `instance_PLANT`(8 B)×(tiles×32768 /
tiles×4096); `buffer_clippedloddedplants` = `xformed_PLANT`(32 B)×1M; `buffer_lookup_*[18]`
sized by `lookupSize*` tables (terrain.h:808-820); `buffer_terrain` = `Terrain_vertex`(8 B)
×32768×numTiles; `buffer_feedback` = `GC_feedback`×1. Vegetation
(vegetationBuilder.cpp:2385-2399): `plantData` `plant`×1024, `plantpivotData`
`_plant_anim_pivot`×256K, `instanceData` `plant_instance`×65536, `blockData(_preSort)`
`block_data`×1M(×3), `vertexData` `ribbonVertex8`×512K, `drawArgs_vegetation`×(18×128),
`drawArgs_billboards`×18, `buffer_gpuSort` uint4×1024, `buffer_feedback`
`vegetation_feedback`×1.

### 5.2 Tile GPU pipeline (compute; per-frame ones marked ▶)

| Pass | File | numthreads / dispatch | CB | Resources (feeder) |
|---|---|---|---|---|
| ▶ tileClear | terrain/compute_tileClear.hlsl | (1,1,1); dispatch(1,1) terrain.cpp:1916 | — | RWSB feedback (851), DrawArgs_Terrain(852), DrawArgs_Quads(853), DispatchArgs_Plants(856), DrawArgs_Plants(1050), feedback_Veg(1049). Sets vertexCounts 3/64/32 — see §2.1 |
| ▶ tileBuildLookup | terrain/compute_tileBuildLookup.hlsl | (32,1,1); dispatch(numTiles/32,1) terrain.cpp:1928 | `gConstants{uint4 frustumflags[1024]}` — 16 KB setBlob terrain.cpp:1923-1924 | RWSB tiles(897), DrawArgs_Quads(898), DrawArgs_Terrain(899), feedback(900), DispatchArgs_Plants(901), tileCenters(902); **ParameterBlock `viewRenderData`** = 3×18 RWSB<uint> lookup arrays, bound terrain.cpp:903-911 |
| ▶ clipLodAnimatePlants | terrain/compute_clipLodAnimatePlants.hlsl | (64,1,1); **dispatchIndirect**(dispatchArgs_plants, 0) terrain.cpp:2896 | `gConstantBuffer{float4x4 view; float4x4 clip; float halfAngle_to_Pixels}` (2892-2894) | SB tiles(861), tileLookup=buffer_lookup_plants[view](2895), plantBuffer=instance_plants(863); RWSB output=clippedloddedplants(864), drawArgs_Plants=plants_Root.drawArgs_vegetation(1043), feedback(866), instance_out=plants_Root.instanceData(1045), plant_buffer(1042), block_buffer(1041), feedback_Veg(1044) |
| tileSplitMerge | terrain/compute_tileSplitMerge.hlsl | (1,1,1); dispatch(1,1) terrain.cpp:2254 | `gConstants{tileForSplit child[4]}` setBlob 2252-2253 | RWSB tiles(872), feedback(873) |
| tileBicubic | terrain/compute_tileBicubic.hlsl | (8,8,1); dispatch(32,32) ×2 (height 2349-2356, albedo 2370-2373; GIS variants 3350-3393) | `gConstants{float2 offset; float2 size; float hgt_offset; float hgt_scale; int isHeight}` | Smp linearSampler(916); Tex gInput (streamed JP2 map / rootElevation, 2034/2161/2349); RWTex gOutput=tileFbo C0(917), gOutputAlbedo=C1(918), gOutputPermanence=C3(919), gDebug(920). Tex gInputAlbedo(2176) |
| tileEcotopes | terrain/compute_tileEcotopes.hlsl | (16,16,1); dispatch(32,32) terrain.cpp:2419 | `gConstants` = `ecotopeGpuConstants` blob (2416; C++ ecotope.h:149) incl. `float4 ect[12][5]; float4 texScales[12]` | RWTex gHeight=C0(926), gAlbedo=C1(927); Tex gLowresHgt=rootElevation(2388), gInPermanence=C3(928), gInEct_0..3=C4-7(929-932), gNoise u16(933); RWSB tiles(934), quad_instance(935), feedback(936); SB plantIndex/plantDensity (2389-2390, from ecotope.cpp:200-206); **ParameterBlock `gmyTextures{Texture2D<float4> T[256]}`** (2395-2400) |
| tileGenerate | terrain/compute_tileGenerate.hlsl | (8,8,1); dispatch(w_gen,h_gen) terrain.cpp:2489 (32-1 groups: generates exact 248² inner pixels) | `gConstants{uint tileIDX}` | Tex gHgt=tileFbo C0(880), gNoise u16(879), gEct1-4=C4-7(881-884); RWSB quad_instance(877), tiles(878), feedback |
| tilePassthrough | terrain/compute_tilePassthrough.hlsl | (256,1,1); dispatch(cnt,1) terrain.cpp:2430,2498 | `gConstants{uint parent_index,child_index,dX,dY}` (2426-2429) | Tex gHgt=C0(892), gNoise(893); RWSB quad_instance(888), plant_i=instance_plants(889), feedback(890), tiles(891), plant_buffer(1047). (tileLookup declared but unbound) |
| tileNormals | terrain/compute_tileNormals.hlsl | (8,8,1); dispatch(32,32) terrain.cpp:2453 | `gConstants{float pixSize; float3 padd}` (2452) | RWTex gInHgt=C0(941), gOutNormals=normals_texture R11G11B10F(942), gOutput=debug(943); RWSB tiles(944) — updates min/maxHgt |
| tileVertices | terrain/compute_tileVertices.hlsl | (8,8,1); dispatch(16,16) terrain.cpp:2468 | `gConstants{float4 constants}` = (pixSize*scale,0,0,tileIndex) (2467) | Smp linearSampler(948/2466); Tex gInHgt=C0(949); RWTex gOutVerts=vertex_A R32Uint(950), gDebug(951); RWSB tileCenters(952), tiles(953). 2nd `main` at :140 is commented out |
| tileJumpFlood | terrain/compute_tileJumpFlood.hlsl | (8,8,1); dispatch(16,16) ×N steps terrain.cpp:2506-2520 | `gConstants{uint step}` (2509) | Tex gInVerts / RWTex gOutVerts **ping-pong** vertex_A/B (2511-2516); gDebug(958) |
| tileDelaunay | terrain/compute_tileDelaunay.hlsl | (8,8,1); dispatch(16,16) terrain.cpp:2543 | `gConstants{uint tile_Index}` (2542) | Tex gInHgt=C0(962), gInVerts=vertex_B(963); RWSB VB=buffer_terrain(964) via **UAV counter** (reset terrain.cpp:2541), tiles(965) |
| bc6h | terrain/compute_bc6h.hlsl | (8,8,1); dispatch(8,8) terrain.cpp:2532 | `gConstants{view,proj,eye,alpha_pass,start_index}` — **copy-paste, never set, unused** | Smp gSampler; Tex gSource=tileFbo C2(2531); RWTex gOutput=bc6h_texture RGBA32Uint(984). Optional define `QUALITY` (not set) |
| terrainUnderMouse | terrain/compute_terrain_under_mouse.hlsl | (1,1,1); dispatch(1,1) terrain.cpp:3081 | `gConstants{float3 mousePos; float3 mouseDir; float2 mouseCoords}` (3076-3078) | Smp gSampler(844); RWSB tiles(846), groundcover_feedback=buffer_feedback(847); Tex gHeight=height_Array Texture2DArray R32F(845), gHDRBackbuffer=fbo C0(3079), gDepthBuffer (declared; not bound in extract) |

### 5.3 Terrain render passes (pixelShader)

| Pass | File / entries / topology | Draw | CB & resources |
|---|---|---|---|
| ▶ terrainShader | terrain/render_Tiles.hlsl vs+ps, TriangleList (load terrain.cpp:664) | renderIndirect(drawArgs_tiles, startArg=view, 1) terrain.cpp:2910; FBO=hdrFbo | CB `gConstantBuffer{view,proj,viewproj,eye,tileIndex}` (2906-2908); CB `PerFrameCB{gConstColor,gAmbient,gisOverlayStrength,showGIS,redStrength,redScale,gisBox,redOffset,float3 padding}` (675-678); CB `LightsCB` (1761-1767); SB VB=buffer_terrain(673), tileLookup(2909); RWSB tiles(665); Tex gAlbedoArray/gPBRArray/gNormArray Texture2DArrays(669-671), gGISAlbedo, terrainShadow(1755), atmosphere set Earthworks_4.cpp:104-106; Smp gSmpAniso(672), gSmpLinearClamp(679) |
| ▶ terrainSpiteShader (billboards) | terrain/render_tile_sprite.hlsl vs+gs+ps, **PointList+GS** (684) | renderIndirect(drawArgs_quads, view, 1) 2936; FBO=hdrFbo | CB `gConstantBuffer{viewproj; float3 right; int alpha_pass; float3 eye}` (2927-2932); CB `PerFrameCB` (same layout as render_Tiles); CB LightsCB (1770-1776); RWSB tiles(685); SB instanceBuffer=buffer_instance_quads(687), tileLookup(2934), materials=sb_vegetation_Materials(1038), plant_buffer(1037); **PB `textures{T[4096]}`** (2918-2920); Tex gEnv(2915), terrainShadow(1756), gPreviousFrame(1759), atmosphere (E4:108-110); Smp gSampler(688), gSmpLinearClamp(689) |
| ▶ triangleShader (skydome/tests) | terrain/render_triangles.hlsl vs+ps, TriangleList (720) | drawInstanced(36,1) 2966; blendstateSplines + rasterstateSplines | CB `gConstantBuffer{viewproj,eye,useSkyDome}` (2961-2963); SB instanceBuffer=triangleData(721), instances=clippedloddedplants(722); Tex gAlbedo, gSky(739), gAtmosphereInscatter_Sky (E4:118); Smp gSampler,gSmpLinearClamp(723-724). veghumanShader = same shader, second instance (727-731), no active draw |
| ribbonShader (grass ribbons — legacy) | terrain/render_ribbons.hlsl vs+gs+ps, **LineStrip+GS** (702) | **no active draw** (commented terrain.cpp:2991-2992); states set 2988-2989 | CB `gConstantBuffer{viewproj,eyePos,fakeShadow,objectScale,objectOffset,radiusScale,offset,repeatScale,numSide,Ao_depthScale,sunTilt,bDepth,bScale,time,ROOT}` (2985-2986); SB instanceBuffer=ribbonData[0] RV6(703), materials(704), instances=clippedloddedplants(705); PB `textures{T[4096]}`; Tex gAlbedo,gPreviousFrame(1758),gEnv(740); Smp gSampler/gSamplerClamp(706-707). Define `_BAKE` variant exists (not loaded) |
| shader_spline3D (overlay) | terrain/render_spline.hlsl vs+ps, TriangleList (1088) | drawIndexedInstanced(64*6, numIndex) 3018/3046/3052/3060; FBO=hdrFbo; blendstateSplines, depthstateAll | CB `gConstantBuffer{viewproj; float alpha}` (3007-3008); SB materials=sb_Terrafector_Materials(3010), splineData=cubicDouble(3016/3050), indexData=bezierLayer(3017/3051); PB `gmyTextures{T[4096]}` (3012-3014); Smp gSmpPoint/Linear/Aniso/LinearClamp (register s0-s3; gSmpLinear set 1090) |
| shader_splineTerrafector (bake) | terrain/render_splineTerrafector.hlsl vs+ps, TriangleList (1092) | drawIndexedInstanced(64*6, n) 2637/2728/2736/2744, 3617/3691-3703; FBO=**tileFbo/bakeFbo** (8 MRT); rasterstateSplines, depthstateAll, **blendstateRoadsCombined** | CB `gConstantBuffer{viewproj; uint startOffset}` (2605-2606, 2726…); SB materials(2607), splineData(2613/3593), indexData per-LOD (2636, 2727-2743); PB `gmyTextures` (2609-2611). PS output = 8-target `PS_OUTPUT_Terrafector` |
| shader_meshTerrafector (bake) | terrain/render_meshTerrafector.hlsl vs+ps, TriangleList (1098) | drawInstanced(**128*3**, tile->numBlocks) 2629 et al., 3609…; same FBO/states as spline bake | CB `gConstantBuffer{viewproj; float overlayAlpha}` (2590-2591, 2710); SB materials(1102), vertexData=`triVertex`(2627), indexData=`uint`(2628); PB `gmyTextures` (2593-2597); Smp gSmpLinear(1099). VS: `iId`=128-tri block, `vId` indexes `indexData[iId*128*3+vId]` → `vertexData` |
| render_Buildings_Far | terrain/render_Buildings_Far.hlsl vs+ps | **no CPU feeder in extract** — consumed only by port-side `buildings.cpp` (new code, keep as port reference) | CB `PerFrameCB{view,viewproj,eye,padd1}`; SB vertexBuffer=`_buildingVertex` |

### 5.4 Vegetation passes (vegetationBuilder.cpp)

All `render_vegetation_ribbons.hlsl` variants share the resource set bound at 2338-2360:
SB `plant_buffer`, `plant_pivot_buffer`, `instance_buffer`, `block_buffer`(_preSort for main),
`vertex_buffer`, `sort`, `materials`; PB `textures{T[4096]}`; Tex `highResShadow`
(=shadowFbo depth!), `gDappledLight`, `gEnv`, atmosphere trio, `terrainShadow`(4003-4004),
`gPreviousFrame`(4000-4001); RWSB `feedback_Veg`; Smp gSmpLinear=sampler_Ribbons,
gSamplerDepth, gSmpLinearClamp. CB `gConstantBuffer` layout —
**exact member order** (`render_vegetation_ribbons.hlsl:31-55`):
`float4x4 view; float4x4 viewproj; float3 eyePos; float padd1; float4 camRight; float4 camUp;
float time; float bake_radius_alpha; float bake_height_alpha; int bake_AoToAlbedo;
float3 windDir; float windStrength; float4x4 shadowViewProj; uint drawIndex; int toneMap;
float2 bake_AlphaOval;` plus CB `LightsCB` (4006-4020).

| Pass | Defines / topology / entries | Draw | Notes |
|---|---|---|---|
| ▶ vegetationShader | optional `_DEBUG_PIXELS/_PIXEL_COUNT/_Z_ONLY/_EARLY_Z` (2318-2331); LineStrip vs+gs+ps (2336) | per-bin renderIndirect(drawArgs_vegetation, idx∈[0,128), 1) 4256-4266 (or single renderIndirect 4241 / drawInstanced(32, totalBlocks) 4248); FBO=hdrFbo; blendstate or blendstate_withAlpha (4191-4194) | default PS at :962 (`SV_DepthGreaterEqual` out) |
| ▶ billboardShader | `_BILLBOARD` (2495); **PointList** vs+gs+ps (2496) | renderIndirect(drawArgs_billboards) 4284 | instance_buffer=instanceData_Billboards (2498); GS point→quad :585 |
| vegetationShader_DEPTH (shadow) | `_DEPTH` (2480); LineStrip (2481) | renderIndirect(drawArgs_vegetation) 4466 into shadowFbo; viewproj=shadowViewProj (4460) | PS :900 |
| vegetationShader_RGB_SAMPLE | `_RGB_SAMPLE` (2465) | renderIndirect(drawArgs_vegetation) 4602 into rgbFbo | PS :844; feeds sampleRGBtoPixel |
| bakeShader | `_BAKE` (2507) | drawInstanced(32, totalBlocksToRender) 3871 into veg bake fbo; blendstateBake | PS_OUTPUT_Bake 5-MRT :773; VS bake path :399-460 |
| ▶ compute_clearBuffers | terrain/compute_vegetation_clear.hlsl (1,1,1) | dispatch(1,1) 4146 | RWSB DrawArgs_Quads=drawArgs_billboards(2403), DrawArgs_Plants=drawArgs_vegetation(2404), feedback_Veg(2405). Sets billboard vertexCount=… (see file), plants vertexCount=VEG_BLOCK_SIZE |
| ▶ compute_calulate_lod | terrain/compute_vegetation_lod.hlsl, no defines → default variant; (256,1,1) | dispatch(65536/256,1) 4163 | CB `gConstantBuffer{view,frustum,eyePos,padd,firstPlant,lastPlant,firstLod,lastLod,lodBias,halfAngle_to_Pixels}` (4154-4162); RWSB DrawArgs_Quads/Plants(2408-2409), block_buffer=blockData_preSort(2413), instance_buffer_billboard(2412), sort(2415), feedback(2414); SB plant_buffer(2410), instance_buffer(2411). `SHADOW_TILE`/`SHADOW_OUTER` variants never loaded |
| ▶ compute_sortCombine (`_PRE`, 2418) + …_POST (no define) | terrain/compute_vegetation_sortCombine.hlsl (1,1,1) | PRE dispatch(1,1) 4173 before draws; POST 4310 after | RWSB DrawArgs_Plants, pre/post_block_buffer, sort, feedback (2420-2432). `_COPY` variant is dead (uses undeclared `i`; would not compile) |
| compute_sampleRGBtoPixel (+`_TO_TEXTURE` twin) | terrain/compute_sampleRGBtoPixel.hlsl (32,32,1) | dispatch(32,8) 4609 per cell; ToTexture dispatch(4,2) 4626 | CB `gConstants{uint4 counters; uint2 pix}` (4608); Tex gIn=rgbFbo C0; RWTex gOut=RGB_MAP; RWSB data=rgb_data uint4×32768 (2440-2448, cleared via setBlob 4476) |
| compute_bakeFloodfill | terrain/compute_bakeFloodfill.hlsl (4,4,1) | dispatch(iW/4,iH/4) ×N steps 3893 | CB `gConstants{uint step}`; RWTex gAlbedo/gNormal/gTranslucency/gpbr = bake fbo C0/C2/C4/C3 (3887-3890). (Also loaded by terrain.cpp:712 for bakeFbo_plants.) **Untyped `RWTexture2D` decls** — DXC requires `<float4>` (see §8) |
| textureExtractShader | terrain/extractTextures.hlsl vs+**gs**+ps, PointList (2970) | drawInstanced(1,1) 3046 into textureTool fbo; blendstateBake | CB `gConstantBuffer{A,B,C,D,start,stop,bezier,width,padd,flipRed,flipGreen,nStrength,toSRGB}` (3024-3039); Tex galbedo/galpha/gnormal/gtranslucency (3041-3044); Smp gSmpLinear(2971). GS emits a quad from bezier params; PS is multi-target `PS_OUTPUT` |

### 5.5 Atmosphere (atmosphere.cpp; per-weather-change, not per-frame)

| Pass | File | Dispatch | Bindings |
|---|---|---|---|
| compute_sunSlice | atmosphere/compute_sunlightInAtmosphere.hlsl (32,32,1) | dispatch(16,8) atmosphere.cpp:151 | RWTex gResult=sunlightTexture(82); CB `FogCloudCommonParams` b0 {sun_direction,pad,cloudBase,cloudThickness,pad2} (103-105); CB `FogAtmosphericParams` b1 — full member list `compute_volumeFog.hlsli:56-138`, set field-by-field (107-149) |
| compute_Atmosphere | atmosphere/compute_volumeFogAtmosphericScatter.hlsl (8,8,1) | dispatch(64/8·?,…) = (m_x/8, m_y/8, 1) :211 | RWTex u0-u5 gIn/Outscatter{,_cloudBase,_sky}(87-92); Tex SunInAtmosphere(85), hazePhaseFunction(86), terrainShadow(221); Smp linearSampler/clampSampler(208-209); CBs as above (160-206); PB `gCfd{Texture3D T[12]; float4 offset[12]; float4 scale[12]}` — STRIP-REVIEW leftover of cfd smoke, bound at 226/257 with dummy textures |
| compute_volumeFogLights.hlsl / compute_volumeFogSmokeAndDust.hlsl | atmosphere/ | **DEAD**: include `"CSVolumeFogCommon.hlsli"` which does not exist in the original tree (pristine included); no CPU load site. EarthworksFX created that hlsli to make them compile | CB `FogLights` b2 {volumeFogLight[64]}, `FogVolumes` b3 {fogVolume volumes[8]} declared in compute_volumeFog.hlsli:140-146 — never set by extract CPU code |

### 5.6 Sprites (Sprites.cpp — rocks/static sprite system)

| Pass | File | Draw | Bindings |
|---|---|---|---|
| plantShaderPlants | hlsl/render_sprite.hlsl vs+ps, **TriangleStrip** (Sprites.cpp:116) | renderIndirect(mpIndirectArgs_static, BS, startArg=y*10+x+1, 1) :171,185; dynamic args :196-199. Args written CPU-side via setBlob per 16-byte slot (:97-114, 396-401) — vertexCount=4 per sprite strip? (:94 `vertexCountPerInstance`), instanceCount=block size | CB `gConstantBuffer{view,proj,eye,alpha_pass,start_index}` (139-143,170,184); SB VB=mpSB_static/mpSB_dynamic (`_spriteVertex_packed` 32 B, Sprite_defines.h:4-10 == shader `vertex` render_sprite.hlsl:36-43); Tex gTex/gNorm/gTranclucent Texture2DArrays(118-120); Smp gSampler(121); two-pass alpha (alpha_pass 0/1) with `pAlphaBlendBS` blend + `mDepthStencil`(153) |
| hlsl/sprite.hlsl | — | **UNUSED near-duplicate** of render_sprite.hlsl (1-line diff); never loaded | — |

### 5.7 Post-process

| Pass | File | Draw | Bindings |
|---|---|---|---|
| ▶ tonemapper | hlsl/compute_tonemapper.hlsl vsMain/psMain (it is a *graphics* pass despite the name/folder), TriangleList (Earthworks_4.cpp:138) | drawInstanced(**3**,1) fullscreen tri :278 to swapchain FBO :276 | CB `gConstants{float avsLum}` (never set — avsLum unused in PS as written); Tex hdr=hdrFbo C0 (:273; **declared `register(u0)` in error** — plain SRV), cube=colorCube 33³ RGB32F LUT (:274,186); Smp linearSampler(275). PS: ACES + LUT `cube.Sample(pow(aces,1/2.2)).rgb` |

### 5.8 Include graph (shared contracts)

```
groundcover_defines.hlsli   ← compute_{tileClear,tileBuildLookup,tileSplitMerge,tileGenerate,
      (t_DrawArguments,        tilePassthrough,tileEcotopes,tileDelaunay(via),tileNormals,
       gpuTile, tileLookup,    tileVertices,clipLodAnimatePlants,vegetation_*,terrain_under_mouse,
       instance_PLANT,         sampleRGBtoPixel}, render_{Tiles,tile_sprite,ribbons,triangles,
       GC_feedback,            vegetation_ribbons,Buildings_Far}
       sprite_material)      ← C++: ecotope.h:5, terrafector.h:31, ribbonBuilder.h:6
groundcover_functions.hlsli ← same render/compute set (lu_Pack/unpack, pos/SRTI packing)
terrainDefines.hlsli        ← all tile computes + render_Tiles/tile_sprite/Buildings_Far
      (tile sizes, Terrain_vertex, centerFeedback)  ← C++: ecotope.h:6, terrafector.h:32
vegetation_defines.hlsli    ← compute_{clipLodAnimatePlants,tileClear,tilePassthrough,
      (plant, plant_instance,   vegetation_*}, render_{tile_sprite,vegetation_ribbons}
       block_data, ribbonVertex8, veg_sort, vegetation_feedback) ← C++: ribbonBuilder.h:5
gpuLights_defines.hlsli     ← render_Common.hlsli, compute_tileSplitMerge ← C++: ecotope.h:7, terrafector.h:33
materials.hlsli (terrain/)  ← render_{spline,splineTerrafector,meshTerrafector} (with
      (TF_material,             #define CALLEDFROMHLSL) ← C++: terrafector.h:34 (without)
       PS_OUTPUT_Terrafector, solveUV/solveAlpha/solveElevationColour)
render_Common.hlsli         ← PBR.hlsli, render_meshTerrafector, render_tile_sprite
      (samplers s0-s4, LightsCB b2, atmosphere/shadow textures t7-t23, bicubic, sunLight, JHFAA)
PBR.hlsli (+material.hlsli) ← render_{Tiles,Buildings_Far,triangles,vegetation_ribbons}
compute_volumeFog.hlsli     ← compute_{sunlightInAtmosphere,volumeFogAtmosphericScatter}
      (+ compute_fogCloudAtmosphereCommon.hlsli b0, noise.inc)
compute_bc6h_functions.hlsli ← compute_bc6h
gpuLights_functions.hlsli   ← render_{Tiles,Buildings_Far} (tiled-light loop)
```
`material.hlsli` (root, JohanMaterialCB b1 + MAT_LAYER) is included via PBR.hlsli only.

### 5.9 GPU↔CPU shared structs (layout mismatch = silent corruption)

Compiled by BOTH MSVC and DXC (the hlsli **is** the C++ header):

| Struct | GPU def | C++ inclusion | Size / hazard |
|---|---|---|---|
| `GC_feedback` | groundcover_defines.hlsli:282 | ecotope.h/terrafector.h/ribbonBuilder.h | **1888 B GPU vs 1868 B glm** — float4x4/float3 members 16-B aligned by SPIR-V, packed by glm. EarthworksFX fixed with `padd_align_0..4` + static_asserts (EarthworksFX/hlsl/terrain/groundcover_defines.hlsli:304-383, BRINGUP F9). **Must carry this fix.** |
| `gpuTile` | groundcover_defines.hlsli:203 | same headers; blob upload terrain.cpp:1217 | 48 B; `origin` float3 followed by float — safe, audited (F9 note) |
| `t_DrawArguments` / `t_DispatchArguments` | :42/:50 | same | 16 B scalars — safe |
| `tileForSplit` | :220 | blob terrain.cpp:2253 | float3+float tail — safe (32 B) |
| `instance_PLANT` (8 B, 2 uints) / `xformed_PLANT` (32 B) | :174/:149 | same | safe |
| `sprite_material` | :358 | vegetationBuilder.cpp:250 setBlob | has C++ **default initializers** in-struct — Slang allowed it; DXC does not (EarthworksFX stripped them). 64 B |
| `plant` (+`_plant_lod`,`_plant_anim_pivot`) | vegetation_defines.hlsli:59 | setBlob vegetationBuilder.cpp:3219/3454/3479 | 96 + 16×16 + 48 B; default initializers, same DXC issue; `lods[16]` fixed array — count is load-bearing |
| `plant_instance`, `block_data`, `ribbonVertex8`, `veg_sort`, `vegetation_feedback` | vegetation_defines.hlsli | setBlob 4417/3557/3225/…; feedback readback | block_data 8 B, ribbonVertex8 32 B; vegetation_feedback read back — scalars only, safe |
| `Terrain_vertex`, `centerFeedback` | terrainDefines.hlsli:22/:29 | buffer sizing terrain.cpp:662/593 (tileCenters created as `sizeof(float4)` — matches 16 B) | 8 B / 16 B |
| `TF_material` | materials.hlsli:8 | terrafector.h:34; setBlob terrafector.cpp:775 | 528 B (subMaterials[8] + ecotopeMasks[15]); float2/float3 members packed by *cbuffer-style manual layout* — the `buf_____0x` filler floats are **load-bearing padding**, do not remove |
| `cubicDouble` / `bezierLayer` | render_spline.hlsl:36/:49 (re-declared per shader) | roads_bezier.h:24/:226; setBlob terrain.cpp:1837-1844, 3879-3905, 5098-5170 | 128 B / 8 B; CPU struct has ctors/serialize — layout is just the data members |
| `_spriteVertex_packed` ↔ shader `vertex` | Sprite_defines.h:4 ↔ render_sprite.hlsl:36 | setBlob Sprites.cpp:208/217 | 32 B, manually mirrored (not shared header) — bit-packing comments must stay in sync |
| `ecotopeGpuConstants` | (CPU-only mirror of tileEcotopes `gConstants`) ecotope.h:149 | setBlob terrain.cpp:2416 | cbuffer rules (float4-aligned arrays `ect[12][5]`) |
| `volumeFogLight`/`fogVolume` | compute_volumeFog.hlsli:8/:19 | volumeFogPhaseFunctions.cpp (CPU mirrors) | only consumed by the dead fog-lights passes |

---

## 6. Dependencies (consumes / provides)

- **Terrain → everything**: `buffer_tiles`, lookup buffers, drawArgs, `height_Array`,
  `terrainShadowTexture` are produced by terrain and consumed by billboards, vegetation,
  mouse-picking, atmosphere (terrainShadow).
- **Terrafectors/roads → terrain**: bake passes draw into tileFbo during the tile build
  chain (between tileBicubic and tileEcotopes; ordering in `terrain.cpp` bake path 2560-2800).
  Their material SB (`sb_Terrafector_Materials`, terrafector.cpp:775) + 4096-texture PB is
  shared by spline overlay, spline bake and mesh bake.
- **Vegetation ↔ terrain**: `plants_Root` (a `vegetationBuilder` owned by terrain) receives
  block/plant/instance/drawArgs buffers wiring at terrain.cpp:1026-1050; clipLod compute
  bridges tile plant instances → vegetation draw args.
- **Atmosphere → all lit passes**: inscatter/outscatter/SunInAtmosphere textures bound into
  terrain, billboard, vegetation, triangle shaders (Earthworks_4.cpp:90-118).
- **App (Earthworks_4.cpp)**: owns hdrFbo, previous-frame blit (:281), tonemapper.

## 7. Falcor API surface actually used (shader-interface relevant)

`ComputeProgram/State/Vars` + `GraphicsProgram/State/Vars`; name-based reflection binding:
`Vars()->setTexture/setBuffer/setSampler`, `Vars()["CB"]["member"] = v` (per-member cbuffer
writes), `getParameterBlock("name")` + `ParameterBlock::setBlob` (raw cbuffer upload) and
`findMember("T")` + indexed writes for 4096-texture arrays (`materialCache::setTextures`);
`setUav` with explicit BindLocation (only in dead mipmap code, terrain.cpp:971-979);
`RenderContext::dispatch(state,vars,uint3)` (group counts), `dispatchIndirect`,
`drawIndirect(count, argBuf, offset, nullptr, 0)`, `drawInstanced`, `drawIndexedInstanced`,
`blit`, `clearFbo`, `clearUAV`; `Buffer::createStructured` (structured+UAV counter),
`Buffer::setBlob`, `getUAVCounter()->setBlob` (counter reset); `Fbo::create2D(w,h,desc,
samples,arraySize)` with per-target formats + `true`=UAV-capable flag; `Texture::create2D/3D`
(incl. array slices + BC formats); `GraphicsProgram` GS path forces SM 6_5;
Slang-only syntax in shaders: `ParameterBlock<T>` (6 shaders + gCfd), member default
initializers in shared structs, untyped `RWTexture2D`.

## 8. Port drift notes & DXC pitfalls (per drifted file)

Diff basis: `diff -w` extract vs `EarthworksFX/hlsl/`. Classification:
**[DXC]** = necessitated by DXC/Diligent, carry the *technique* forward;
**[NEWER]** = extract is newer than the June import in the port — extract is authority;
**[PORT-DEBUG]** = port-side bring-up instrumentation, optional;
**[SUSPECT]** = port rewrite worth re-checking.

| File (Δ lines) | Classification |
|---|---|
| render_vegetation_ribbons.hlsl (108) | [DXC] `ParameterBlock textures.T[4096]` → flat `textures_T[4096]`. [NEWER] extract adds `camRight/camUp/toneMap/padd1` cbuffer members (**layout change!**), camera-facing point-sprite GS branch (`diamond & 0x2`, :629-667), `packDiamond` mask 0x3 vs 0x1, VS worldPos-scale lines disabled (:443-446), bake oval smoothstep 0.90 vs 0.8. Port version must NOT be used as source. |
| compute_volumeFog.hlsli (77) | [DXC] `ParameterBlock<cfdTextures> gCfd` → 12 loose Texture3D + explicit `cbuffer gCfdParams : register(b4)` for offsets/scales — DXC puts loose globals into `$Globals`, which Diligent D3D12 rejects (empty resource name). Port also adds `CSVolumeFogCommon.hlsli` to unbreak the fog-lights includes. |
| render_spline / splineTerrafector / meshTerrafector (51/38/28) | [DXC] PB→flat `gmyTextures_T[4096]` and **include-order flip**: texture array must be declared *before* `#include "materials.hlsli"` since materials.hlsli references it. meshTerrafector also has [PORT-DEBUG] `dbgTagSuspicious` elevation tagging (tile-hole hunt). |
| groundcover_defines.hlsli (49) | [DXC] GC_feedback SPIR-V alignment pads + static_asserts (§5.9, F9) — **must survive**; sprite_material default-initializers removed. |
| compute_volumeFogAtmosphericScatter.hlsl (37) | [DXC] `RWTexture{2,3}D<float3>` → `<float4>` (Vulkan OpImageWrite component-count rule) + `[[vk::image_format("r11f_g11f_b10f"/"rgba16f")]]` annotations (F17). Caveat noted in file: annotation is wrong if `mainNear` (RGBA16F 3D) is ever bound. |
| compute_tonemapper.hlsl (32) | [DXC] `hdr : register(u0)` → `t1` (DXC rejects SRV in u-register). [SUSPECT→resolved] port's 6-vertex fullscreen-quad VS was a workaround for the F22 drawInstanced-indexed bug, later fixed in the compat layer; original 3-vertex `(vId<<1)&2` tri is fine once drawInstanced is truly non-indexed. Port adds debugView modes [PORT-DEBUG]. |
| compute_tileVertices.hlsl (21) | [SUSPECT/fix] port replaces the single center-height tap with a median-of-5 (tile-hole fix for terrafector-carved strips). Deliberate behavioural fix — decide whether to keep; it's a real robustness improvement, not DXC. Also `groupshared uint vCount = 0;` initializer removed (DXC disallows groupshared initializers) [DXC]. |
| compute_tileBuildLookup.hlsl (15) | [DXC] `ParameterBlock<views>` (3×18 RWSB arrays) → port-invented `viewRenderData_lookupBuffers.hlsli` with `Store*Lookup()` helpers — arrays of RW buffers in one block are Slang-only. A Diligent re-port needs an equivalent strategy (54 individually named buffers, or 3 buffers with per-view offsets — **decide explicitly**). |
| compute_vegetation_lod.hlsl (12) | [NEWER] extract computes `pixBoost` separately and uses it *only* for the billboard cutover (:87,94); port boosts `pix` itself → changes LOD selection when looking down. Default-initializers in cbuffer members removed [DXC]. |
| compute_vegetation_sortCombine.hlsl (10) | [NEWER] extract sets `startInstanceLocation=0` (SM 6.8 comment); port writes `sort[i].offset` (harmless pre-6.8 but misleading). Port lost the `#if _PRE/#else` structure? — no: port keeps only PRE body; **extract has both PRE and POST variants; port's POST re-allocation block missing** → check before reuse. |
| compute_bakeFloodfill (13), compute_tileEcotopes (15) | [DXC] untyped `RWTexture2D` → `RWTexture2D<float4>`/`<float>`; swizzle-assign on UAV load (`gAlbedo[crd].a = 0`) → read-modify-write local. PB→flat for the 256-texture array. |
| render_Tiles (9), render_tile_sprite (12), render_ribbons (25), render_Buildings_Far (8), materials.hlsli (22), vegetation_defines (16), PBR.hlsli (3), noise.inc (5), bc6h_functions (6) | [DXC] mechanical: PB→flat rename, default-initializer strip; render_Tiles adds [PORT-DEBUG] gConstColor world-pos pattern. |
| Port-only files | `CSVolumeFogCommon.hlsli`, `viewRenderData_lookupBuffers.hlsli`, `debugGrid.hlsl` — port-side creations, not originals. |

Relevant BRINGUP F-findings encoding engine semantics (do not re-derive): **F9** (SPIR-V
struct alignment), **F10** (FBO colors need UAV bind flag), **F13/F18** (4096-descriptor
arrays blow Vulkan dynamic pool / D3D12 GPU heap — needs enlarged pools), **F14** (bind
resources in *every* stage), **F15** (arrayed SRVs need SetArray, not element 0), **F16**
(never CpuAccess::Write for once-written buffers → USAGE_DYNAMIC discard), **F17** (storage
image format annotations), **F22** (drawInstanced is always non-indexed), F25
(indirect draws need the stub-free buffer path). Camera: RH, CCW front faces, depth [0,1],
film-back FOV (PROJECT_OVERVIEW).

## 9. Bad code / removable candidates (OPINION — do not act)

- `hlsl/sprite.hlsl` — unused duplicate of `render_sprite.hlsl` (never loaded).
- `compute_bc6h.hlsl` `gConstants` (view/proj/eye) — copy-paste, never set, unused in shader.
- `compute_vegetation_sortCombine.hlsl` `_COPY` branch (:22-31) — references undeclared `i`;
  cannot compile; dead.
- `compute_tileElevationMipmap` block terrain.cpp:969-980 — commented out; the shader file
  does not exist anywhere (incl. pristine).
- `compute_volumeFogLights.hlsl` + `compute_volumeFogSmokeAndDust.hlsl` — broken include in
  the *original*, no load site; dead until smoke/fog-lights return.
- `gCfd` ParameterBlock in compute_volumeFog.hlsli — cfd smoke leftover (glider strip);
  bound with dummies; candidate for removal *if* the shader code paths using it
  (`SmokeUV` at :244) are also stubbed.
- `render_ribbons.hlsl` (legacy grass) — loaded and fully bound but its draw is commented
  out (terrain.cpp:2991); superseded by render_vegetation_ribbons. Confirm with developer
  before dropping.
- Duplicate `LightsCB` member-set blocks repeated per shader (terrain.cpp:1761-1776,
  vegetationBuilder.cpp:4006-4020) — a shared CB object would collapse these (port-quality
  concern only).
- `compute_terrain_under_mouse` declares `gDepthBuffer` never bound; PS-side
  `fogDensities[32]` in compute_volumeFog.hlsli never bound.

## 10. Open questions / uncertainties

1. `render_ribbons` (RV6 path): kept alive intentionally as reference, or awaiting
   revival? No active draw in extract.
2. `Sprites.cpp` (render_sprite): is this subsystem in scope for the port? It renders only
   via `sprites` object; didn't verify it is called from the active frame path.
3. `compute_vegetation_lod` SHADOW_TILE/SHADOW_OUTER variants are never loaded — shadow
   drawArgs (`DrawArgs_Shadows_*`) never fed; shadow rendering instead reuses the main
   drawArgs with the `_DEPTH` shader. Confirm that cascade shadows (cascadeShadowMaps.cpp is
   ~stubbed) are indeed future work, not lost functionality.
4. Exact bake-time terrafector pass ordering/state within tile rebuild (which passes write
   which tileFbo planes in what order, incl. `depthstateAll` rationale) belongs to the
   terrafector catalog doc; only bindings are covered here.
5. `tileCenters` buffer is created as `sizeof(float4)` but typed `centerFeedback` in
   shaders — same 16 B, but name/type mismatch worth normalizing.
6. `render_tile_sprite` binds `PerFrameCB` with the same GIS members as render_Tiles but
   CPU only sets them on terrainShader (terrain.cpp:675-678) — sprite pass reads defaults
   (zeros). Intentional?
7. The `viewMask` hardcoded at compute_tileBuildLookup.hlsl:149 excludes cubeEnv views —
   are env-cube renders driven elsewhere or unfinished?
