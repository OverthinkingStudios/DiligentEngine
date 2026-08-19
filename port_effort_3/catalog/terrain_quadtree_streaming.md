# Concept Catalog — Terrain quadtree + tile streaming (CPU side)

Sources analyzed: `port_effort_3/source_extract_3/` — `earthworks_scene/terrain.{h,cpp}`,
`earthworks_scene/lru_cache.h`, `hlsl/terrain/compute_tileSplitMerge.hlsl`,
`hlsl/terrain/terrainDefines.hlsli`, `hlsl/terrain/groundcover_defines.hlsli` (shared structs),
plus the interface edges of `compute_tileBicubic.hlsl`, `compute_tileBuildLookup.hlsl`,
`compute_terrain_under_mouse.hlsl` and the host calls in `Earthworks_4.cpp`.
All file:line refs are into `port_effort_3/source_extract_3/` unless prefixed.
The tile-BAKE compute chain (bicubic→topdown→ecotopes→passthrough→normals→vertices→jumpflood→
delaunay→bc6h) and indirect-draw generation are the **gpu_tile_pipeline** doc's territory; here
they appear only as the interface `splitChild()` drives.

---

## 1. Purpose & data flow

`terrainManager` (terrain.h:388) owns the world: a CPU quadtree of streamed terrain tiles whose
per-tile textures/vertices are generated **on the GPU at split time** and then rendered fully
GPU-driven (indirect draws fed by a per-frame lookup compute). CPU never touches per-frame
geometry — it only decides *which* tiles exist.

### Per-frame (host: Earthworks_4.cpp:234–238, 269)

1. `updateShaderConstants()` (terrain.cpp:1752) — pushes sun/fog/screen constants + shadow/
   previous-frame textures into the terrain/sprite shaders.
2. `setCamera(CameraType_Main_Center, view, proj, pos, true, 1920)` (terrain.cpp:1688) — stores
   per-view matrices and builds the frustum-plane matrix (see §2.3). Note: only ONE camera is set
   by the sample host; the 18-view machinery (CameraType enum terrain.h:242–262) is mostly latent.
3. `update(pRenderContext)` (terrain.cpp:1783–1946):
   - early-out for non-terrain modes (vegetation/glider/terrainBuilder/textureTool) 1787–1794;
   - bake stepper + ecotope-change full reset 1809–1820;
   - road/spline GPU upload when dirty 1826–1851;
   - **split/merge decision** 1853–1885: reset flags → `testForSplit` per used tile → mark
     children of no-longer-splitting tiles for remove → splice removed tiles back to `m_free`;
   - `splitOne()` 1888 — at most ONE split executes per frame (§2.5);
   - **readback** of `buffer_tileCenter_readback` (min height per tile) → patches
     `tile->origin.y` / `boundingSphere.y` (1901–1913);
   - `compute_tileClear` dispatch (zero draw args/feedback counters) 1916;
   - `calculateSurfaceFlags()` (CPU frustum flags for all 18 views) 1919;
   - upload `frustumFlags[1024]` (16 KB) as raw cbuffer blob + dispatch
     `compute_tileBuildLookup` over all tiles (1923–1928).
4. `onFrameRender()` (terrain.cpp:2823) — clip/LOD plants compute, indirect terrain draw,
   indirect billboard draw, plants, skydome, splines/sprites overlays, then
   `compute_TerrainUnderMouse` + **full GC_feedback readback** (3074–3114) which feeds the mouse
   picking state (`mouse.hit`, pan/orbit pivots) and all debug metrics.

### Async (split-time streaming)

Tile data comes from JPEG2000 (OpenJPH) decoded on worker threads via `std::async`; a split is
**deferred** until both elevation and orthophoto textures for that quadtree cell are resident in
the LRU texture caches (§2.6). Split then runs the whole GPU bake chain for the 4 children in one
frame.

---

## 2. Core tricks & clever mechanisms (a rewriter MUST NOT lose these)

### 2.1 Fixed tile pool, no allocation, GPU state addressed by pool index
- `numTiles = 997` `quadtree_tile`s allocated once (terrain.h:477, terrain.cpp:1162–1179), managed
  by two `std::list<quadtree_tile*>` (`m_free`/`m_used`). A tile's `index` IS its slice in every
  GPU array: `height_Array`/`compressed_{Albedo,Normals,PBR}_Array` are 997-slice texture arrays
  (terrain.cpp:634–638), `buffer_tiles` is `gpuTile[997]`, `buffer_terrain` vertex storage is
  `numVertPerTile(32768) * 997` (terrain.cpp:662), instance buffers `numTiles*numQuadsPerTile`
  etc. (terrain.cpp:649–653). WHY: zero per-frame allocation, and the renderer needs only a tile
  index to find everything.
- Merging is trivial: children go back on the free list (terrain.cpp:1874–1884); their GPU data
  simply becomes stale and unreferenced (flags cleared per-frame). Nothing is freed.

