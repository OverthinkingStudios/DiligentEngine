# Concept Catalog — Roads + Terrafectors (terrain-affecting stamps)

Sources analyzed (extract, `port_effort_3/source_extract_3/`): `earthworks_scene/roadNetwork.*`,
`roads_road.*`, `roads_bezier.*`, `roads_Intersection.*`, `roads_cubicDouble.*` (empty stubs),
`roads_materials.*`, `roads_physics.*`, `terrafector.*`, plus the roads/terrafector parts of
`terrain.cpp/.h`; shaders `hlsl/terrain/render_spline.hlsl`, `render_splineTerrafector.hlsl`,
`render_meshTerrafector.hlsl`, `materials.hlsli`, `terrainDefines.hlsli`.
Context: `EarthworksFX/src/core`, `EarthworksFX/BRINGUP_NOTES.md`.

Scope note: **`render_ribbons.hlsl` is NOT a roads shader.** It is loaded once as `ribbonShader`
(terrain.cpp:702, `Vao::Topology::LineStrip` + GS) for paraglider/vegetation ribbons and uses
`sprite_material`/`xformed_PLANT` — it belongs to the vegetation catalog. Roads use
`render_spline` (3D overlay) and `render_splineTerrafector` (bake). `roads_cubicDouble.cpp/.h`
are empty (3/4 lines, include-only) — carry nothing.

---

## 1. Purpose & data flow

Terrafectors are **terrain-affecting decals**: they are never drawn in the 3D scene. Everything
(FBX land-use meshes, road splines, quad "stamps") is rasterized **top-down into the tile-bake
FBO** during quadtree tile splits, modifying the tile's elevation/albedo/PBR/ecotope textures,
which the terrain then renders. Roads additionally have a thin 3D overlay path for editing.

### CPU-side pipelines (all load-time / edit-time, not per-frame)

**A. Mesh terrafectors (FBX → tiled GPU triangle soup)**
1. `terrafectorSystem::loadPath(dirRoot+"/terrafectors", dirRoot+"/bake")` — terrain.cpp:1061,
   terrafector.cpp:1177. Recursive directory scan (`terrafectorElement::loadPath`,
   terrafector.cpp:1036) picks up `.fbx/.obj/.dxf`.
2. Per mesh: `splitAndCacheMesh` (terrafector.cpp:825) imports via Assimp
   (`aiProcess_FlipUVs|Triangulate|PreTransformVertices|JoinIdenticalVertices|GenBoundingBoxes`),
   splits every triangle into **LOD grid tiles** (`lodTriangleMesh`, LOD2/4/6), and caches the
   result as `.lod{2,4,6}Cache` cereal binaries with file-timestamp invalidation
   (`isMeshCached`, terrafector.cpp:807).
3. **Filename conventions route meshes to combiners** (terrafector.cpp:835-837, 891-916):
   substring `"LOD2"` → also LOD2; `"bakeOnlyBottom"`/`"bakeOnlyTop"` → bake-only low/high;
   `"overlay"` → GIS overlay; `"50_top"` → `_top` combiners (drawn last = on top);
   `"bakeOnly"` in a directory name sets `bakeOnly` on the element (terrafector.cpp:943).
4. `lodTriangleMesh_LoadCombiner::addMesh` (terrafector.cpp:414) appends per-tile vertex/index
   vectors, remapping the mesh's material names to global `materialCache` indices.
5. `loadToGPU` (terrafector.cpp:473) pads each tile's index list to **whole 128-triangle blocks**
   (padding indices = 0 → degenerate triangles), creates two structured buffers per tile
   (`triVertex` VB, `uint` IB), optionally writes `terrafector_lodN.gpu` for the EVO export,
   then frees all CPU copies.

**B. Roads (spline network → GPU bezier + layer-index buffers)**
1. `roadNetwork::load(path)` (roadNetwork.cpp:370) — cereal binary of `roadSectionsList` +
   `intersectionList` + `roadMaterialCache`; re-links intersections and calls `solveRoad()`
   twice per road (deliberate: solve→optimize feedback needs a 2nd pass).
2. `roadSection::solveRoad` (roads_road.cpp:1193): `solveWidthFromLanes` → `solveStart/End`
   (intersection welds) → per bezier lane {middle, left, right}: `solveEnergyAndLength`,
   `solveUV`, `optimizeSpacing`, `optimizeTangents`. Details in §2.
3. `roadNetwork::updateAllRoads` (roadNetwork.cpp:993): every road →
   `convertToGPU_Realistic(staticBezierData, staticIndexData, staticIndexData_BakeOnly)`
   (roads_road.cpp:168) producing:
   - `staticBezierData`: `cubicDouble` (2 parallel cubic beziers = a curved quad strip, 128 B),
   - `staticIndexData`: `bezierLayer` (2×uint, one *drawable layer* referencing a bezier),
   - `staticIndexData_BakeOnly`: per segment one `MATERIAL_SOLID` (inner geometry) + one
     `MATERIAL_BLEND` (outer verge falloff) layer — **this is what flattens terrain under roads**.
4. Upload in `terrainManager::update` when `isDirty` (terrain.cpp:1831-1850): `setBlob` into
   `splines.bezierData/indexData/indexDataBakeOnly`, then `bezierRoadstoLOD(4)`
   (terrain.cpp:4996) **bins every static layer into per-tile lists** for LOD4 (16×16),
   LOD6 (64×64), LOD8 (256×256) grids and uploads them packed into
   `indexData_LOD4/6/8` with `startOffset_LODn[y][x]` / `numIndex_LODn[y][x]` tables
   (also written to `bake/roadbeziers_*.gpu` for EVO).
