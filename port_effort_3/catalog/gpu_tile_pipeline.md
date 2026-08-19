# Concept Catalog — GPU Tile Pipeline (tile-bake compute chain + terrain render dispatch)

Sources analyzed: `port_effort_3/source_extract_3/` — `earthworks_scene/terrain.{h,cpp}` (bake
orchestration, lookup building, camera/frustum setup, indirect dispatch), `pixelShader.{h,cpp}`,
`computeShader.h`, and `hlsl/terrain/`: `compute_tileClear`, `compute_tileSplitMerge`,
`compute_tileGenerate`, `compute_tileEcotopes`, `compute_tileDelaunay`, `compute_tileJumpFlood`,
`compute_tileBicubic`, `compute_tileNormals`, `compute_tileVertices`, `compute_bakeFloodfill`,
`compute_bc6h(+_functions)`, `compute_tilePassthrough`, `compute_tileBuildLookup`,
`render_Tiles.hlsl`, `render_tile_sprite.hlsl`, `render_triangles.hlsl`,
`terrainDefines.hlsli`, `terrainFunctions.hlsli`, `groundcover_defines.hlsli`,
`groundcover_functions.hlsli`, `materials.hlsli`.
All file:line refs are into `port_effort_3/source_extract_3/` unless prefixed.

Scope split: CPU quadtree/split-merge decisions, JP2 streaming, and LRU caches belong to
`terrain_quadtree_streaming.md`; per-pass binding minutiae to `shader_interface.md`; the
`shadow()`/`sunLight()`/atmosphere functions consumed by the render shaders to
`atmosphere_shadows.md`. This doc owns everything from "a tile has its source data on the GPU"
to "pixels on screen": the split-time bake compute chain, the per-frame lookup/indirect-arg
build, and the indirect terrain/billboard draws.

---

## 1. Purpose & data flow

Two distinct cadences share the same resources:

### A. Split-time (async, at most ONE 4-child split per frame — `splitOne`, terrain.cpp:2202)

For each of the 4 new children, `splitChild(tile)` (terrain.cpp:2316) runs the **bake chain**
through ONE shared set of 256×256 intermediates (`split.tileFbo` + scratch textures), then
copies results into per-tile array slices. Order (all on the same immediate context — ordering
is the only synchronization):

| # | Step | Where | In → Out |
|---|------|-------|----------|
| 0 | `compute_tileSplitMerge` (1,1) — seed 4 gpuTile entries (flags=0, origin, scale_1024=size/1024, lod/X/Y, counters=0) | terrain.cpp:2252-2254; compute_tileSplitMerge.hlsl:18-35 | cbuffer `gConstants{tileForSplit child[4]}` → `tiles[]` |
| 1 | `clearFbo` + per-RTV clears (PBR=(1,.07,1,0), ect planes to fixed colours) | terrain.cpp:2326-2336 | → tileFbo (8 MRT) |
| 2 | `compute_tileBicubic` isHeight=1, dispatch (32,32) | terrain.cpp:2339-2357; compute_tileBicubic.hlsl:50-93 | streamed 1024² R16Unorm elevation → tileFbo color0 (R32Float, metres) |
| 3 | `compute_tileBicubic` isHeight=0 (only if orthophotos exist) | terrain.cpp:2359-2374; hlsl:94-111 | 1024² RGBA8Srgb ortho → tileFbo color1 (albedo, with hardcoded gamma tweaks hlsl:101-106) |
| 4 | `splitRenderTopdown` — rasterize terrafector meshes + road splines + stamps top-down into the tileFbo with an ortho camera (elevation color0 IS a render target here: terrafectors overwrite height) | terrain.cpp:2554-2793 | terrafector VB/IB per LOD grid + spline buffers → tileFbo 0–7 |
| 5 | `copySubresource` color0 → `height_Array[tile.index]` (picking/terrain-under-mouse only) | terrain.cpp:2378 | |
| 6 | `compute_tileEcotopes` (32,32), only if numEcotopes>0 — procedural albedo blend + **billboard-quad spawning** | terrain.cpp:2384-2420; compute_tileEcotopes.hlsl:71-265 | tileFbo 0/3/4-7 + 24 ecotope textures + noise → color1 rewrite, `quad_instance`, `tiles[].numQuads` |
| 7 | `compute_tilePassthrough` (128,1)×256 threads — inherit parent's quads/plants into this child | terrain.cpp:2423-2430; compute_tilePassthrough.hlsl:45-114 | parent `quad_instance` slots → child quad/plant slots |
| 8 | copy `vertex_clear`(zeros) → `vertex_B_texture`, `vertex_preload`(lattice) → `vertex_A_texture` | terrain.cpp:2436-2437 | |
| 9 | copy tileFbo color1 → `compressed_Albedo_Array[idx]` (albedo BC6H **deliberately disabled**, terrain.cpp:2443-2447) | | |
| 10 | `compute_tileNormals` (32,32) — cross normals, *0.5+0.5 encode | terrain.cpp:2451-2453; compute_tileNormals.hlsl:31-47 | color0 → `normals_texture` (R11G11B10) |
| 11 | `compute_tileVerticis` (16,16) — pick mesh vertices on a 128² half-res grid, 3 hierarchical passes | terrain.cpp:2456-2468; compute_tileVertices.hlsl:65-131 | color0 → `vertex_A_texture` (adds to preload), `tiles[].numVerticis`, `tileCenters[idx]` |
| 12 | copy `normals_texture` → `compressed_Normals_Array[idx]` | terrain.cpp:2473 | |
| 13 | `compute_tileJumpFlood` ×3, steps 4,2,1, ping-pong A→B→A→B | terrain.cpp:2504-2527; compute_tileJumpFlood.hlsl:18-40 | Voronoi ownership of nearest vertex; final result lands in `vertex_B_texture` |
| 14 | `compute_bc6h` (8,8) on tileFbo color2 → `bc6h_texture`, copy → `compressed_PBR_Array[idx]` | terrain.cpp:2530-2533; compute_bc6h.hlsl:27-75 | PBR is the ONLY BC6H-compressed plane |
| 15 | `compute_tileDelaunay` (16,16) — triangulate the Voronoi map into the per-tile VB region | terrain.cpp:2537-2543; compute_tileDelaunay.hlsl:29-100 | `vertex_B` + color0 → `VB[idx*numVertPerTile…]`, `tiles[].numTriangles` |

After the 4 children are baked, `testForSplit` re-runs on them so their frustum flags are fresh
(terrain.cpp:2260-2263).

### B. Per-frame (update + render)