### 2.2 Split decision = screen-space pixel size with two thresholds
`testForSplit` (terrain.cpp:1615–1660): for each active camera,
`lod_Pix = size / viewDistance * (resolution * length(proj[0]) / 4)`.
Split if `lod_Pix > 150 && inFrustum` OR `lod_Pix > 300` (even outside the frustum — keeps the
area behind you warm so turning the camera doesn't hit a wall of un-split tiles;
terrain.cpp:1641–1648). `mainMaxLod 15` cap (terrain.cpp:1614). Merge = parent no longer wants to
split → all descendants removed recursively (`markChildrenForRemove`, terrain.cpp:1667–1676).
WHY the two constants matter: they are tuned against the amortized 1-split-per-frame budget;
lowering 150 or raising the /4 changes streaming pressure and popping behavior.

### 2.3 Frustum test as one mat4 multiply (CPU mirrors GPU)
`setCamera` builds 4 side planes **from the proj matrix only** (terrain.cpp:1702–1726) and packs
them as rows of `frustumMatrix`. Test (terrain.cpp:1590–1593, 1629–1631):
`viewBS = view * boundingSphere` (view-space center), then
`test = saturate(viewBS * frustumMatrix + float4(R,R,R,R)); inside = all(test)` with a CPU
`all()` mimicking HLSL (terrain.cpp:1570). Radius is deliberately generous: `1.0*size` in
`calculateSurfaceFlags` ("missing here is FATAL", terrain.cpp:1588), `0.9*size` in testForSplit.
Near/far planes are NOT tested (terrain has no far clip concerns; near misses are covered by the
radius slack). NOTE the matrix conventions here are subtle (glm, transposed-proj plane
extraction, `vec*mat` row-vector product) — BRINGUP F6 confirms the semantics survive a memcpy
into glm; copy this code verbatim rather than "fixing" it.

### 2.4 "Surface" flag: render exactly the leaf cut of the tree
A tile is renderable surface iff `tile->parent->main_ShouldSplit && tile->child[0]==nullptr`
(terrain.cpp:1597). `calculateSurfaceFlags` (terrain.cpp:1581–1607) packs per-tile
`uint4{ surfaceBits, visibleBits, -, - }`: `.x` bit31 = active, bits 0..17 = surface-visible per
view; `.y` = in-frustum per view (plants use this). This 16 KB array is the ONLY per-frame
CPU→GPU tile traffic (one cbuffer blob, terrain.cpp:1923–1924). Consequences a rewriter must
keep: (a) the root tile draws nothing until the first split (see compute_tileBuildLookup.hlsl:152
`t>0`, and BRINGUP F6); (b) children become drawable only when the parent still *wants* to split,
which guarantees parent/child never draw simultaneously (no cracks/double-draw).

### 2.5 Amortization: exactly one split per frame, and only when data is ready
`splitOne` (terrain.cpp:2202–2314): returns if `m_free.size() < 8` (keeps slack so merges can
always run); walks `m_used` in list order and takes the FIRST `forSplit` tile whose elevation AND
image textures are cached (`dataReady`); performs the full 4-child split + GPU bake; returns.
Tiles that aren't ready just kick off async decodes and are skipped this frame. Author comment
"FIXME PICK A BETTER ONE HERE" (terrain.cpp:2208) — list order ≈ age, not priority; do not
"improve" to multiple splits per frame without re-validating frame-time spikes (a split runs
~10 compute dispatches + a topdown raster for each of 4 children).

### 2.6 Two-stage async JP2 decode with a 0/1/2 state machine
Elevation (`hashAndCache`, terrain.cpp:1997–2040 + thread 1954–1993) and images
(`hashAndCacheImages`, 2127–2182 + thread 2044–2123) each use:
`hashCount==0` idle → launch `std::async` decode into a reusable staging vector
(`jphData`/`jphImageData`) and set 1 → thread finishes, sets `hashCount=2` + `cacheHash` →
**next frame on the main thread** the texture is created from staged data and inserted into the
LRU (`elevationCache`/`imageCache`, 45 entries each, terrain.cpp:996/1000). WHY: texture creation
stays on the render thread (Falcor requirement), decode (~ms) never blocks a frame, and only ONE
decode per stream is in flight. The split simply keeps failing `dataReady` until both caches hit.
- Elevation JP2: one file per tile, opened by filename from `elevationTileHashmap`
  (elevations.txt, parsed terrain.cpp:1227–1289), decoded to 1024² int16 → `R16Unorm` texture;
  dequantized in the bicubic shader by per-tile `hgt_offset/hgt_scale`.
- Orthophotos: **packed multi-tile archives** (`jp2Dir`/`jp2File`/`jp2Map`, terrain.h:291–382,
  from orthophotos.json). File 0 (coarse LODs) is fully preloaded to RAM (`cache0`,
  terrain.cpp:1477); other whole files are read into a 40-entry byte-blob LRU (`jp2Dir::cache`,
  terrain.cpp:1472, 1509–1536) and individual tiles decode **from memory**
  (`mem_infile.open(data + mapTile.fileOffset, sizeInBytes)`, terrain.cpp:2072–2078) to 1024²
  RGB → `RGBA8UnormSrgb`.