5. Dynamic (currently edited) road/intersection: `updateDynamicRoad` (terrain.cpp:3948,
   roadNetwork.cpp:960) rebuilds only the current road into
   `dynamic_bezierData/dynamic_indexData` in stylized (editing-material) form.

**C. Stamps (quad decals, e.g. road markings/patches)**
- `stamp` = pos/right/dir/material quad (roads_road.h:225). Live cursor stamp →
  `currentStamp_to_Bezier` (terrain.cpp:3849) packs ONE `cubicDouble` with straight control
  points + `bezierLayer` with **bit 29 (`isQuad`) forced** (terrain.cpp:3845) into the dynamic
  spline buffers. Placed stamps → `allStamps_to_Terrafector` (terrain.cpp:3911): two triangles
  per stamp inserted into a **LOD7 (128×128) mesh combiner** (`loadCombine_LOD7_stamps`) —
  i.e. persisted stamps bake through the *mesh* path, not the spline path.

**D. Physics/surface queries (`roads_physics.*`, kept for future consumers)**
- `ODE_bezier`: 2D spatial hash grid (`bezierFastGrid`, 50 m cells, 500×500 —
  roadNetwork.cpp:26) over `physicsBezier` bounding circles + start/end half-planes; per-query
  incremental cache (`bezierCache`, 10 slots × 17 tessellated segments with precomputed
  tangent/inner/outer 2D planes). `intersect()` walks planes with a persistent `t_idx` and a
  `distanceTillNextSearch` early-out (distance-to-fail metric) so a moving query does near-zero
  work per frame. Used at edit time for `testHit`/`lanesFromHit` (lane under mouse).

### GPU-side / per-frame
- **Bake at tile split**: `splitChild` (terrain.cpp:2316) clears the tile FBO, runs bicubic
  height upsample into RT0, then `splitRenderTopdown` (terrain.cpp:2554) draws all terrafector
  layers top-down (see §2.4), then compute passes consume RT0/RT1/... (ecotopes, normals,
  vertices, Delaunay). One tile per frame (`splitOne`). No bake happens outside splits.
- **Offline bake** (`bake_start/bake_frame/bake_Setup/bake_RenderTopdown`, terrain.cpp:3184-3707):
  iterates lods 4..7 over tile masks (`bake/lod{n}.raw`, value 255 = bake), renders the same
  stack into the 1024² `bakeFbo`, reads back RT0, min/max normalizes and writes JP2 elevation
  tiles (`hgt_lod_y_x.jp2`) + `tiles.list` — this REGENERATES the streamed terrain elevation
  data with roads/terrafectors burned in.
- **3D overlays** (`onFrameRender`, terrain.cpp:2999-3064): `render_spline.hlsl` draws the
  dynamic stamp (terrafector mode) or the dynamic/static road splines when
  `showRoadSpline && !bSplineAsTerrafector` — editing visualization only.

---

## 2. Core tricks & clever mechanisms (MUST NOT be lost)

### 2.1 `cubicDouble` — two beziers spanning a curved ribbon; UV packed in `.w`
`roads_bezier.h:24-45`: `float4 data[2][4]` = [inner, outer][P0..P3]; each control point's `.w`
carries the road-length U coordinate, interpolated by the same Casteljau evaluation as position
(free perspective-correct UVs along the curve). Left-hand-side beziers are built **backwards**
(direction reversed, roads_bezier.cpp:22-51) and their packed `u` negated
(roads_bezier.cpp:154-163) so texture direction flips correctly; the shader decodes via
`isFlipped` (bit 28) `texCoords.x *= -1`. The lane constructor (roads_bezier.cpp:115-165)
derives lane sub-beziers by **lerping between middle and edge control points by the lane's
width-percentages** — lanes cost 128 bytes each but zero extra solve work. The clearing/verge
constructor (roads_bezier.cpp:55-111) offsets the outer bezier by `w` along
`-normalize(cross(up, tangent))` and rescales tangent lengths by the chord-length ratio
(`distance2/distance`) to keep curvature sane.

### 2.2 `bezierLayer` — 8-byte drawable "layer" instance (bit-packed)
Constructor `roads_physics.cpp:17-42`; decode macros `render_splineTerrafector.hlsl:76-86`:
```
A: [31] inner edge sel (0=center,1=outside)  [30] outer edge sel
   [28:18] material (11 bits, <=2047)        [16:0] bezier index (17 bits, <=131071)
B: [31] isStartOverlap  [30] isEndOverlap  [29] isQuad  [28] isFlipped(_left)
   [27:14] w0  [13:0] w1     w encode: (w+16) clamp[0,32] * 500  → ±16 m in 2 mm steps
```
(The struct-comment bit counts in the shaders, e.g. `[3][13][16]`, are STALE — the macros are
authoritative.) One `bezierLayer` = one instanced draw slice: a road is a *stack* of layers
(verge, tarmac, sidewalk, gutter, paint lines, rubber) all referencing the same few beziers with
different materials/edge-offsets. `bezierIndex` combines with the cbuffer `startOffset` so
LOD-binned index lists can address the single global bezier buffer.