`terrainManager::update` (terrain.cpp:1783-1946):
1. **Readback** `buffer_tileCenters` → CPU (copyResource + map = full stall, terrain.cpp:1901-1913);
   patches `tile->origin.y` / `boundingSphere.y` for every used tile whose `.x > 0`.
2. `compute_tileClear` (1,1) — one thread zeroes ALL 18 views' draw/dispatch args and feedback
   counters (compute_tileClear.hlsl:18-115).
3. `calculateSurfaceFlags()` (terrain.cpp:1581-1607) — CPU frustum test of every used tile
   against all active cameras → `frustumFlags[1024]` (uint4 per tile; `.x` = *surface* flags
   (renderable leaf) + bit31 active marker, `.y` = *visible* flags (plants)).
4. Upload `frustumFlags` as a raw **16 KB cbuffer blob** into `compute_tileBuildLookup`'s
   `gConstants` (terrain.cpp:1923-1924: `getParameterBlock("gConstants")->setBlob(frustumFlags, 0, 1024*sizeof(uint4))`).
5. `compute_tileBuildLookup` dispatch `((numTiles+31)>>5, 1)` = (32,1) groups × 32 threads
   (terrain.cpp:1925-1929) — builds the per-view lookup buffers AND the indirect args (§2.2).

`terrainManager::onFrameRender` (terrain.cpp:2823-3180), main view only:
1. `compute_clipLodAnimatePlants.dispatchIndirect(dispatchArgs_plants, 0)` (terrain.cpp:2896)
   — vegetation clip/LOD (vegetation doc), consuming `buffer_lookup_plants[Main_Center]`.
2. **Terrain**: `terrainShader.renderIndirect(drawArgs_tiles, nullptr, CameraType_Main_Center, 1)`
   (terrain.cpp:2910) → non-indexed `drawIndirect`, byte offset `1*16` (pixelShader.cpp:47).
3. **Billboards**: `terrainSpiteShader.renderIndirect(drawArgs_quads, nullptr, Main_Center, 1)`
   (terrain.cpp:2936), PointList + GS expansion.
4. Skydome `triangleShader.drawInstanced(36,1)` (terrain.cpp:2966); road-spline overlays
   (editor); `compute_TerrainUnderMouse` (1,1) + `buffer_feedback` readback (mouse picking,
   terrain.cpp:3074-3113).
5. `debug==true` blits all 8 tileFbo planes + debug_texture onto the backbuffer every frame
   (terrain.cpp:3116-3133) — active because `COMPUTE_DEBUG_OUTPUT` is unconditionally defined
   (groundcover_defines.hlsli:40 → terrain.h:485-489).

Only `CameraType_Main_Center` (=1) is ever rendered by the current code; cascade/parabolic
views are *packed* by buildLookup but no code consumes their args yet (grep: `renderIndirect`
on `drawArgs_tiles` only at terrain.cpp:2910).

There is also an **offline export bake** (`bake_start/bake_frame/bake_Setup`,
terrain.cpp:3184-3524): re-runs bicubic + topdown into the 1024² `bakeFbo`, reads back
elevation, finds min/max, re-encodes to JP2 (OpenJPH) for the EVO export. Same shaders,
different FBO and constants; not per-frame.

---

## 2. Core tricks & clever mechanisms (a rewriter MUST NOT lose these)

### 2.1 GPU re-triangulation: adaptive vertices → Jump-Flood Voronoi → "Delaunay" quads
The entire terrain mesh is (re)generated on the GPU per split, with triangle density adapting
to curvature:
- `compute_tileVertices.hlsl:40-61` `testPixel`: a vertex is planted where the centre height
  deviates from the average of 4 diagonal samples by more than `pixelSize * cutoff`. Three
  hierarchical passes on the 128² half-res grid: stride 4 (cutoff 0.1), stride 2 (0.15),
  stride 1 (0.1 or 0.15 depending on running count — hlsl:107-108, a density feedback).
- The vertex texture is **pre-seeded** (`vertex_preload`, terrain.cpp:566-589) with (a) a
  border ring at rows/cols 1 and 127 every 2 px ("kante" = edges) so tile borders are always
  dense enough to stitch neighbours without cracks, (b) rings at 5/125 every 4 px, (c) an
  interior lattice every 8 px — guarantees a minimum triangulation even on flat ground.
- Vertices are stored as **self-codes**: texel value `(y<<7)+x` (its own coordinate,
  compute_tileVertices.hlsl:90); 0 = empty. This makes JFA trivially seedable.
- `compute_tileJumpFlood` runs exactly 3 iterations with steps **4,2,1** (terrain.cpp:2507-2523)
  — hand-tuned ("ek weet 32 en 6 loops is goed" comment terrain.cpp:2503 refers to earlier
  tuning); ping-pong A→B→A→B so the final ownership map is in `vertex_B_texture`, which is
  what Delaunay binds (terrain.cpp:963).
- `compute_tileDelaunay.hlsl:83-100`: for every 2×2 cell of the ownership map where the
  diagonal owners differ, emit 1–2 triangles between the 4 owning vertices; the 4-bit
  `flag` of pairwise inequalities (hlsl:33) selects the topology. Vertex height = average of
  the 4 full-res texels around the vertex (hlsl:22-26) — a built-in low-pass.
- Output `Terrain_vertex{uint idx; float hgt}` (terrainDefines.hlsli:22-26, 8 bytes) into a
  fixed per-tile VB window `tile.index * numVertPerTile` (=32768 slots, terrainDefines.hlsli:11).
  Triangle count via `InterlockedAdd(tiles[t].numTriangles)`.

### 2.2 GPU-driven rendering: 64-item blocks, packed lookups, indirect args
`compute_tileBuildLookup.hlsl` turns per-tile counters into per-view render work:
- Work is chopped into **blocks of 64** (triangles / quads / plants). Lookup entry =
  `lu_Pack(tile,blockOffset,used)` = `tile<<20 | offset<<8 | used` (12/12/8 bits,
  groundcover_functions.hlsli:5-8). Consumers decode with `lu_Tile/lu_Used/lu_Index`.
- **Terrain** (`packTile`, hlsl:56-80): `numBlocks=(numTriangles>>6)+1`;
  `InterlockedAdd(DrawArgs_Terrain[view].instanceCount, 64*numBlocks)`, vertexCountPerInstance
  stays 3 (set by tileClear). So the draw is `Draw(3 verts, 64*numBlocks instances)`: one
  *instance = one triangle*, `iId>>6` finds the lookup block, `iId&0x3f` the triangle within
  it (render_Tiles.hlsl:76-83).
- **Billboards** (`packBillboard`, hlsl:84-108): instanceCount += numBlocks, vertexCount=64:
  one *instance = one 64-quad block*, one *vertex = one point* expanded to a quad by the GS
  (render_tile_sprite.hlsl:136-160).