### 2.7 Tile-coordinate hash: `(lod<<28) | (y<<14) | x`
`getHashFromTileCoords` (terrain.cpp:1223–1225) — the universal key for elevation map, image
tileHash, caches, and bake lists. 14 bits for y/x pairs with `mainMaxLod 15`: consistent. Hash 0
is reserved/special: "elevationHash==0" means "use rootElevation" (terrain.cpp:2347–2350) and
`jp2Dir::cacheHash(0)` early-outs because file 0 is preloaded (terrain.cpp:1512).

### 2.8 Children inherit parent's stream hashes
`setChild` (terrain.cpp:2186–2199) copies `elevationHash`/`imageHash` from the parent; the child
then upgrades to its own hash only if the hashmap has an entry at its exact lod/y/x
(terrain.cpp:2009–2013, 2151–2154). WHY: sparse datasets — where high-res data doesn't exist the
child keeps sampling the ancestor's 1024² texture through the bicubic window math (offset/size in
the parent's UV space, terrain.cpp:2343–2345). This is the whole "LOD fallback" mechanism; there
is no other.

### 2.9 The 256/248 border convention
Every tile texture is 256² but represents 248² inner pixels + 4-pixel border on each side
(`tile_numPixels 256`, `tile_InnerPixels 248`, `tile_toBorder 256/248`,
terrainDefines.hlsli:6–9). CPU side: `outerSize = size*256/248`, `pixelSize = outerSize/256`
(terrain.cpp:2320–2321), topdown camera ortho half-size `s *= 256/248` (terrain.cpp:2568), jp2Map
origins are pre-shifted by the border (`jp2Map::set`, terrain.cpp:1361–1375), and the bicubic
shader offsets by `(crd - 4.0)` (compute_tileBicubic.hlsl:62). WHY: normals, jumpflood and
ecotope filters need neighbor data without cross-tile reads; render geometry only uses the inner
248. Any resolution change must keep the 4-px border and every one of these ratios in sync.

### 2.10 Tile min-height feedback loop (GPU→CPU)
GPU `compute_tileVertices` writes per-tile min height into `buffer_tileCenters`
(`centerFeedback{min,max,A,B}`, terrainDefines.hlsli:29); `update()` reads it back every frame
and overwrites `tile->origin.y` and `boundingSphere.y` (terrain.cpp:1901–1913, guarded by
`.x > 0`). Until real data arrives, `quadtree_tile::set` estimates: root `boundingSphere.y=0`,
child `origin.y = parent->boundingSphere.y - size*2`, `boundingSphere.y = parent->boundingSphere.y`
(terrain.cpp:345–354). WHY it matters: frustum culling and `lod_Pix` distance both use
`boundingSphere` — without the readback, culling of mountainous tiles is wrong. (Author comment
at 1910 says ".x contains the middle … I also think its unused" — it IS used, right there.
compute_tileBuildLookup also clears `tileCenters[t].min = 0` for inactive tiles,
compute_tileBuildLookup.hlsl:158–161, so stale pool slots don't inject heights.)

### 2.11 Split GPU handoff: `compute_tileSplitMerge`
One 1-thread dispatch per split writes the 4 children's `gpuTile` records (lod/Y/X/origin/
`scale_1024 = size/1024`, counters zeroed, flags 0) from a `tileForSplit child[4]` cbuffer blob
(compute_tileSplitMerge.hlsl:18–35, CPU terrain.cpp:2236–2254). `scale_1024` is the
world-units-per-heightfield-texel-ish scale the whole GPU side multiplies 10-bit local positions
by (groundcover_defines.hlsli:211). This is the ONLY place gpuTile geometry fields are written —
lose it and every downstream pass reads garbage (BRINGUP F7 proved exactly this failure mode).

### 2.12 Terrain-under-mouse picking via feedback buffer (not GUI-only — keep)
`compute_TerrainUnderMouse` (dispatched terrain.cpp:3076–3081) ray-marches the height texture
array on the GPU, warm-starting from `tum_lastKnownTile` (`tum_idx`,
compute_terrain_under_mouse.hlsl:109–112), writes `tum_Position/tum_Normal/tum_idx` +
`colourUnderMouse`/`heightUnderCamera` into `GC_feedback`. CPU reads the struct back the same
frame (terrain.cpp:3083–3087) and derives camera pan/orbit/zoom pivots (`mouse.*`,
terrain.cpp:3089–3113, used in onMouseEvent 4556–4624) and road/stamp editing anchors. The
`// STRIP-REVIEW` tag at terrain.h:563 records the keep decision. The ImGui dependency here is
only `ImGui::IsMouseDown(n)` for pan/orbit gating (terrain.cpp:3092–3111, 4491–4573).