### 2.3 Spline VS: bezier evaluated per-vertex, 64×6-index instancing
`render_splineTerrafector.hlsl:89-131` / `render_spline.hlsl:99-133`: draw call is
`drawIndexedInstanced(ctx, 64*6, numLayers)` using the shared **128-quad-pattern index buffer**
(0,1,2, 1,3,2, 2,3,4, … from `pixelShader::load`); vertex IDs 0..129 map to `t = (vId>>1)/64`,
odd/even = outer/inner edge. Each vertex Casteljau-evaluates both beziers, takes
`perpendicular = normalize(outer-inner)` and offsets by w0/w1 — geometry, widths and UVs all
come from 128 B + 8 B of data per curve. `isQuad` (stamps) replaces Casteljau by straight lerp
(render_spline.hlsl:109-113; note: only render_spline has this branch — the bake shader doesn't,
persisted stamps go through the mesh path instead). Overlap fade: `isStartOverlap/isEndOverlap`
segments (bridges) get a per-vertex alpha ramp `saturate(1-(vId>>1)*0.2)`
(render_splineTerrafector.hlsl:102-112) so bake layers feather out instead of hard-cutting.

### 2.4 The bake pass — coordinate mapping, ordering, and the elevation blend trick
**This is the developer's #1 problem area; every detail here is load-bearing.**

*Camera* (terrain.cpp:2554-2581, identical in bake_RenderTopdown:3536-3559): hand-built
top-down view: `V[0]=(1,0,0,0) V[1]=(0,0,1,0) V[2]=(0,-1,0,0) V[3]=(-x, z, 0, 1)` (world X→
clip x, world Z→clip y **negated via the translation sign**, world Y→depth), then
`P = glm::orthoLH(-s, s, -s, s, -10000, 10000)` with `s = tileSize/2 * 256/248`. The ×256/248
(`tile_toBorder`) makes the ortho window cover the tile's **4-pixel filter border**
(tile_numPixels=256, tile_InnerPixels=248, terrainDefines.hlsli:6-9); the bicubic height pass
uses the same border convention, so bake and height are pixel-aligned. The glm→`rmcv` element
copy `view[j][i] = V[j][i]` (terrain.cpp:2575-2580) is an **implicit transpose** (rmcv is
row-major in original Falcor) matching HLSL `mul(pos, viewproj)` — see §8 for the port trap.
`GLM_FORCE_DEPTH_ZERO_TO_ONE` must be defined or `orthoLH` produces GL depth (BRINGUP F20).

*LOD selection of stamps* (terrain.cpp:2618-2792): each combiner lives on a fixed grid; a tile
at `lod` indexes it by `(y >> (lod-N)) * gridN + (x >> (lod-N))` with N = combiner LOD
(LOD2→4², LOD4→16², LOD6→64², LOD7 stamps→128²). Mesh combiners are chosen by tile depth:
`lod>=6 → LOD6`, `else lod>=4 → LOD4`, `else lod>=2 → LOD2` (near tiles get denser split
meshes; the same triangles exist in each, pre-clipped to that grid with a
`tileSize/248*4` buffer margin — terrafector.cpp:230, 286-373). Road layers: tile `lod>=8` uses
the LOD8 bin of its lod-8 ANCESTOR (`while (P8->lod > 8) P8 = P8->parent`), etc.
(terrain.cpp:2722-2745). Bin margins in `bezierRoadstoLOD`: ±80 m AABB pad plus
`borderSize = 4*pixelSize + splineWidth` (terrain.cpp:5046, curve-bulge compensation);
LOD4 doubles `splineWidth` as a run-off heuristic (terrain.cpp:5049); a layer enters lod4/6
bins only if `splineWidth > pixelSize` (thin paint lines are skipped at coarse LOD, everything
enters LOD8) — terrain.cpp:5050.

*Draw order = priority order* (splitRenderTopdown, terrain.cpp:2617-2792; bake variant
3597-3705 identical minus overlay/stamps/top and with `bSplineAsTerrafector` checks
commented IN — the offline bake ALWAYS bakes roads):
1. `loadCombine_LOD4_bakeLow` (mesh, bake-only ground)
2. road **bakeOnly** layers (`indexDataBakeOnly`, full buffer, startOffset 0) — road solid+blend
3. `loadCombine_LOD4_bakeHigh` (mesh, bake-only top)
4. main mesh terrafectors (LOD6 or LOD4 or LOD2)
5. GIS overlay meshes (`LOD4_overlay`, alpha = `gis_overlay.terrafectorOverlayStrength`)
6. road static layers from the LODn bin (materials incl. paint/rubber + MATERIAL_CURVATURE)
7. stamps (`LOD7_stamps` mesh)
8. `_top` mesh combiners (LOD6_top/LOD4_top) — always last, always on top.
Steps 1-3 are gated by `gis_overlay.bakeBakeOnlyData`; 2 and 6 additionally by
`bSplineAsTerrafector` in the split path.

*The elevation blend trick* (the core mechanism): tile FBO RT0 is **R32Float elevation**
(terrain.cpp:614). Blend state `blendstateRoadsCombined` (terrain.cpp:1127-1137): independent
blend, RT1-7 = `SrcAlpha/OneMinusSrcAlpha` (both color and alpha); **RT0 =
`One/OneMinusSrcAlpha`** (line 1136 — the `//??? hoekom het ek dit gedoen` override). Combined
with the shader (`solveElevationColour`, materials.hlsli:183-262):
- *relative* materials (`useAbsoluteElevation == 0`) write `Elevation = (h*alpha, a=0)` →
  blend degenerates to `dst += h*alpha` (additive height detail);
- *absolute* materials (`useAbsoluteElevation`, default true) write
  `Elevation = (H*alpha [+ vertexY*alpha if useVertexY], a=alpha)` →
  `dst = H*alpha + (1-alpha)*dst` (height REPLACE with feathering);