- **Plants** (`packPlants`, hlsl:111-134): fills `DispatchArgs_Plants[view].numGroupX` — the
  same block-lookup pattern drives a **dispatchIndirect** instead of a draw.
- Slots past `used` are killed in the VS by `pos = float4(0,0,0,0)` (degenerate triangle,
  render_Tiles.hlsl:132-136) — no CPU involvement, no index buffer, no compaction pass.
- All of this is rebuilt from scratch EVERY frame in one 1024-thread dispatch; the only CPU
  input is the 16 KB frustumFlags cbuffer.

### 2.3 All indirect draws are NON-INDEXED with 16-byte args (verified)
- `t_DrawArguments{vertexCountPerInstance, instanceCount, startVertexLocation,
  startInstanceLocation}` — 4 uints, **16 bytes** (groundcover_defines.hlsli:42-48). This is
  the D3D12 `D3D12_DRAW_ARGUMENTS` layout, NOT the 20-byte indexed layout.
- `pixelShader::renderIndirect` → `drawIndirect(state, vars, numArgs, argBuf, startArg*16,
  nullptr, 0)` (pixelShader.cpp:43-48) — byte offset = viewIndex*16, count buffer null.
- `t_DispatchArguments{numGroupX,Y,Z, padd}` (groundcover_defines.hlsli:50-56): the `padd`
  member is **load-bearing** — it makes the structured-buffer stride 16, so element `i` sits
  at byte offset `i*16`; `compute_tileClear.hlsl:67-70` writes all four fields including
  `padd=0`. Deleting it changes every element offset AND the buffer size
  (`createStructured(sizeof(t_DispatchArguments), numRenderViews)`, terrain.cpp:644).
  A past porting agent deleted "unused" padding and broke rendering — do not repeat.
- `compute_tileClear` initializes per view: Terrain `{vtx=3}`, Quads `{vtx=64}`, Plants(draw)
  `{vtx=VEG_BLOCK_SIZE}`, Dispatch `{0,1,1,0}` (compute_tileClear.hlsl:55-70).
  `startVertexLocation/startInstanceLocation` are NEVER written anywhere — the code relies on
  the buffers being zero at creation (see §10).

### 2.4 The 16 KB frustumFlags cbuffer (CPU→GPU visibility, one blob)
`cbuffer gConstants { uint4 frustumflags[1024]; }` (compute_tileBuildLookup.hlsl:36-39),
uploaded as a raw `setBlob` of the CPU array `uint4 frustumFlags[1024]` (terrain.h:481,
terrain.cpp:1923-1924). Semantics (terrain.cpp:1581-1607):
- `.x` bit31: tile active; `.x` bit i (i<18): tile is a **renderable surface** for camera i —
  true only for frustum-visible tiles that are *leaves of a splitting parent*
  (`parent->main_ShouldSplit && child[0]==nullptr`, terrain.cpp:1597) — the resolution set.
- `.y` bit i: tile merely frustum-visible for camera i (plants pack from this, so vegetation
  persists on coarser ancestor tiles).
- `.z/.w`: unused (padding to uint4 — keep; the blob size and HLSL array stride assume it).
- The GPU copies `.x` into `tiles[t].flags` (hlsl:156) — that's how the render/other shaders
  see visibility; `flags==0` also clears `tileCenters[t].min=0` (hlsl:158-161) so the CPU
  readback stops patching heights of dead tiles.

### 2.5 CPU frustum test as one matrix multiply
`setCamera` (terrain.cpp:1688-1737) extracts the 4 side planes Gribb-Hartmann-style from the
**projection** matrix (transposed layout — note comment terrain.cpp:1701) and packs them as
rows of `frustumMatrix`. The per-tile test is then
`test = saturate(viewBS * frustumMatrix + bsSize); visible = all(test)`
(terrain.cpp:1591-1593) — 4 plane distances in one mat4 multiply, `saturate+all` instead of
branches. Bounding-sphere slack differs on purpose: **1.0×size** for surface flags
("missing here is FATAL", terrain.cpp:1588) vs 0.9×size in `testForSplit` (terrain.cpp:1624).
Near/far planes are deliberately not tested.

### 2.6 12-bit height packing anchored at a floating tile origin
Instance positions (`instance_PLANT`, groundcover_defines.hlsli:174-179) pack height as
`uHgt = (height - origin.y) / scale_1024` into 12 bits — range = 4096·(size/1024) = **4× the
tile size**, centred by making `origin.y = centerHeight − 2·size`:
- GPU: `compute_tileVertices.hlsl:73-79` samples the tile centre (0.5,0.5), writes
  `tileCenters[idx].min = centerHeight` and `tiles[idx].origin.y = centerHeight − scale_1024*2048`.
- Same "corner origin" recomputed locally in `compute_tileEcotopes.hlsl:76` and
  `compute_tilePassthrough.hlsl:89` (from `gHgt[uint2(128,128)]` — the FBO centre texel)
  because those run BEFORE tileVertices in the chain.
- CPU mirror: `quadtree_tile::set` seeds `origin.y = parent->boundingSphere.y − size*2`
  (terrain.cpp:345-354), then the per-frame `tileCenters` readback refines it
  (terrain.cpp:1906-1913, guarded by `.x > 0`).
This closed CPU↔GPU loop is exactly where the current port's "tiles sent to y=0" bug lives —
in the ORIGINAL, a tile whose centre sample is ≤0 simply keeps its inherited parent height
(the `> 0` guard); the origin.y triple-redundancy (vertices/ecotopes/passthrough) must all
agree or heights unpack wrong.

### 2.7 256-px tile with 4-px border (the 248 convention)
`tile_numPixels 256 / tile_BorderPixels 4 / tile_InnerPixels 248 / tile_toBorder 256/248`
(terrainDefines.hlsli:3-9). The inner 248 px cover the tile's world size; 4-px skirt on each
side gives filter support (bicubic, normals, floodfill across edges). Consequences that must
survive:
- bake camera scales by `256/248` (`s *= 256.0f/248.0f`, terrain.cpp:2568);
- bicubic applies `crd − 4.0` (compute_tileBicubic.hlsl:62,96);
- world reconstruction: `origin.xz + (pix − 4) * scale_1024 * 4 * tile_toBorder`
  (render_Tiles.hlsl:91) — note `scale_1024*4*(256/248) = size/248` = world-per-inner-pixel;
- billboard unpack multiplies x/z by `1.032258` (= 256/248) (groundcover_functions.hlsli:73);
- ecotope plant spawn excludes the border (`crd<4 || crd>252` → no plant,
  compute_tileEcotopes.hlsl:228-235);