### 2.13 Full-reset gating: `fullResetDoNotRender`
`reset(true)` (terrain.cpp:1181–1221) rebuilds the quadtree from the root and sets
`fullResetDoNotRender`; the host skips rendering entirely until `update()` sees no dirty splits
(terrain.cpp:1872, Earthworks_4.cpp:257). WHY: after ecotope/terrafector changes every tile is
stale; rendering during the rebuild storm would flash garbage.

---

## 3. Invariants & conventions

| Invariant | Where |
|---|---|
| World is 40000 m square, origin-centered: root tile origin `(-size/2, 0, -size/2)`, X/Z ∈ [-20000, 20000] | terrain.cpp:1195, jp2Map::set default `wOffset=-20000` terrain.h:292 |
| Y-up; tile origin = min-corner (x,z); `origin.y` = tile min height (via readback) | §2.10 |
| Tile at lod L has size `40000/2^L`; child (x*2+dx, y*2+dy) | terrain.cpp:2186–2199 |
| `boundingSphere.xz` = tile center, `.y` = readback height, `.w = 1` | terrain.cpp:328–354 |
| Tile textures 256² with 4-px border = 248 inner px; ALL ratio math derives from this | §2.9 |
| Hash key `(lod<<28)|(y<<14)|x`; hash 0 = root/preloaded | §2.7 |
| `scale_1024 = tileSize/1024` is the GPU's only scale; heights are R16Unorm × hgt_scale + hgt_offset | compute_tileSplitMerge.hlsl:25, compute_tileBicubic.hlsl:92 |
| gpuTile ownership: lod/Y/X/origin/scale written ONLY at split; `flags` rewritten EVERY frame by tileBuildLookup from the CPU cbuffer; counters written by bake computes | groundcover_defines.hlsli:203–217 comments |
| Root tile (index 0) is never rendered; GPU loops are hardcoded `t>0 && t<1000` / `t<1001` | compute_tileBuildLookup.hlsl:152, compute_terrain_under_mouse.hlsl:109/118 |
| Only ONE camera drives everything in the sample host (`resolution=1920` hardcoded) | Earthworks_4.cpp:236 |
| `viewMask` (CPU, terrain.h:822) and the shader's own hardcoded `viewMask` (compute_tileBuildLookup.hlsl:149) must agree — the shader does NOT read the CPU one | |
| `m_free` never drops below 8 (split refuses) — merge always possible | terrain.cpp:2205 |
| lastFile.xml (cwd) is REQUIRED; missing file = shutdown. Saved in the destructor with current mode/road/stamps. Cereal-versioned (104) | terrain.cpp:391–414, 377–387, terrain.h:103–161 |
| `.terrain` file = `_terrainSettings` JSON (name/projection/size/dirs, version 100); dirRoot/dirResource/dirGis get prefixed from lastFile dirs or cwd | terrain.cpp:416–520, terrain.h:164–189 |

Camera/matrix conventions (RH camera, CCW faces, depth [0,1], film-back FOV, row/column
transposition dance in `splitRenderTopdown` terrain.cpp:2562–2581) are catalogued in
conventions_app_wiring.md; the frustum-plane extraction of §2.3 depends on them.

---

## 4. Performance-critical details

1. **One split per frame + data-ready gating** (§2.5/2.6) is THE frame-pacing mechanism. Naively
   splitting all pending tiles, or blocking on decode futures, reintroduces multi-ms hitches.
2. **Per-frame CPU cost is intentionally tiny**: flag reset + testForSplit over `m_used`
   (~100–200 tiles), one 16 KB cbuffer upload, two buffer readbacks, a handful of dispatches.
   The author's "NOT GREAT to redo every frame but also likely really fast" (terrain.cpp:1580)
   is measured truth — don't add per-tile allocations or maps.
3. **Readbacks**: `buffer_tileCenter_readback` map (terrain.cpp:1902–1905) and
   `buffer_feedback_read` map (terrain.cpp:3083–3087) are `copyResource` + `map(Read)` each
   frame. Under Falcor this synchronizes with the copy (internally fenced); the original's fps
   INCLUDES this cost. In Diligent, a naive `MapBufferRead` with an idle-GPU wait would stall the
   whole pipeline — port with a 1–2 frame latency ring (values are camera-feedback; a frame of
   lag is invisible) but do NOT drop them (culling heights §2.10 and picking §2.12 die).
4. **LRU sizes are load-bearing**: elevation/image texture caches 45 (terrain.cpp:996/1000),
   jp2 file-blob cache 40 (terrain.cpp:1472). The author documents crashes from undersizing —
   "the weird crash comes from my cache being too small, so tiles get deleted then needed at
   another resolution" (terrain.cpp:2125) and the in-code log "FIX imageCache.resize(55); its
   still too small" (terrain.cpp:2082–2086) which fires when a tile's *file blob* was evicted
   between `cacheHash` and the decode thread. Sizes scale with visible-tile count (FOV-dependent,
   see the 2085 comment). A rewrite should keep ≥ these sizes and make the miss path fail soft.