- materials with `useElevation == 0` write `Elevation = (0, a=0)` → **no height change at all**.
One blend state expresses add, replace and no-op, selected per-pixel by shader-written alpha.
Roads use fixed pseudo-materials for this (render_splineTerrafector.hlsl:148-178):
`MATERIAL_SOLID` (2046) = replace with the spline's own world Y (`posW.y`, i.e. the road
surface height solved on CPU), `MATERIAL_BLEND` (2045) = replace weighted by
`smoothstep(colour.r)` where `colour.r = 1-|u|` (feathered verge), `MATERIAL_CURVATURE`
(2047) = additive crown `0.1*pow(cos(|u|*π/2), 0.85)` with a=0. **This is how roads flatten
and crown the terrain.**

### 2.5 128-triangle block instancing for mesh terrafectors
`render_meshTerrafector.hlsl:55-74` + terrafector.cpp:473-520: tiles draw with
`drawInstanced(128*3, numBlocks)` (NON-indexed; SV_VertexID + SV_InstanceID compute
`idx = iId*384 + vId`, manual fetch through structured index+vertex buffers). Index lists are
padded to full blocks with index 0 → degenerate zero-area triangles, so no CPU-side draw-count
fiddling. `triVertex` is exactly 32 bytes: `pos(12) alpha(4) uv(8) material(4) buffer(4)` —
the trailing `buffer` pad is load-bearing for the structured-buffer stride
(terrafector.h:96-113). Per-vertex `alpha` comes from FBX vertex color R (terrafector.cpp:210)
and drives `vertexAlphaScale` feathering in `solveAlpha`.

### 2.6 Material system: one 464-byte constant record, 4096-texture bindless array
`terrafectorEditorMaterial::_constData` (terrafector.h:325-430) mirrors `TF_material`
(materials.hlsli:8-78) byte-for-byte, including explicit `float3 buf_____02/03/05` padding —
**a past agent deleting "unused" padding broke rendering; the paddings align the HLSL cbuffer
packing and MUST survive**, as must the field order. All materials live in one structured
buffer `sb_Terrafector_Materials` (2048 entries, created terrain.cpp:1087, filled by
`rebuildStructuredBuffer` via `setBlob` per material — terrafector.cpp:770). All textures live
in one `Texture2D<float4> T[4096]` ParameterBlock shared by spline/mesh/bake shaders
(`materialCache::setTextures`, terrafector.cpp:760: `var[i] = texture`). Material IDs > 2030
are fixed/editing pseudo-materials (materials.hlsli:91-100), which caps real materials at 2031
even though the field holds 2047. `MATERIAL_TYPE_MULTILAYER` (materialType 1) resolves up to
8 sub-materials packed as `(alpha<<16 | matIndex)` in `subMaterials[]`
(terrafector.cpp:781-804; shader loop render_splineTerrafector.hlsl:209-238) — sub-material
recursion depth is exactly 1.

### 2.7 Road solver: energy-based tangent relaxation
`solveEnergyAndLength` (roads_road.cpp:1092-1189) tessellates each segment 64×, accumulates a
"pull" from cross products of consecutive chords (curvature energy) plus a chord-direction
stabilizer (`* 0.145f`, empirically tuned — comment "HIERDIE DEEL WERK" = "this part works"),
weighted by per-point bias `B`; `optimizeTangents` (851-891) relaxes tangent directions against
that pull with continuity `C`; `optimizeSpacing` (901-930) equalizes Casteljau step lengths by
scaling `dst_Forward/dst_Back` by `1+(dm-da)/(dm+da)`. `solveUV` (934-969) distributes integer
texture repeats (`uvScale`, default 8 m) over total middle-lane length, then assigns
`u`, `u_back = u - dU/3`, `u_forward = u + dU/3` — the ±1/3 offsets are exactly what makes the
packed `.w` Casteljau interpolation produce linear arc-length UV (cubic through control values).
All the magic constants (0.145, ×2 tangent at intersections, 0.33 pushback fractions,
1.4 corner-radius factor, blend window `(theta-1.7)*3`) are hyper-tuned — do not "clean up".

### 2.8 Intersection solver
`roadNetwork::solveIntersection` (roadNetwork.cpp:1197-1413): links sorted by approach angle;
per adjacent pair computes corner position either as an "open corner" (T-junction flat side,
`dot(right_A, dir_B) < 0.5`) or a radius corner `vec_C = dir_B*(wA+r)/sinθ + dir_A*(wB+r)/sinθ`
pulled back along `normalize(vec_C)*r*0.8`; corner tangent magnitude
`D = (π/2 - θ/2) * r / 3` is the exact cubic-bezier circular-arc approximation. Road end
vertices are force-anchored to the intersection and the *next* vertex pushed back
`max(pushBack_A, pushBack_B, roadWidth)`; `roadSection::solveStart/solveEnd`
(roads_road.cpp:652-774) then overwrite the end point's three bezier points from the link's
corners/tangents (middle tangent doubled). Corner data is serialized, so **loading a network
does not require re-solving intersections** (only `solveRoad` per road).

### 2.9 Terrafector texture auto-compression
`materialCache::find_insert_texture` (terrafector.cpp:682-745): non-DDS textures are converted
on first use by shelling out to CompressonatorCLI (mips → BC6H for sRGB / BC7 otherwise) into
`<name>.earthworks.dds` next to the source. Hardcoded `F:\terrains\_resources\...` paths —
pipeline-tool behavior that needs a home in the port (or precompressed data).

---

## 3. Invariants & conventions

- World: Y-up, terrain spans `±terrainSize/2` (`ecotopeSystem::terrainSize`, 40 000 m);
  tile grid index `y*grid + x`, `y` derived from world **Z** (terrafector.cpp:295, 336).
  Assimp import default is MAX Z-up: `pos.y = src.z; pos.z = -src.y` unless `_yup`
  (terrafector.cpp:191-198) — the YZ-flip sanity logs (terrafector.cpp:271-282) exist because
  artists get this wrong.