- passthrough child split threshold is **496** = 2·248 half-pixels (compute_tilePassthrough.hlsl:72-73).

### 2.8 Quad→plant inheritance instead of re-generation
On split, children do NOT regenerate vegetation: `compute_tilePassthrough` redistributes the
parent's packed quads to the correct child quadrant (bit arithmetic on the 10-bit coords,
hlsl:67-78), re-anchors the height to the child origin, and **promotes** an instance from
billboard-quad to full plant when `plantY / tile.scale_1024 / 2 > 15` (hlsl:93-103) — i.e.
promotion by *relative* size as tiles shrink, no camera term. Spawning itself happens once,
in `compute_tileEcotopes.hlsl:204-252` (density from `plantDensity` per ecotope per lod,
deterministic noise texture, `InterlockedAdd(tiles[].numQuads)`), plus a dead lod==8 path in
`compute_tileGenerate` (§9).

### 2.9 Selective compression + one shared scratch
Only PBR goes through the GPU BC6H compressor (`compute_bc6h`, one-pass `EncodeP1`, quality
loop disabled, hlsl:48-56); albedo BC6H exists but is commented out (terrain.cpp:2443-2447)
and normals compression likewise (terrain.cpp:2474-2479) — a deliberate quality/perf trade.
All per-tile bakes run through ONE tileFbo/normals/vertex texture set and are then
`copySubresource`d into the big arrays (`compressed_*_Array`, `height_Array`, all
`numTiles`=997 slices, terrain.cpp:634-638) — no per-tile allocations, ever.

### 2.10 Terrafectors bake INTO the elevation target
`splitRenderTopdown` renders terrafector mesh layers with color0 (elevation) as a render
target — terrafectors literally rewrite terrain height at bake time, selected per LOD from
pre-combined grids (LOD2/4/6 mesh caches, LOD7 stamps, LOD4/6 "top" layers, ordered
bakeLow → roads → bakeHigh → overlay → stamps → top; terrain.cpp:2617-2792). The ortho
camera is a hand-built axis-swap matrix (V rows: X→X, Z→Y, −Y→Z, terrain.cpp:2562-2569) with
`glm::orthoLH(−s,s,−s,s,−10000,10000)`. Roads use `drawIndexedInstanced(64*6, count)` with
the shared 128-quad-pattern IB. Details of the terrafector content are the roads/terrafector
doc's territory; the *pass structure and its position in the chain* (after bicubic, before
ecotopes) is load-bearing here.

---

## 3. Invariants & conventions

| Invariant | Where | Note |
|---|---|---|
| `numRenderViews == 18`, order left,main,right,rearL/C/R,casc0-3,cube0-5,para_low,para_med | groundcover_defines.hlsli:8-30; terrain.h:242-262,819-820 | CPU `CameraType` enum and shader bit defines MUST stay in lockstep |
| Indirect args: 16-byte non-indexed layout, element i at byte i*16 | groundcover_defines.hlsli:42-56; pixelShader.cpp:47 | see §2.3 |
| Tile 0 (quadtree root) is never rendered: buildLookup skips `t==0` | compute_tileBuildLookup.hlsl:152 | root also draws nothing until the first split (surface flag needs a splitting parent) |
| Thread guard `t < 1000` vs `numTiles = 997` vs cbuffer `[1024]` | hlsl:152; terrain.h:477,481 | three different constants that must satisfy 997 ≤ 1000 ≤ 1024 |
| Shader-side hardcoded `viewMask = main_CENTER\|cascade_0..2\|parabolic_*` | compute_tileBuildLookup.hlsl:149 | CPU `terrainManager::viewMask` (terrain.h:822, recomputed in setCamera 1729-1736) is NOT uploaded — main_LEFT packs nothing even though the CPU mask includes it |
| `packPlants` gate is `(visibleFlags & viewMask)` — **no `(1<<view)` term** | hlsl:187 | plants pack into ALL 18 views' args whenever visible in ANY masked view; the clipLod dispatchIndirect then reads **offset 0** (view 0!) (terrain.cpp:2896) and works only because of this. "Fixing" either side alone breaks vegetation |
| Lookup block size 64 everywhere (`>>6`, `&0x3f`, `min(x,64)`) | hlsl:61,89,116; render_Tiles.hlsl:76-79; render_tile_sprite.hlsl:88-90 | encoded in lu_Pack bit layout AND in `DrawArgs_Quads.vertexCountPerInstance=64` |
| lu_Pack limits: ≤4096 tiles, ≤4096 blocks/tile, ≤255 used | groundcover_functions.hlsli:5-8 | 4096 blocks × 64 = 262144 items/tile max |
| Per-tile VB window: `tile.index * numVertPerTile` (32768) | compute_tileDelaunay.hlsl:32; render_Tiles.hlsl:78 | max ~10922 triangles/tile; **no overflow clamp** in Delaunay |
| Heights are quantized to 12 bits over 4×tileSize anchored at origin.y = center − 2·size | §2.6 | origin.y computed in 3 shaders + CPU; all must agree |
| `tiles[].flags` is GPU-written from the cbuffer each frame; everything else in gpuTile is split-time | groundcover_defines.hlsli:203-217 | CPU reset() re-uploads the whole array zeroed (terrain.cpp:1201-1217) |
| One split per frame, children baked serially through shared scratch | terrain.cpp:2202-2265 | no fences needed; a parallel rewrite must add its own hazards |
| `buffer_terrain` UAV counter zeroed per child ("I misuse increment", terrain.cpp:2539-2541) | | current shaders never call IncrementCounter — counter appears vestigial, but the buffer MUST still be created counter-capable in Falcor terms |
| Frustum matrices: proj-only side planes, row-packed, `saturate(viewBS*M + r)`/`all` | terrain.cpp:1701-1726,1591-1593 | camera conventions per EarthworksFX/PROJECT_OVERVIEW.md "Matrix & camera conventions" (RH camera, row-vector HLSL mul, [0,1] depth, CCW front) |
| `COMPUTE_DEBUG_OUTPUT` always defined (shared header) — gates feedback InterlockedAdds AND the CPU `debug=true` blits | groundcover_defines.hlsli:40; terrain.h:485-489 | port may want it off; original ships with it ON |

---

## 4. Performance-critical details

- **The whole render is 2 CPU draw calls + 2 dispatches per view** regardless of tile count.
  Anything that reintroduces per-tile CPU draws destroys the design.