5. **`imageDirectory.cacheHash(hash)` runs synchronous whole-file IO on the main thread**
   (terrain.cpp:2146–2149 wraps it in a timer; >1 ms is recorded in `stream.imageCacheIOTime`).
   This is a known hitch source the author tolerated (OS file cache usually absorbs it). Fair
   game to move onto the decode thread — but keep the state machine's single-flight property.
6. **Reusable decode staging buffers** `jphData` (2 MB u16) / `jphImageData` (4 MB) are grown
   once and reused (terrain.cpp:1958, 2054) — no per-decode allocation.
7. **Texture formats** were chosen deliberately (terrain.cpp:634–638): PBR array is BC6H
   (compute-compressed at split), albedo array is R11G11B10F with the BC6H path present but
   commented out (terrain.cpp:2443–2447), normals R11G11B10F, height R32Float. Changing formats
   changes both memory (997 slices!) and the bake chain.
8. **Generous culling radii** (1.0×/0.9× tile size, §2.3) are cheap insurance; shrinking them
   for "efficiency" produces missing tiles at screen edges — the author marked it FATAL.
9. **`splitChild` dispatch order** interleaves copies between dependent dispatches ("Do this
   early to avoid stalls", terrain.cpp:2434–2438) — the copy of vertex_clear/vertex_preload
   happens while earlier compute output is still in flight. Preserve the ordering intent.
10. The out-of-frustum split threshold (300 px, §2.2) trades idle-time streaming for
    turn-the-camera latency. Removing it looks free until the first fast 180° pan.

---

## 5. GPU resources & shader interface (this subsystem's own)

**Structs shared C++↔HLSL — layout is load-bearing; see BRINGUP F9 before touching.**

| Struct | Layout | Notes |
|---|---|---|
| `gpuTile` (groundcover_defines.hlsli:203) | uint lod,Y,X,flags; float3 origin; float scale_1024; uint numQuads,numPlants,numTriangles,numVerticis — 44 B, no padding needed (float3 followed by float) | element of `buffer_tiles[997]` |
| `tileForSplit` (:220) | uint index,lod,y,x; float3 origin; float scale — 32 B | ×4 in `gConstants` of compute_tileSplitMerge; uploaded as one raw blob (terrain.cpp:2253). In a cbuffer DXC gives each array element 16-byte-aligned members — this struct is already 16-aligned per row; keep field order exactly |
| `GC_feedback` (:282) | ~1.9 KB mixed scalars/float3/float4x4/arrays[18]/[20] | THE structure that broke the previous port: DXC aligns float3/float4x4 to 16, glm doesn't → the port added explicit `padd_align_*` members + static_asserts (F9). The extract version has NO explicit pads — the re-port MUST re-do the layout audit, not copy the port's pads blindly (arrays changed between versions) |
| `t_DrawArguments` (:42) | 4×uint = 16 B, non-indexed | all terrain indirect draws are non-indexed 16-byte args → Diligent `DrawIndirect`, offset `view*16` |
| `t_DispatchArguments` (:50) | 3×uint + explicit `padd` | the padd is real and uploaded — do not delete (this class of "unused padding" deletion broke rendering before) |
| `centerFeedback` (terrainDefines.hlsli:29) | 4×float | CPU reads it as `float4` and uses `.x` (terrain.cpp:1903–1911) |
| `Terrain_vertex` (terrainDefines.hlsli:22) | uint idx; float hgt | `buffer_terrain` element |

**Buffers owned here** (created terrain.cpp:592–662):

| Buffer | Size / flags | Producer → Consumer |
|---|---|---|
| `buffer_tiles` | gpuTile × 997, structured UAV | splitMerge/bake computes + tileBuildLookup → every render/compute pass |
| `buffer_tiles_readback` | CpuAccess::Read twin | declared; readback path currently unused in extract |
| `buffer_tileCenters` | float4 × 997 | compute_tileVertices → CPU (culling heights) + tileBuildLookup (clear) |
| `buffer_tileCenter_readback` | CpuAccess::Read, BindFlags::None | staging for the above |
| `buffer_feedback` / `buffer_feedback_read` | GC_feedback × 1 (+Read twin) | all computes → CPU picking/metrics |
| `drawArgs_tiles`, `drawArgs_quads` | t_DrawArguments × 18, UAV+IndirectArg | tileClear zeroes, tileBuildLookup accumulates → renderIndirect |
| `dispatchArgs_plants` | t_DispatchArguments × 18, UAV+IndirectArg | → dispatchIndirect of clipLodAnimatePlants (terrain.cpp:2896) |
| `buffer_lookup_terrain/quads/plants[18]` | uint × per-view sizes, terrain.h:808–818 | tileBuildLookup → VS tile lookup. Sizes are per-view tuned (main=524288 ≈ 33 M tris; comment "Zero is not allowed") |
| `buffer_terrain` | Terrain_vertex × 32768×997 | delaunay writes via **UAV counter** (reset each split by `getUAVCounter()->setBlob`, terrain.cpp:2541 — "I misuse increment, count in 1's but write in 3's"). Diligent has no implicit counter: port needs an explicit counter buffer bound to the delaunay pass |
| `buffer_instance_quads/plants`, `buffer_clippedloddedplants` | instance_PLANT / xformed_PLANT | vegetation side; owned here, populated by bake chain |
| splines.* buffers | cubicDouble/bezierLayer | road/terrafector side (see roads doc); fed from here at terrain.cpp:1826–1851, 5080+ |

**Textures owned here**: `height_Array` R32F×997, `compressed_Albedo_Array` R11G11B10F×997,
`compressed_Normals_Array` R11G11B10F×997, `compressed_PBR_Array` BC6HU16×997 (terrain.cpp:634–638);
`split.tileFbo` 256² 8-MRT (R32F hgt, 3×R11G11B10F albedo/pbr/alpha, 4×RGBA8 ecotope, D24S8,
mipLevels=8; terrain.cpp:612–622) — **FBO color targets must be UAV-bindable** (computes write
them directly; BRINGUP F10); `bakeFbo` 1024² same desc; `rootElevation` R32F w/ mips from raw
lod-0 file (terrain.cpp:1271); `noise_u16` 256² R16Uint seeded mt19937(2) — deterministic, don't
reseed (terrain.cpp:601–609); `vertex_A/B_texture` 128² R16Uint ping-pong; `vertex_clear` (zeros)
and `vertex_preload` — CPU-generated seed pattern for the vertex selection: border columns/rows at
1/127 every 2, ring at 5/125 every 4, interior grid every 8 from 9 (terrain.cpp:560–589). This
pattern is the guaranteed-minimum vertex set of every tile mesh — hyper-tuned, copy bit-exact.
LRU-cached streaming textures: elevation R16Unorm 1024², images RGBA8UnormSrgb 1024².

**cbuffer feeds from this subsystem**: `compute_tileSplitMerge.gConstants` = raw 4×tileForSplit
blob; `compute_tileBuildLookup.gConstants` = raw `uint4 frustumflags[1024]` blob (16 KB);
`compute_tileBicubic.gConstants` = per-member offset/size/hgt_offset/hgt_scale/isHeight;
`compute_TerrainUnderMouse.gConstants` = mousePos/mouseDir/mouseCoords. The raw-blob pattern is
pervasive — the port path must support "upload N bytes into named cbuffer" (F7's failure).

---

## 6. Dependencies

**Consumes:**
- `terrafectorSystem::loadCombine_LOD{2,4,6,7}[_*]` static mesh caches + material array — the
  topdown bake draws them per tile (terrain.cpp:2617–2792); `terrafectorEditorMaterial::static_materials`.
- `roadNetwork` static/dynamic bezier + index vectors (terrain.cpp:1826–1851, 3948–3990) and
  `bezierRoadstoLOD` spatial re-bucketing into per-LOD-cell index lists (terrain.cpp:4996–5197).
- `ecotopeSystem` (`mEcosystem`): plant/density buffers + `ecotopeGpuConstants` for the ecotope
  bake pass (terrain.cpp:2384–2420); `ecotopeSystem::terrainSize` static.
- `_rootPlant plants_Root` (vegetationBuilder): plant/block data buffers wired into
  clipLod/passthrough/tileClear (terrain.cpp:1041–1050), rendered from onFrameRender.
- `spriteRender mSpriteRenderer` (editor markers), `cascadeShadowMaps.h` types (stubs),
  `terrainGenerator newTerrainBuilder` (given the render context, terrain.cpp:2825).
- OpenJPH (decode+encode), cereal (XML/JSON/binary), Assimp (skydome cube.fbx load
  terrain.cpp:745–794; scene export), std::async/thread.
- Host (`Earthworks_4`): render context, hdrFbo, camera, `shaderLightBuffer`, terrainShadowTexture.

**Provides:**
- The tile GPU state (`buffer_tiles`, lookup buffers, draw/dispatch args) that gpu_tile_pipeline
  and vegetation passes consume.
- `split.feedback` (GC_feedback mirror) — picking positions/normals for roads/stamps editing and
  camera control; `heightUnderCamera`.
- `height_Array` + `tileFbo` products to the bake chain; texture arrays to render_Tiles.
- `settings`/`lastfile` path roots for every other subsystem (terrafectorEditorMaterial::rootFolder,
  ecotopeSystem::resPath, mRoadNetwork.rootPath, terrain.cpp:1010–1063).
- Offline: `bake_*` JP2 elevation re-export + fbx/obj scene export (terrain.cpp:3184–3819).

---

## 7. Falcor API surface actually used

- `Texture::create2D` — incl. **arraySize=997** and **mipLevels** variants; `createFromFile`
  (DDS/JPG); `generateMips`; `captureToFile` (bake only).
- `Buffer::createStructured` (UAV default, `IndirectArg`, `CpuAccess::Read` staging,
  `BindFlags::None`), `Buffer::create`; `setBlob` (partial updates at offsets — e.g. LOD spline
  buckets terrain.cpp:5098); `map(Read)/unmap`; **`getUAVCounter()->setBlob`** (counter reset).
- `RenderContext`: `copyResource`, `copySubresource` (texture-array slice copies — the tile
  publish step), `clearFbo`, `clearRtv`, `blit` (debug), `resourceBarrier`, `flush(true)` (bake
  only), `readTextureSubresource` (bake/export only), `updateTextureData` (host, shadow tex).
- `computeShader` wrapper: `load`, `dispatch(ctx,x,y)`, `dispatchIndirect`,
  `Vars()->setBuffer/setTexture/setSampler`, `Vars()["cb"]["member"]=`,
  `Vars()->getParameterBlock("name")->setBlob(...)` (raw cbuffer blobs),
  `getParameterBlock("viewRenderData")->findMember("terrainLookup")[i] = buffer` (**arrayed
  buffer members in a named ParameterBlock** — 18 RWStructuredBuffers per array,
  terrain.cpp:903–912; a Diligent port likely flattens this to explicitly indexed bindings or a
  bindless array).
- `pixelShader` wrapper: `load(path, vs, ps, topology[, gs])`, `drawInstanced`,
  `drawIndexedInstanced`, `renderIndirect(ctx, argsBuffer, ?, viewOffset, count)`,
  `State()->setFbo/setViewport/setRasterizerState/setBlendState/setDepthStencilState`.
- State objects: `Sampler::create` (4 configs incl. anisotropy 8/4/1 and mixed Clamp/Wrap for
  ribbons), `DepthStencilState` (Greater/LessEqual/Always, depth-write off),
  `RasterizerState` (cull none), `BlendState` incl. **independent per-RT blend across 8 MRTs**
  with `SrcAlphaSaturate` for the veg-bake and the "roads combined" premultiplied config
  (terrain.cpp:1122–1148 — the `One, OneMinusSrcAlpha` on RT0 has an author "??? hoekom" comment;
  it is what shipped, keep it).
- `Fbo::create2D` (multi-MRT desc), `getColorTexture(n)`, `getRenderTargetView(n)`.
- Misc: `FALCOR_PROFILE`, `gpFramework->getWindow()->shutdown()`, `gpDevice->getRenderContext()`,
  `openFileDialog/saveFileDialog`, `TextRenderer` (debug), `KeyboardEvent/MouseEvent/Input::*`,
  `HotReloadFlags` (no-op), `rmcv::mat4` (row-major matrix type used at shader boundary),
  `Camera::getViewMatrix/getProjMatrix/getViewProjMatrix().getTranspose()`, `Camera::setUpVector/
  setPosition/setTarget/getPosition/getTarget`.

---

## 8. Port drift notes (EarthworksFX vs this extract)

The port's `src/core/terrain.cpp` (5681 lines vs 5207 here) is the same code shaped by the June
import + compat fixes; a raw diff is ~5.1k changed lines but is dominated by formatting/logging.
Substantive drift:

- **buildings replace rappersville**: the port delegates the (commented-out-in-original)
  rappersville blocks to a new `buildingsRenderer` (port terrain.cpp:834, 2357, 3586). Developer:
  buildings work very well and have culling the original lacks — carry over, don't re-port from
  the extract (extract keeps only dead members, tagged STRIP-REVIEW terrain.h:531).
- **rmcv transpose adaptation** at the shader boundary (port terrain.cpp:3238, 3545, 4196):
  the port's rmcv::mat4 is row-major; the `[j][i]` index swap = transpose. A native Diligent
  re-port picks one convention and re-derives — do not blind-copy either side.
- **Roads kept runtime** (port terrain.cpp:1110 note): roads are content, not editor-only.
- Relevant F-findings encoding real semantics (mine, don't re-derive):
  - **F9**: GC_feedback CPU/GPU layout mismatch — explicit pads + static_asserts required for any
    read-back shared struct containing float3/float4x4.
  - **F10**: computes write tileFbo color targets via UAV — targets need UnorderedAccess bind.
  - **F11**: tile texture arrays are real arrays; `copySubresource(dst, tile->index, …)` per slice.
  - **F16**: buffers written once at load must NOT be USAGE_DYNAMIC in Diligent.
  - **F22**: `drawInstanced` is ALWAYS non-indexed regardless of any bound IB (the topdown
    terrafector bake was scrambled by this for a year).
  - **F7**: raw cbuffer `setBlob`s (frustumFlags! splitMerge children!) must actually reach the
    GPU — the two most split-critical uploads in the codebase were the first casualties.
  - **F6**: root tile draws nothing until first split; indirect args stride/offset semantics
    confirmed; frustum-plane extraction survives glm memcpy.
  - **F23/F26** concern `_shadowEdges` (atmosphere_shadows doc) but live in this file.
  - **F3/F20**: proj convention + film-back FOV feed straight into `lod_Pix` — wrong FOV silently
    shifts all split thresholds (F3.2: fovY = 2·atan(0.5·24/focalLength)).
- Known open bug (developer, task.md §3): terrafector tiles "sent to y=0" on Vulkan — the y here
  is plausibly the §2.10 origin.y/tileCenters loop or the topdown bake path; unresolved, treat
  the whole tileCenters/bake interface as a validation hot spot in the re-port.

---

## 9. Bad code / removable candidates (OPINION — do not act without developer sign-off)

- **`reset()` uploads `&split.cpuTiles` — the address of the std::vector OBJECT, not
  `.data()`** (terrain.cpp:1217). 997×44 bytes are read from the vector header + whatever
  follows: UB, uploads garbage. It has been harmless because tileBuildLookup rewrites `flags`
  every frame, splitMerge rewrites children at split, and index 0 never draws — but a re-port
  should upload `cpuTiles.data()` (and the loop zero-fills `cpuTiles` right above, so intent is
  clear).
- `#pragma optimize("", off)` at terrain.cpp:37 disables optimization for the ENTIRE translation
  unit — almost certainly leftover debugging. Removing it is a free CPU win but changes timing;
  re-测 after removal. (Opinion: remove, verify.)
- Dead/vestigial: `testForSurfaceEnv` (empty, 1609), `testFrustum` (returns true, 1662),
  `env_ShouldSplit` (written, never read), `shadowSetup/shadowRenderFar/Near/Soft/shadowRender`
  (empty, 2797–2817), `buffer_tiles_readback` (created, unused), `hashCount==2` texture-create
  duplication between elevation/image paths (copy-paste), `blockFromPositionB`/three identical
  `replaceAll*` helpers, big commented blocks (vegetation human, old bicubic, debug text).
- `i == 100000;` (compute_terrain_under_mouse.hlsl:87) is a no-op comparison — intended as a
  loop break, works anyway because `found` short-circuits nothing… it simply keeps marching;
  benign but should be `break` (behavioral check needed: later iterations can only find CLOSER
  hits due to the `distance < hitDistance` guard).
- Hardcoded 1000/1001/1024 tile-count constants scattered across shaders vs `numTiles=997` CPU —
  works because 997 < 1000, but unify behind one define in the re-port.
- `_shadowEdges` 4096²-based struct (≈ 320 MB!) lives by value inside terrainManager
  (terrain.h:76–92, 784) — the author's own comment says "MOVE TO SHADOW CLASS". At minimum,
  heap-allocate.
- The bake/export machinery (`bake_*`, `sceneToMax`, jp2 encode, ~600 lines) is offline tooling;
  candidates for the terrainGenerator/pipeline tool rather than the runtime renderer.
- Editor-input code (onKeyEvent/onMouseEvent_Roads/_Stamps, stamps-to-bezier) is deferred-editor
  scope; the runtime port needs only the §2.12 picking + camera-control path from onMouseEvent.

---

## 10. Open questions / uncertainties (flagged, not guessed)

1. **`splitChild` runs the bake with the CHILD's own elevation hash even though `dataReady` was
   verified against the PARENT tile pre-split**: `hashAndCache(tile)` binds the parent's texture;
   after `setChild` copies hashes, children may upgrade to their own hash only at their NEXT
   split. `splitChild` reads `elevationTileHashmap[_tile->elevationHash]` (terrain.cpp:2341)
   which at that moment equals the parent's — consistent, but the exact moment a tile's own
   1024² data first gets used (its own split) should be verified in a debugger before the
   re-port assumes it.
2. `terrainCamera.viewProj/viewProjTranspose` members are computed (terrain.cpp:1698) but I found
   no consumer in this file — used elsewhere or dead?
3. `t < 1000` vs `t < 1001` vs 997: are indices 997–999 ever touched with garbage gpuTile data?
   (flags come from the zeroed CPU array so they pack nothing; likely benign.)
4. The dual split thresholds (150 in-frustum / 300 always) and the `resolution*fovscale/4` (split)
   vs `/2` (render clip, terrain.cpp:2890) discrepancy look deliberate but are undocumented —
   confirm with the developer before any "unification".
5. `Fbo::create2D(..., 1, 8)` — 8 mip levels on the tileFbo/bakeFbo: only mip 0 is used since
   `compute_tileElevationMipmap` is commented out (terrain.cpp:968–980). Waste, or does some
   shader SampleLevel a higher mip? (No hit found in the terrain shaders; ecotope shader samples
   `gLowresHgt` = rootElevation mips instead.)
6. Elevation decode reads `codestream.pull` for exactly 1024 lines / images exactly 1024×3
   components — no dimension validation against the actual file. Assumed dataset invariant
   (all JP2 tiles are 1024²); a re-port should assert it.
7. `elevationCache.get` in `bake_Setup` (terrain.cpp:3370) uses `!get(...)` then binds
   `map.texture` from the FAILED get (default-constructed) — looks like an inverted condition
   in the bake path (offline only). Not verified against pristine.
8. Whether Falcor's `map(Buffer::MapType::Read)` fully syncs the GPU each frame (perf note §4.3)
   is asserted from Falcor semantics, not measured here — validate before copying the
   same-frame-readback design into Diligent.