- Tile bake: 256² px, 248 inner + 2×4 border; ortho half-size = `tileSize/2 * 256/248`;
  depth range [0,1] (GLM_FORCE_DEPTH_ZERO_TO_ONE), depth test `Always`, depth write OFF,
  cull NONE for all terrafector/spline draws (terrain.cpp:1104-1120).
- Draw order defines layer priority (§2.4 list) — there is no depth/priority sort on GPU.
- bezierLayer bit layout and w-encoding (§2.2) are shared by C++ writer, bake shader, overlay
  shader and `bezierRoadstoLOD`'s CPU decode (terrain.cpp:4998-5037) — four copies of the same
  contract; change one, change all.
- `splines.maxBezier = 131072` matches the 17-bit index field exactly; `maxIndex = 524288`;
  dynamic 4096/16384 (terrain.h:718-740). LODn index buffers are `maxIndex*2` "for safety".
- Buffer strides: `cubicDouble` 128 B, `bezierLayer` 8 B, `triVertex` 32 B, `TF_material`
  464 B — all consumed as StructuredBuffer with these exact sizes.
- Instanced spline draws require the shared 128-quad-pattern index buffer
  (`pixelShader::load`); `drawInstanced` (mesh terrafectors) must be NON-indexed
  (BRINGUP F22 — Falcor semantics).
- FBO semantics (terrain.cpp:612-623): RT0 elevation R32F, RT1 albedo R11G11B10F, RT2 PBR
  R11G11B10F, RT3 "alpha"/permanence R11G11B10F, RT4-7 ecotopes RGBA8. Clear colors in
  `splitChild` (2328-2335) are data: PBR clears to (1, 0.07, 1) etc. RT3 permanence output is
  `1-permanence*` per channel (materials.hlsli:244) consumed by the ecotope compute.
- Road cereal versioning: `ROADNETWORK_CEREAL_VERSION 103`; `roadNetwork::upgrade` reads any
  older version and re-saves — versioned `serialize` bodies (roads_bezier.h, roads_road.h) are
  the file-format spec; do not touch field order.
- `roadSection` GUIDs are indices into `roadSectionsList`; deletion never compacts (see
  `deleteCurrentIntersection` comment, roadNetwork.cpp:1483) — code assumes GUID == index.
- Editing state machine: exactly one of `currentRoad`/`currentIntersection` is non-null.
- `solveRoad` is called twice after load (roadNetwork.cpp:388-389) — keep it; single solve
  leaves tangent relaxation one iteration short of its fixed point.

## 4. Performance-critical details

- **Bake cost model**: one tile split per frame; the whole terrafector stack re-rasterizes into
  256² each split. Cheapness relies on (a) per-tile pre-clipped mesh combiners (no CPU culling
  at bake time, just an array index), (b) LOD-binned road index lists (`startOffset/numIndex`
  lookup, zero per-bake CPU work), (c) tiny per-instance data (8 B) with the bezier math in the
  VS. Replacing the binning with per-draw CPU culling or drawing all layers per tile would
  multiply bake cost by orders of magnitude.
- 128-triangle blocks + degenerate padding avoid per-tile draw-parameter churn and keep a
  single PSO; the `numBlocks` granularity wastes at most 127 triangles per tile.
- `#pragma optimize("", off)` is ACTIVE at the top of terrafector.cpp:3 and roadNetwork.cpp:7
  (debug leftovers). Load-time only, but the mesh split loops (`insertTriangle` over full
  grids) run unoptimized — port candidates for removal, measure load time.
- `lodTriangleMesh::insertTriangle(material, pos, uv)` (stamp path, terrafector.cpp:286) scans
  the **entire** grid (LOD7 = 16384 tiles) per triangle; the FBX path uses the AABB-limited
  `xMin..xMax` variant (329). Fine for hundreds of stamps; do not use the full-scan variant for
  meshes.
- `.lodNCache` binary caches make terrafector reload ~IO-bound; killing them re-runs Assimp +
  grid split per FBX every start.
- The ODE bezier cache is designed for *incremental* queries (cars moving along roads):
  persistent `t_idx`, `distanceTillNextSearch`, and cell-hash change detection. Calling it with
  random positions each query defeats all three.
- Overlap (bridge) segments still rasterize with alpha 0 (fade ramp) — they must stay in the
  buffers for the ramp to work; "optimizing them out" breaks bridge blending.
- `updateDynamicRoad` uploads only when `bRefresh || isDirty` (terrain.cpp:3968-3984) —
  static buffers are NOT touched during dragging; only the small dynamic buffers are.
- Texture memory accounting `texture_memory_in_Mb` (terrafector.cpp:727) is diagnostic only.

## 5. GPU resources & shader interface

### Buffers (created in `init_TopdownRender`, terrain.cpp:1084-1159)
| Name | Stride/Count | Producer → Consumer |
|---|---|---|
| `sb_Terrafector_Materials` | 464 B × 2048 | `rebuildStructuredBuffer` setBlob → `materials` in all 3 shaders |
| `splines.bezierData` | 128 B × 131072 | update() setBlob → `splineData` (bake + overlay) |
| `splines.indexData` | 8 B × 524288 | update() → `indexData` (overlay static) |
| `splines.indexDataBakeOnly` | 8 B × 524288 | update() → `indexData` (bake step 2) |
| `splines.indexData_LOD4/6/8` | 8 B × 1048576 | `bezierRoadstoLOD` per-bin setBlob → `indexData` (bake step 6) |
| `splines.dynamic_bezierData` | 128 B × 4096 | updateDynamicRoad / currentStamp → `splineData` (overlay) |
| `splines.dynamic_indexData` | 8 B × 16384 | idem → `indexData` |
| per combiner-tile `vertex` | 32 B × numVerts | loadToGPU → `vertexData` (mesh bake) |
| per combiner-tile `index` | 4 B × numBlocks·384 | loadToGPU → `indexData` (mesh bake) |