- **Block size 64** balances VS wave occupancy vs lookup-buffer size. The FIXME at
  compute_tileBuildLookup.hlsl:65 ("cant we just use numBlocks and vertexCount 3*64?")
  explains the terrain oddity: 3-vertex instances × 64·numBlocks instances. Non-indexed
  3-vert instancing is unusual but deliberate — the known-good baseline. A rewriter may be
  tempted to switch to `vertexCount=192, instances=numBlocks`; that changes wave packing and
  MUST be measured, not assumed (the author left it as-is on purpose; see also the
  interleaving complaint terrain.cpp:1930-1931 — known-suboptimal but tuned-around).
- **Per-view lookup buffer sizes are asymmetric and hand-tuned** (terrain.h:808-818):
  terrain `{524288, 524288, 256, 256, 256, 256, 1024×4, 65536×6, 16384, 32768}`,
  quads/plants `{256, 131072, 256, …}`. Comment: "Zero is not allowed". 524288 blocks = 33M
  triangles headroom for main; shrinking these saves memory but overflow is UNGUARDED (only
  the debug `maxTriangles` feedback would reveal it).
- **Split-time cost is bounded** by the one-split-per-frame rule and by data-readiness gating
  (`hashAndCache*` return false → split deferred, terrain.cpp:2213-2226) — streaming hitches
  become at worst a one-frame delay, never a multi-split spike.
- **Two per-frame full stalls exist in the original**: tileCenters readback
  (terrain.cpp:1901-1905) and GC_feedback readback (terrain.cpp:3083-3087) — copy + immediate
  map(Read). They are part of the tuned baseline (Falcor flushes and waits); replacing them
  with latency-tolerant readback is safe ONLY if origin.y patching tolerates N-frame-old data
  (it does — it's a refinement loop), but mouse picking (`tum_*`) gets laggier.
- **JFA step sequence 4,2,1 and 3 iterations** (terrain.cpp:2507-2522): on the 128² grid this
  covers ±7 px around each seed; the preload lattice guarantees a seed within that radius.
  More iterations = slower; fewer/other steps = holes in the Voronoi map = missing triangles.
- **Degenerate-kill (`pos=0000`)** in VS instead of exact args: keeps buildLookup branch-free
  and arg math trivial; the rasterizer eats degenerates for free. Don't "optimize" by making
  counts exact per block.
- `compute_tileClear` is a SINGLE thread looping 18 views + arrays (hlsl:18-115) — trivially
  parallelizable but ~free anyway; don't spend time there, but ALSO don't accidentally run it
  more than once per frame (args are `+=` accumulated by buildLookup).
- BC6H one-pass `EncodeP1` only, quality loop commented out (compute_bc6h.hlsl:48-74) —
  re-enabling `EncodeP2Pattern` costs ~32× on that pass.
- Albedo deliberately NOT BC-compressed (terrain.cpp:2443-2447): quality decision; flipping it
  changes look, not just speed.
- render_Tiles.hlsl psMain: `shadow()`, `sunLight()`, `lightLayer()` and the bicubic
  atmosphere inscatter are the expensive parts (author comments hlsl:289-292, 406). Note the
  `lightLayer` result is currently discarded (§9.5) — porting it 1:1 keeps the cost.
- 8-MRT tileFbo (R32F + 3×R11G11B10F + 4×RGBA8) at 256² (terrain.cpp:612-622): the whole bake
  chain assumes these formats are UAV-writable (see F10/F17 in §8) — format changes ripple
  into compute binds.

---

## 5. GPU resources & shader interface

### 5.1 Buffers (all `Buffer::createStructured`; created terrain.cpp:592-662 unless noted)

| Buffer | Stride (struct) | Count | Bind | Written by | Read by |
|---|---|---|---|---|---|
| `drawArgs_tiles` | 16 (`t_DrawArguments`) | 18 | UAV+IndirectArg | tileClear, tileBuildLookup | drawIndirect terrain (offset view*16) |
| `drawArgs_quads` | 16 | 18 | UAV+IndirectArg | tileClear, tileBuildLookup | drawIndirect billboards |
| `dispatchArgs_plants` | 16 (`t_DispatchArguments`, padd!) | 18 | UAV+IndirectArg | tileClear, tileBuildLookup | dispatchIndirect clipLod (offset 0) |
| `plants_Root.drawArgs_vegetation` | 16 | (veg doc) | UAV+IndirectArg | tileClear (`DrawArgs_Plants`) + clipLod | veg render |
| `buffer_tiles` | 48 (`gpuTile`, defines:203-217) | 997 | UAV | splitMerge, generate/ecotopes/passthrough/vertices/delaunay (counters, origin.y), buildLookup (flags) | every render/compute pass; CPU `setBlob` full reset only |
| `buffer_tiles_readback` | 48 | 997 | staging read | copyResource | (debug; not per-frame) |
| `buffer_instance_quads` | 8 (`instance_PLANT`) | 997·32768 (≈255 MB) | UAV | ecotopes, generate, passthrough | render_tile_sprite VS |
| `buffer_instance_plants` | 8 | 997·4096 | UAV | passthrough | clipLodAnimatePlants |
| `buffer_clippedloddedplants` | 32 (`xformed_PLANT`, defines:149-157 — note `uint padd`) | 1M | UAV | clipLod | ribbon/triangle shaders |
| `buffer_lookup_terrain[18]` | 4 (`tileLookupStruct`=uint) | per-view table terrain.h:808 | UAV | buildLookup (via ParameterBlock array) | render_Tiles VS (per-view SRV rebind terrain.cpp:2909) |
| `buffer_lookup_quads[18]` | 4 | terrain.h:811 | UAV | buildLookup | render_tile_sprite VS |
| `buffer_lookup_plants[18]` | 4 | terrain.h:817 | UAV | buildLookup | clipLod |
| `buffer_terrain` (VB) | 8 (`Terrain_vertex`) | 32768·997 | UAV (+counter) | tileDelaunay | render_Tiles VS; counter zeroed CPU-side per child |
| `buffer_tileCenters` | 16 — CPU float4 vs GPU `centerFeedback{min,max,A,B}` (terrainDefines.hlsli:29-34) | 997 | UAV | tileVertices (.min=centerHeight), buildLookup (clears .min) | CPU readback (.x only) |
| `buffer_tileCenter_readback` | 16 | 997 | staging read | copyResource / frame | CPU map |
| `buffer_feedback` | sizeof(`GC_feedback`) (defines:282-354; ~1.9 KB, **layout-fragile**, see F9) | 1 | UAV | tileClear, buildLookup, ecotopes, terrain_under_mouse, clipLod | CPU readback / frame |
| `splines.*`, terrafector `gpuTileTerrafector.vertex/index` | (roads/terrafector doc) | | SRV | CPU upload | splitRenderTopdown draws |

### 5.2 Textures (terrain.cpp:552-638,982)

| Texture | Format / size | Role |
|---|---|---|
| `split.tileFbo` colors 0-7 | R32F(hgt), R11G11B10F(albedo), R11G11B10F(pbr), R11G11B10F(alpha/permanence), 4×RGBA8(ecotopes); 256², D24S8 depth | bake scratch; colors ALSO bound as UAV by bicubic/ecotopes (needs UAV bind flag — F10) |
| `split.bakeFbo` | same desc, 1024² | offline export bake |
| `split.normals_texture` | R11G11B10F 256² UAV | tileNormals out |
| `split.vertex_A/B_texture` | R16Uint 128² SRV+UAV | vertex codes / JFA ping-pong |
| `split.vertex_clear` / `vertex_preload` | R16Uint 128², immutable | zeros / seed lattice (§2.1) |
| `split.noise_u16` | R16Uint 256², mt19937(seed 2) | deterministic placement noise — same seed ⇒ same world |
| `split.debug_texture` | RGBA8 256² UAV | debug output (always-on blit) |
| `split.bc6h_texture` | RGBA32Uint 64² UAV | BC6H block scratch |
| `compressed_Normals_Array` | R11G11B10F 256²×997 | render normal source |
| `compressed_Albedo_Array` | R11G11B10F 256²×997 (BC6H variant commented) | render albedo |
| `compressed_PBR_Array` | **BC6HU16** 256²×997 | copySubresource target of bc6h scratch |
| `height_Array` | R32F 256²×997 | terrain-under-mouse picking only |
| `split.rootElevation` | R32F texSize², 8 mips, generateMips (terrain.cpp:1271-1272) | lod-0 elevation + ecotope `gLowresHgt` (samples explicit mip via `ect[i][1].a`) |
| streamed elevation tiles | R16Unorm 1024² (LRU) | bicubic `gInput` |
| streamed ortho tiles | RGBA8Srgb 1024² (LRU) | bicubic `gInputAlbedo` |

### 5.3 cbuffers (layout + padding — ALL load-bearing)

| Shader / cbuffer | Layout | Upload style |
|---|---|---|
| tileBuildLookup `gConstants` | `uint4 frustumflags[1024]` = **16384 B** | raw `setBlob` (terrain.cpp:1923-1924) |
| tileSplitMerge `gConstants` | `tileForSplit child[4]` — `{uint index,lod,y,x; float3 origin; float scale}` 32 B each (defines:220-229; float3+float packs one 16-B slot) | raw `setBlob` (terrain.cpp:2252-2253) |
| tileEcotopes `gConstants` | `ecotopeGpuConstants` (ecotope.h:149-167): 3×16-B header rows, `float2 padd2` **filled by CPU with hgt_offset/scale but unread by the shader** (terrain.cpp:2411), then `float4 ect[12][5]` + `float4 texScales[12]` | raw `setBlob` (terrain.cpp:2416) |
| tileBicubic `gConstants` | `float2 offset; float2 size; float hgt_offset; float hgt_scale; int isHeight` | per-member |
| tileNormals `gConstants` | `float pixSize; float3 padd` (explicit pad, hlsl:23-27) | per-member |
| tileVertices `gConstants` | `float4 constants` = {pixSize·scale·2.5, 0, 0, tileIndex} — **lod-dependent scale table** terrain.cpp:2458-2464 (1.3 below lod7, 1.2/1.5/2.0/3.2 at 13/14/15/16+) | per-member |
| tileJumpFlood/tileGenerate/tileDelaunay/tilePassthrough `gConstants` | 1–4 scalars | per-member |
| render_Tiles `gConstantBuffer` | `float4x4 view,proj,viewproj; float3 eye; uint tileIndex` | per-member; matrices uploaded pre-transposed (`getTranspose()`, terrain.cpp:2838-2840) |
| render_Tiles `PerFrameCB` | `bool gConstColor; float3 gAmbient; …; float redOffset; float3 padding` (hlsl:31-46) | per-member |
| render_* `LightsCB` | sun/fog params (shared `shaderLightBuffer`) | terrain.cpp:1761-1776 |

**Padding rules distilled:** every struct shared C++↔HLSL here is either scalars-only, or a
`float3` immediately followed by a 4-byte scalar (gpuTile, tileForSplit, triangleVertex,
xformed_PLANT). `GC_feedback` is the one exception — it contains `float4x4`/bare `float3`
runs and WILL mismatch between MSVC and DXC unless explicit pads are added (proven, F9).
`t_DispatchArguments.padd` and the frustumFlags `.z/.w` lanes are structural, not decorative.

---

## 6. Dependencies

**Consumes:**
- Quadtree/streaming (sibling doc): `splitChild` is called with data-ready tiles; bicubic
  input textures come from the elevation/ortho LRU caches; `elevationTileHashmap` provides
  origin/size/offset/scale per source tile (terrain.cpp:2341-2355).
- Terrafector system: pre-combined per-LOD-grid mesh tiles (`terrafectorSystem::loadCombine_*`)
  + material SB/texture array for the topdown bake (terrain.cpp:2585-2792).
- Road network: `splines.bezierData/indexData*` (uploaded in update, terrain.cpp:1831-1850)
  rendered via `shader_splineTerrafector` when `bSplineAsTerrafector`.
- Ecotope system: `getPLantBuffer()/getPLantDesityBuffer()` (`Buffer<uint>`),
  `getConstants()`, 12+12 ecotope textures bound into `gmyTextures.T[]` (terrain.cpp:2388-2403).
- Vegetation (`plants_Root`): `plantData` (plant sizes for passthrough promotion +
  billboard GS extents), `blockData`, `drawArgs_vegetation`, `buffer_feedback` veg twin
  (terrain.cpp:1037-1050); `_plantMaterial::static_materials_veg` materials + textures for
  render_tile_sprite.
- Atmosphere/shadows: `gAtmosphereOutscatter/Inscatter`, `sunLight()`, `shadow()`,
  `terrainShadowTexture`, `LightsCB` values (updateShaderConstants terrain.cpp:1752-1779).
- Host (`Earthworks_4.cpp`): calls `setCamera`, `update`, `onFrameRender` (wiring doc).

**Provides:**
- `buffer_tiles`, `buffer_lookup_plants[view]`, `dispatchArgs_plants`,
  `buffer_instance_plants`, `buffer_clippedloddedplants` → vegetation clip/LOD/render.
- `height_Array` + `buffer_tiles` + `buffer_feedback` → `compute_terrain_under_mouse`
  (camera pan/orbit + road/stamp editing feedback `tum_*`).
- `buffer_tileCenters` readback → CPU quadtree height refinement.
- Debug counters in `GC_feedback` → author debug UI.

---

## 7. Falcor API surface actually used (this subsystem)

| Falcor call | Use here | Diligent mapping note |
|---|---|---|
| `Buffer::createStructured(stride, count[, bindFlags, cpuAccess])` | all SBs; `UnorderedAccess\|IndirectArg` for args; `CpuAccess::Read` staging | stride comes from `sizeof` — struct layout audits required (F9) |
| `Buffer::setBlob(data, offset, size)` | tiles reset, splines upload | |
| `Buffer::map(Read)/unmap` | tileCenters + feedback readback (per-frame, blocking) | Falcor map(Read) = flush+wait semantics |
| `Buffer::getUAVCounter()->setBlob(&zero,0,4)` | VB counter reset (terrain.cpp:2541) | counter likely vestigial but present |
| `Texture::create2D(w,h,fmt,arraySize,mips,data,bind)` | arrays (997 slices!), immutable seeds, UAV scratch | arraySize honored (F11); mips=8 + `generateMips` for rootElevation |
| `Fbo::create2D(w,h,desc,arraySize,mipLevels=8?)` | tileFbo/bakeFbo — 8 MRT, colors later bound as UAV | needs UAV bind on FBO colors (F10) |
| `RenderContext::clearFbo / clearRtv` | per-split clears with specific colours (§1 step 1) | clear VALUES are data (ecotope defaults) |
| `RenderContext::copyResource / copySubresource(dst, dstSlice, src, 0)` | scratch→array-slice publication; staging copies | per-slice layout transitions (Vulkan backlog note in F-docs) |
| `RenderContext::drawIndirect(state, vars, count, argBuf, byteOffset, nullptr, 0)` | terrain + billboards, offset = view*16, **non-indexed** | 16-B `DrawArguments`; count=1 |
| `ComputeContext::dispatchIndirect(argBuf, byteOffset)` | clipLod plants (offset 0) | 16-B stride elements, 12 B consumed |
| `computeShader::dispatch(ctx, w, h)` | all bake dispatches — args are GROUP counts | numthreads 8×8 (most), 16×16 (ecotopes), 256×1 (passthrough), 32×1 (buildLookup), 1×1 (clear/splitMerge) |
| `Vars()->setBuffer/setTexture/setSampler` by NAME | all binding | reflection-driven |
| `Vars()["cb"]["member"] = v` | per-member cbuffer sets | |
| `Vars()->getParameterBlock("gConstants")->setBlob(...)` | frustumFlags 16 KB, splitMerge children, ecotope constants | THE F7 trap: raw blob upload path must actually reach the GPU |
| `ParameterBlock` with `RWStructuredBuffer` **arrays** (`viewRenderData.terrainLookup[18]` etc.) + `findMember(...)[i] = buffer` | buildLookup output binding (terrain.cpp:903-912; hlsl:26-32) | no native Diligent equivalent — port flattened to 54 named buffers + Store helpers (§8) |
| `GraphicsProgram` with GS entry, `setShaderModel("6_5")` | render_tile_sprite (PointList+GS) | pixelShader.cpp:9-14 |
| `GraphicsState::setFbo/setViewport/setRasterizerState/setBlendState/setDepthStencilState` | render + topdown bake states | |
| `drawInstanced / drawIndexedInstanced` | topdown terrafector (128·3 verts) / splines (64·6 idx, shared quad-pattern IB) | drawInstanced must NEVER take the indexed path (F22) |
| `Texture::captureToFile`, `readTextureSubresource`, `resourceBarrier(CopySource)`, `flush(true)` | offline bake export only (terrain.cpp:3401-3443) | |
| `blit(SRV, RTV, srcRect, dstRect, filter)` | debug tile views (terrain.cpp:3124-3128) | |
| `FALCOR_PROFILE(name)` | scoped GPU/CPU zones | keep equivalent markers for perf calibration |

---

## 8. Port drift notes (EarthworksFX vs this extract)

- `EarthworksFX/src/core/terrain.cpp` is ~5681 lines vs extract 5207 — extra lines are port
  notes, buildings delegation (rappersville block → `buildingsRenderer`, port terrain.cpp:834,
  2357, 3586) and formatting; the pipeline structure is the same June-2026 code.
- **Lookup-array flattening**: the port could not express `ParameterBlock` arrays of
  RWStructuredBuffer; it created `viewRenderData_lookupBuffers.hlsli` (port-side invention,
  extract3_inventory surprise 2) with `StoreTerrainLookup/StoreQuadLookup/StorePlantLookup(view,
  slot, value)` switch-helpers over 54 individually-named buffers
  (diff vs extract confirmed: only lines 26 and the three Store sites differ). A fresh re-port
  to native Diligent can instead bind 3×18 buffers via arrays-of-descriptors or merge each
  triple into one large buffer + per-view offsets — but whatever is chosen, buildLookup writes
  and the three consumers (render_Tiles, render_tile_sprite, clipLod) must use the SAME
  addressing.
- **BRINGUP_NOTES F-findings that encode real semantics for THIS subsystem** (mine, don't
  re-derive; all in `EarthworksFX/BRINGUP_NOTES.md`):
  - **F7**: the 16 KB `frustumFlags` cbuffer blob is the single point of failure — if raw
    setBlob-style uploads don't reach the GPU, instanceCount stays 0 and terrain is invisible
    while all CPU metrics look healthy.
  - **F9**: `GC_feedback` MSVC-vs-DXC layout mismatch (float4x4/float3 alignment) shifted
    every readback field; fixed with explicit pads + static_asserts on SPIR-V offsets.
    Rule: layout-audit every shared struct containing float3/float4x4.
  - **F10**: FBO colour targets need UAV bind or all bicubic/ecotope writes silently vanish
    (flat terrain at the clear value 0.3).
  - **F11**: `create2D` arraySize must produce a real Tex2DArray (997 slices) or every tile
    samples the same image.
  - **F22**: `drawInstanced` is ALWAYS non-indexed in Falcor, even though every pixelShader
    VAO carries the shared quad-pattern IB — taking the indexed path scrambles the topdown
    terrafector bake (128·3 draws) and the skydome.
  - **F6** (verified-OK list): indirect draws = non-indexed DrawIndirect, 16-B stride, offset
    view*16 with view=1; hardcoded shader viewMask incl. main_CENTER; root tile draws nothing
    until the first split. Matches this extract exactly.
- **Known port bug (task.md §3): "tiles sent to y=0 where terrafectors are placed; broken on
  Vulkan."** What the ORIGINAL does at every point that could produce y=0:
  1. `tileCenters` slots are cleared to 0 by buildLookup only for `flags==0` tiles
     (hlsl:158-161); CPU applies readback only when `.x > 0` (terrain.cpp:1908), so a zeroed
     slot never overwrites a good height.
  2. `origin.y` is (re)derived from the CENTER texel of the baked elevation
     (`gHgt[128,128]` / SampleLevel(0.5,0.5)) in tileVertices/ecotopes/passthrough — AFTER
     terrafectors have rewritten color0 in splitRenderTopdown. If a port orders these
     differently, or the FBO-UAV binding fails per-plane (F10/F17 territory, worse on
     Vulkan), the centre sample reads 0 → `origin.y = −2·size` and packed heights collapse.
  3. The CPU fallback chain (`quadtree_tile::set`: inherit parent boundingSphere.y,
     terrain.cpp:345-354) means the original NEVER has a y=0 tile unless the root itself
     failed. Any port reproduction should check, in order: tileCenters readback validity,
     per-plane UAV writes on Vulkan, and blob upload of splitMerge's `child[4]`.
- Shaders: the port's DXC conversions are quality-suspect by decision (task.md §3 — shaders
  are re-ported fresh from this extract; use `EarthworksFX/hlsl/` only as a DXC-issue
  reference, e.g. explicit register annotations and the flattened lookup include).