### `render_splineTerrafector.hlsl` (bake) / `render_spline.hlsl` (overlay)
- cbuffer `gConstantBuffer`: bake = `{float4x4 viewproj; uint startOffset;}`;
  overlay = `{float4x4 viewproj; float alpha;}` (row-vector `mul(pos, viewproj)` —
  matrix must arrive transposed relative to glm column-major, see §2.4/§8).
- SRVs: `materials`, `splineData`, `indexData`; ParameterBlock `gmyTextures { Texture2D<float4> T[4096]; }`.
- Samplers s0-s3 declared; only `gSmpLinear` (s1) is set by C++ (terrain.cpp:1090 — spline3D
  only; the terrafector bake shaders never get their samplers set explicitly in the extract:
  Falcor default samplers apply. Port must provide a linear-wrap sampler).
- VS out: pos, world pos, `texCoords = (±(1-(vId&1)), U_from_bezier_w, t)`, `flags.y=material`,
  colour(r = 1-|u| feather, a = overlap alpha).
- PS out (bake): `PS_OUTPUT_Terrafector` = 8 RTs (materials.hlsli:169-179). Overlay PS: single
  float4.

### `render_meshTerrafector.hlsl` (bake)
- cbuffer `gConstantBuffer { float4x4 viewproj; float overlayAlpha; }`; SRVs `materials`,
  `vertexData` (triVertex), `indexData` (uint), same 4096-texture block; same PS as spline bake
  plus `overlayAlpha` multiplied into alpha (render_meshTerrafector.hlsl:139).
- Includes `../render_Common.hlsli` (unused declarations) — drop candidate but verify.

### Blend/depth/raster state (terrain.cpp:1104-1148) — load-bearing, see §2.4
`blendstateRoadsCombined`: independent; RT0 One/OneMinusSrcAlpha (color AND alpha), RT1-7
SrcAlpha/OneMinusSrcAlpha. `blendstateSplines` (overlay): SrcAlpha/OneMinusSrcAlpha with
alpha-factors Zero/Zero (dst alpha preserved). Depth: `depthstateAll` = test Always, write off.
Raster: solid, cull NONE. `depthstateCloser/Futher` are created but unused by these passes.

### TF_material layout
464 bytes; see materials.hlsli:8-78. The three `float3 buf_____NN` pads and the
`uint subMaterials[8]` + `float4 ecotopeMasks[15]` tails are part of the byte contract with
`terrafectorEditorMaterial::_constData` (terrafector.h:325-430). `_constData` is written to
disk (cereal AND raw `fwrite` in `exportMaterialBinary`/`exportBinary`) — the struct is a
file format too.

## 6. Dependencies

Consumes:
- `ecotopeSystem::terrainSize` (world size), `terrafectorSystem::pEcotopes` (set by terrain).
- Assimp (FBX import/export), cereal (all persistence), OpenJPH (bake JP2 output), ImGui only
  via stripped GUI remnants (`imgui.h` includes in roads_road/roads_physics — dead in extract).
- `split.feedback.tum_Position/tum_Normal` (terrain-under-mouse compute) for stamp placement
  and road editing.
- `materialCache::selectedMaterial` (set by editor GUI, read by `currentStamp_to_Bezier`).

Provides:
- Tile FBO contents (elevation/albedo/PBR/permanence/ecotopes) → tile compute chain
  (`compute_tileEcotopes/Normals/Vertices/Delaunay` all read RT0; terrain.cpp:880-962) →
  everything the terrain renders, including where vegetation grows (ecotope RTs).
- `height_Array` per-tile copy of RT0 (terrain.cpp:2378) → picking.
- Offline: EVO export set (`terrafector_lodN.gpu`, `roadbeziers_*.gpu`, `TextureList.gpu`,
  `Materials.gpu`, JP2 elevation tiles, bridge OBJ/FBX, per-block road FBX).
- `roads_physics` surface query results (`bezierIntersection.height/grip`) — editor use today,
  future game consumers.

## 7. Falcor API surface actually used (port-path input)

- `Buffer::createStructured(stride, count [, BindFlags::ShaderResource|UnorderedAccess,
  CpuAccess::None, initData])`, `Buffer::setBlob(data, offset, size)`.
- `Texture::createFromFile(path, genMips, sRGB)`, `Texture::create2D`,
  `tex->setSourcePath/getSourcePath/setName/getWidth/getHeight`, `captureToFile` (bake PNG).
- `Fbo::create2D` + `Fbo::Desc` (8 MRT + D24S8), `getColorTexture`, `getRenderTargetView`.
- `RenderContext`: `clearFbo`, `clearRtv`, `copySubresource`, `copyResource`,
  `readTextureSubresource` (sync readback in bake!), `resourceBarrier`, `flush(true)`, `blit`.
- `BlendState/DepthStencilState/RasterizerState` Desc+create (incl. `setIndependentBlend`,
  `setRenderTargetWriteMask`, `setRtParams` with separate alpha factors).
- Earthworks `pixelShader` wrapper: `load(file, "vsMain", "psMain", Topology::TriangleList)`,
  `Vars()` ShaderVar tree (`["gConstantBuffer"]["viewproj"] = rmcv::mat4`, `setBuffer`,
  `setSampler`, `getParameterBlock("gmyTextures")->findMember("T")`, `var[i] = tex` array
  binding), `State()` (setFbo/setRasterizerState/setBlendState/setDepthStencilState/
  setViewport), `drawInstanced(ctx, vtxCount, instCount)` (non-indexed),
  `drawIndexedInstanced(ctx, idxCount, instCount)` (shared quad-pattern IB).
- `rmcv::mat4` (row-major matrix shim), glm everywhere else.
- `openFileDialog/saveFileDialog + FileDialogFilterVec` (editor-only functions kept in extract:
  road load/save, exports).
- `TriangleMesh::SharedPtr` (terrafectorElement::pMesh — never assigned in extract; drop).
- `Gui*` parameter in `stampPad::renderGUI` stub (roads_road.h:204) — dead.

## 8. Port drift notes (EarthworksFX) & known bugs

Diff status vs extract: `roads_bezier.cpp`, `roads_physics.cpp`, `roads_Intersection.cpp` are
byte-identical; `roads_road.cpp` ~5 lines; `terrafector.cpp`/`roadNetwork.cpp`/
`roads_materials.cpp` diffs are dominated by editor-GUI code that the extract stripped, plus
port-side debug instrumentation.