---

## 9. Bad code / removable candidates (OPINION — do not act during port)

1. `compute_tileGenerate` is **dead**: loaded and fully bound (terrain.cpp:876-884) but its
   only dispatch is inside a commented block (terrain.cpp:2482-2500). Quad spawning happens
   in compute_tileEcotopes instead. (`gEct5` in the shader is also unbound.) Candidate: drop
   shader + bindings; keep the file until vegetation doc confirms nothing else revives it.
2. `terrainManager::compute_bakeFloodfill` member (terrain.cpp:524,712) is loaded but never
   dispatched from terrain — the live copy lives in vegetationBuilder.cpp:2552/3893
   (billboard bake gutter fill). The terrain-side member is removable.
3. Commented-out corpses: `drawArgs_plants`/`drawArgs_clippedloddedplants` (terrain.cpp:641-642
   etc.), `compute_tileElevationMipmap` (terrain.cpp:968-980), the alternate CubicHermite
   bicubic (compute_tileBicubic.hlsl:125-171 — note its comment: double precision fixes the
   quality issue but is 10× slower), `unpackFrustum` (compute_tileBuildLookup.hlsl:41-55),
   fisheye/paraboloid experiments in render_Tiles VS (hlsl:97-128).
4. `rappersville*` members (terrain.h:530-535) are unreferenced (STRIP-REVIEW tagged); the
   port's `buildings.*` replacement is the developer-verified keeper.