Relevant F-findings (mine, don't re-derive — BRINGUP_NOTES.md):
- **F22**: compat `drawInstanced` wrongly used the quad-pattern IB → ALL mesh-terrafector bakes
  were scrambled until 2026-07-03. Falcor semantics: `drawInstanced` is always non-indexed.
- **F25**: `Texture::createFromFile` stub (1×1 undefined) → black land-use polygons baked into
  tiles; fixed with real loader; missing files now fall back to 1×1 **transparent** (alpha 0)
  so bake consumers skip them.
- **F13/F18**: the 4096-entry texture arrays exhaust Vulkan dynamic descriptor pools / D3D12
  GPU heap; fixed by enlarging pools. Long-term recommendation recorded: bind `textures_T` as
  MUTABLE, not per-draw DYNAMIC.
- **F15**: arrayed ShaderVar binding (`var[i]=tex`) needed a dedicated `SetArray` path with
  dummy-padding of unset elements — a fresh Diligent port needs the same (unset array slots =
  invalid descriptors on both APIs).
- **F16**: the shared quad-pattern IB must NOT be USAGE_DYNAMIC (device-loss root cause).
- **F20**: RH camera / CCW front faces / `GLM_FORCE_DEPTH_ZERO_TO_ONE` must be public — the
  bake's `orthoLH` depends on the latter.
- **PORT NOTE in port terrain.cpp:3238-3250**: original `rmcv::mat4` is ROW-major, so the
  original's element-copy `view[j][i] = V[j][i]` was an implicit transpose. The port's rmcv is
  glm, so it must copy `[j][i] = V[i][j]` — otherwise every bake vertex leaves clip space and
  the bake is empty. **Any re-port must decide the matrix convention once and mirror it here.**
- Port shaders replaced `ParameterBlock<myTextures> gmyTextures` with a flat
  `Texture2D<float4> gmyTextures_T[4096]` and moved struct decls before `materials.hlsli` —
  functional drift, remember shaders are re-ported fresh from the originals.
- Port `render_meshTerrafector.hlsl` contains TEMP DEBUG `dbgTagSuspicious` (tags fragments
  with elevation≈0 & alpha≈1) and port terrain.cpp has a TFBAKE trace harness
  (per-pass RT0 center readback, "incriminate" logging, `blendstateRoadsCombined_noElevBlend`
  debug blend, "SPLITCHILD BAD BIRTH" checks) — bring-up tooling, not original behavior.

**Known bug 1 — tiles sent to y=0 where certain terrafectors are placed (port, unresolved).**
What the ORIGINAL does precisely: only materials with `useElevation != 0` touch RT0; absolute
materials replace height with `YOffset + baseElev·scale + detailElev·scale (+ posW.y·alpha if
useVertexY)`, feathered by alpha (§2.4). A tile can legitimately go to ~0 only if such a
material's height terms sum to ~0 with alpha ~1.
Evidence-backed failure modes consistent with the port symptom (hypotheses, labeled):
- (H1) A mesh-terrafector material reaches the shader with `useElevation=1,
  useAbsoluteElevation>0.5` but zero height inputs (missing/stubbed elevation textures, or a
  zeroed `TF_material` record because `rebuildAll/rebuildStructuredBuffer` didn't run or wrote
  with wrong stride/offsets) → blend replaces terrain height with ≈0 exactly under the
  terrafector footprint. The port's own dbgTagSuspicious/`TFBAKE incriminate: bicubic 949.9 →
  after bake 0.0` trace targets exactly this signature.
- (H2) RT0 blend-state mistranslation (RT0 must be One/OneMinusSrcAlpha with the SAME factors
  on the alpha channel; if alpha factors or the independent-blend override of RT0 are lost,
  relative materials with a=0 start replacing instead of adding).
- (H3) `elevationTileHashmap` miss at split (`operator[]` inserts hgt_scale=0 → bicubic writes
  flat 0 before any terrafector draws) — the port logs this as "BAD BIRTH"; distinguishable
  from H1 because the whole tile (not just the terrafector footprint) is flat.

**Known bug 2 — terrafectors render "in principle" on D3D12 but broken on Vulkan (port).**
Document of original: nothing backend-specific exists in the original — same shaders, same
states. Evidence-backed Vulkan-divergence candidates in this subsystem (hypotheses, labeled):
- (H4) The 4096-texture array descriptor pressure is exactly the F13/F18 territory; Vulkan
  needs pool sizing AND every array element written (F15) — an unset element is UB on Vulkan
  but often tolerated by D3D12 drivers.
- (H5) RT0 is R32Float with blending enabled: `VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT`
  for R32_SFLOAT is **optional** in Vulkan (universally supported on desktop NV/AMD, but
  validation/format-feature handling in the engine layer can reject or ignore the blend) —
  worth one targeted RenderDoc/validation check before any code hunt.
- (H6) 8-MRT independent blend requires the `independentBlend` feature enabled on the Vulkan
  device — silently missing feature = broken per-RT blend = both bugs at once.

## 9. Bad code / removable candidates (OPINION — do not act without developer sign-off)

- `#pragma optimize("", off)` in terrafector.cpp:3 and roadNetwork.cpp:7 — debug leftovers.
- `JLogger`/`logBlockEvent` (terrafector.h:43-93) — bespoke logging; port should map to spdlog.
  Ditto the `fprintf(terrafectorSystem::_logfile, ...)` fabric and `logTab` global.
- Dead: `roads_cubicDouble.*` (empty), `triangleBlock` (terrafector.h:118), `blendHash()`
  (returns 0), `terrafectorElement::pMesh`/`TriangleMesh`, `stampPad::renderGUI`,
  `physicsBezier` global `std::vector<physicsBezierLayer> layers;` at roads_physics.cpp:221
  (file-scope accident), `bool bCM = true;` debugger-anchor pattern everywhere,
  `allStamps_to_Bezier` (fully commented body), `updateDynamicStamp` (empty),
  `ODE_bezier::blendLayers` (empty), `bezierCache::solveStats` (empty).
- `find_insert_texture`'s hardcoded `F:\terrains\_resources\Compressonator` + `system()` calls;
  `exportBinary`'s `attrib -r` shell-outs; `path.substr(13, ...)` magic offset in texture-path
  export (roadNetwork.cpp:497, terrafector.cpp:1248) — brittle, EVO-export-only.
- Duplicated draw-stack code: `splitRenderTopdown` vs `bake_RenderTopdown` are near-copies
  differing only in FBO/tile addressing and the `bSplineAsTerrafector` gates — merge candidate
  in the port, but keep the two gate differences EXACT (offline bake always bakes roads).
- `lodTriangleMesh_LoadCombiner::addMesh`'s dead `float3 V[3]` + commented circumference debug
  block (terrafector.cpp:430-453).
- `roadNetwork::getDone` divides by zero when no roads (roadNetwork.cpp:352).
- Duplicate `replaceAll` clones (`replaceAllrm`, `replaceAlltm`, `replace`).

## 10. Open questions / uncertainties (flag, don't guess)

1. **`ODE_bezier::bezierBounding` is never populated** — neither in the extract nor in the
   PRISTINE tree (grep confirms only reads/clears; roads_AI.cpp doesn't fill it either). So
   `physicsTest/testHit/lanesFromHit` and the `.ode` export operate on an empty vector in the
   current original. Either population code was lost upstream or lives in an out-of-tree
   consumer. Developer input needed on whether roads_physics must actually *work* or just
   survive as scaffolding.
2. `terrainManager::update` early-returns (no split/merge, no bake, no road upload) when
   `terrainMode` is vegetation/glider/terrainBuilder/textureTool (terrain.cpp:1787-1794), yet
   the default member value is `vegetation` (terrain.h:548). Presumably the editor sets a
   working mode at startup — the port must confirm which mode the runtime is supposed to sit in
   for splits to happen at all.
3. `bSplineAsTerrafector` starts `false` (terrain.h:617) and is only toggled by editor
   keys (terrain.cpp:4218/4402) — i.e. in a bare runtime, roads are NOT baked at split time,
   only mesh terrafectors are (offline bake bakes them regardless). Is the intended shipped
   behavior "roads pre-baked into JP2 elevation via bake_start" with live baking as an editor
   preview? Affects what the first-milestone port needs.
4. `bake_Setup`'s second `elevationCache.get(hashParent, map)` uses `if (!get(...))
   { setTexture(map.texture) }` (terrain.cpp:3370-3372) — binding the texture only on cache
   MISS, with `map.texture` null-ish in that branch; looks inverted vs the first block at 3321.
   Works in practice because the first block always populates the cache — port carefully.
5. Samplers for the two bake shaders are never set in the extract (only spline3D gets
   `gSmpLinear`); original Falcor default samplers must be linear-wrap for `solveAlpha`'s
   world-UV sampling to look right — verify against Falcor defaults during the port.
6. `stamp_to_Bezier` writes the quad's cubicDouble with duplicated control points
   (P0=P1, P2=P3) so the bake shader (no `isQuad` branch) still evaluates it as a straight
   Casteljau — but dynamic stamps only ever draw through `render_spline` (which HAS isQuad).
   If persisted stamps ever went through the spline bake path this would matter; currently they
   go through the LOD7 mesh path. Confirm no other consumer.
7. `bezierRoadstoLOD` computes `endInside/endOutside` with `perpStart` (terrain.cpp:5036-5037)
   — likely a latent bug (should be `perpEnd`); consequences limited to bin-margin width.
   Port as-is or fix? (behavior-affecting only at bin borders).
8. LOD4 top/`LOD6_top` combiners and the `50_top` naming convention: the "50_" prefix
   semantics (sort priority?) are undocumented — ask the developer before renaming anything.