5. render_Tiles.hlsl psMain is three stacked lighting experiments: the `lightLayer` result
   (hlsl:292-299) is overwritten at hlsl:312, and the first "custom" block is largely
   overwritten by the "TRY 2" block (hlsl:331-373) which produces the final colour. The dead
   math still costs real shader time (`lightLayer` is called and discarded). A re-port could
   deliver identical pixels with the TRY 2 block + shadow/sun/atmosphere only — but verify
   pixel-exactness before cutting anything (this file is exactly where "improvements" lost
   quality before).
6. `feedback[0].plants_culled = slot` writes in ecotopes/generate (compute_tileEcotopes.hlsl:248)
   abuse an unrelated debug field as a "last slot" probe — harmless, confusing.
7. Hardcoded literals that deserve defines: `1000` thread guard, `(numQuadsPerTile)>>8`
   dispatch count, block size 64 (`>>6`), `>>5` in the buildLookup dispatch, `14000` spawn
   threshold, `FACTOR > 15`, jumpflood `step=4`.
8. `uint step;` cbuffer in compute_bakeFloodfill (hlsl:15-18) unused; large commented tail.

---

## 10. Open questions / uncertainties (flag, don't guess)

1. **Zero-init reliance**: `startVertexLocation/startInstanceLocation` of every
   `t_DrawArguments` element are never written by any shader or CPU code — correctness
   depends on `createStructured` delivering zeroed memory. Falcor appears to guarantee this;
   Diligent does NOT for default-heap buffers. A port must clear them once explicitly (or
   have tileClear write all 4 fields).
2. **packPlants view semantics** (§3): is the missing `(1<<view)` test a bug the author
   compensated for (dispatchIndirect at offset 0), or intentional "plants are view-shared"?
   Either way the pair must be ported as a unit. Developer confirmation would help before
   any cleanup.
3. **Shader-hardcoded viewMask** vs the CPU `viewMask` member (terrain.h:822) — the CPU value
   is recomputed in setCamera but never uploaded; presumably vestigial. If cascades are to
   actually render in the port (shadows are a §1 requirement), the mask, per-view lookup
   rebinds, and `renderIndirect(..., view, 1)` calls for casc0-3 all need wiring that does
   not exist in this extract — where did cascade rendering happen in the original build?
   (`cascadeShadowMaps.*` exists but does not consume `drawArgs_tiles`; atmosphere/shadows
   doc reports the shipped shadows are the CPU `_shadowEdges` 4096² solve.)
4. **`tiles[].origin.y` triple-write**: ecotopes and passthrough recompute the corner origin
   locally but only tileVertices persists it to `tiles[]` — is the transient inconsistency
   (quads packed against a slightly different OH than the persisted one) intentional slack or
   a latent bug? Heights differ only if the centre texel changes between chain steps
   (terrafectors already baked by then — likely fine, but unverified).
5. **VB overflow**: Delaunay has no clamp at `numVertPerTile/3` triangles; is 32768 slots
   empirically safe for lod≥16 tiles with the 3.2 vertex-scale factor, or does the scale
   table exist precisely to keep counts under the cap? (Looks like the latter — the lod→scale
   table raises the flatness cutoff on deep tiles — but no assert exists.)
6. **`buffer_terrain` UAV counter**: reset every split but never incremented by current
   shaders — confirm no other pass uses `IncrementCounter` on VB before dropping
   counter-capability in the port.
7. `terrainShader` binds `gSmpLinearClamp` (terrain.cpp:679) but render_Tiles also expects
   samplers from atmosphere includes (`gSmpAniso` set at :672); the BRINGUP backlog mentions a
   never-set `gSmpLinear` validation error — reconcile the exact sampler set when re-porting
   the shader.
8. `lookupSizeBillboard[16] = 8000` (terrain.h:812) — not a power of two like every other
   entry; typo for 8192 or deliberate? Harmless either way (it's just a buffer size).
