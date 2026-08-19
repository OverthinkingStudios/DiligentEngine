# Concept Catalog — Vegetation

Sources analyzed: `port_effort_3/source_extract_3/` — `earthworks_scene/vegetationBuilder.{h,cpp}`
(4718+1987 lines), `vegetationBuilder_Trees.cpp` (1462), `ribbonBuilder.{h,cpp}`, `ecotope.{h,cpp}`,
`Sprites.{h,cpp}` + `Sprite_defines.h`; `hlsl/terrain/`: `render_vegetation_ribbons.hlsl` (1212 —
the biggest shader), `compute_vegetation_clear/lod/sortCombine.hlsl`,
`compute_clipLodAnimatePlants.hlsl`, `vegetation_defines.hlsli`, `groundcover_defines.hlsli`,
`groundcover_functions.hlsli`, `render_tile_sprite.hlsl`; `hlsl/render_sprite.hlsl` (=`sprite.hlsl`,
byte-identical duplicate). terrain.cpp is referenced for the driving interface only.
All `file:line` refs are into `port_effort_3/source_extract_3/` unless prefixed.

⚠ This subsystem is explicitly hyper-tuned; a past agent lost ~200 fps "improving" it. Sections 2/4/5
list what must survive verbatim.

---

## 1. Purpose & data flow

Vegetation = procedurally-assembled plants (leaf/flower/stem/clump/aggregate/tree "builders")
turned into **packed 32-byte ribbon vertices**, grouped into **32-vertex blocks**, drawn as
non-indexed **LineStrip instances expanded by a geometry shader** into camera-ribbon quads, with
hierarchical GPU wind animation, per-vertex baked lighting, LOD via pre-built vertex ranges +
billboard impostors, and a 128-bucket GPU depth-binning sort for front-to-back drawing.

Three operating modes of `_rootPlant` (the runtime hub, vegetationBuilder.h:1789):

| Mode | Trigger | Instance/LOD source | Draw |
|---|---|---|---|
| Single-plant preview | `displayModeSinglePlant=true` (default; after `build()`) | none — all blocks of the one built plant | `drawInstanced(VEG_BLOCK_SIZE, totalBlocksToRender)` vegetationBuilder.cpp:4248 |
| Instanced field | `buildAllLods()` or `importBinary()` set it false | `instanceData` (64k `plant_instance`) + `compute_vegetation_lod` | 128× `renderIndirect(drawArgs_vegetation, idx)` front-to-back + billboards indirect (cpp:4252-4285) |
| Terrain-driven (`terrainMode=true`) | terrain.cpp:2951 | tiles spawn `instance_PLANT` (compute_tileEcotopes.hlsl:204-251) → `compute_clipLodAnimatePlants` converts to `plant_instance` + blocks | single `renderIndirect(drawArgs_vegetation)` (cpp:4241) — see §10, wiring looks inconsistent |

### CPU build path (on demand, synchronous; `buildTime` measured cpp:3585)

`_rootPlant::build(pivotOffset)` (cpp:3509):
1. `_ribbonBuilder.setup(scale, radiusScale, offset)` from `packSettings` (h:284: `getScale()=objectSize/16384`, `getOffset()=objectOffset*objectSize`) — sets the position quantization frame.
2. `settings.clear(); generator.seed(settings.seed)` — **the whole build is deterministic from one seed**.
3. `root->build(settings, true)` — recursive builder tree (each node re-seeds; §2.4).
4. `lightBasic(extents, shadowDepth, shadowPenetationHeight)` — bakes the "egg" light cone/depth/AO into every vertex (ribbonBuilder.cpp:292-319).
5. `pack()` + `finalizeAndFillLastBlock()` — quantize to `ribbonVertex8`, pad last block with a degenerate copy (radius=0, startBit=false) (ribbonBuilder.cpp:346-377).
6. Upload: `vertexData`, identity `block_data` into BOTH `blockData` and `blockData_preSort` (cpp:3550-3558), `plantBuf[0]` (scale/offset/rootPivot/shadow params) into `plantData`, pivots into `plantpivotData` at `pivotOffset`.

`buildAllLods()` (cpp:3127) builds **3 plant variants** (`numBinaryPlants=3`, seeds `1000+pIndex`),
looping **LOD backwards from 100 down to 1** ("backwards since it fixes a bug in build()", cpp:3175)
and records per-LOD `startBlock/numBlocks` into `plant.lods[lod-1]`; exports everything as a cereal
binary `<plant>.earthworksPlant` (`exportPlant`, h:1518; cpp:3237-3288).

`importBinary()` (cpp:3397) loads that file, **remaps material indices inside the packed vertices**
(`idx=(V.b>>8)&0x3ff; V.b^=(idx<<8); V.b+=(newIdx<<8)`, cpp:3430-3435), offsets `lods[i].startVertex`
by the current block offset, and appends into the shared GPU buffers (`binVertexOffset` etc.).
`ecotopeSystem::rebuildRuntime()` (ecotope.cpp:108) is its only production caller.

### Per-frame GPU path (instanced field; `_rootPlant::render`, cpp:4025)

1. `compute_vegetation_clear` (1,1,1): zero 128 draw args (`vertexCountPerInstance=VEG_BLOCK_SIZE`), billboard args (`instanceCount=1, vertexCountPerInstance=0`), feedback counters (compute_vegetation_clear.hlsl:13-53). Always dispatched ("kest try alwasy clear", cpp:4143-4147).
2. `compute_vegetation_lod` (64k threads / 256): per instance — frustum-sphere test against 4 plane rows of `frustum` matrix (hlsl:74-77), pixel size `pix = lodBias * halfAngle_to_Pixels * PLANT.size.y * scale / distance` (hlsl:81), billboard if `pixBoost < lods[0].pixSize` (append to `instance_buffer_billboard`, count in `DrawArgs_Quads[0].vertexCountPerInstance`), else pick highest LOD with `pix > pixSize` and append that LOD's blocks into the **z-bucket region** of `blockData_preSort` (§2.6).
3. `compute_sortCombine` (`_PRE` define): copy `sort[i].current` → `DrawArgs_Plants[i].instanceCount` for the 128 buckets; `startInstanceLocation` deliberately 0 ("DOES NOTHING UNTIL Shader Model 6.8", compute_vegetation_sortCombine.hlsl:46).
4. Ribbon draws: 128 indirect draws idx 0..127 with per-draw cbuffer `drawIndex`; VS indexes `block_buffer[iId + sort[drawIndex].offset]` (render_vegetation_ribbons.hlsl:391). Front-to-back (`render_FrontToback`) or reverse.
5. Billboard draw: PointList `renderIndirect(drawArgs_billboards)` — N **vertices**, 1 instance; VS reads `instance_buffer[vId]` (hlsl:341).
6. Feedback readback: `copyResource` + `map(Read)` **every frame** (cpp:4294-4298) — a sync stall by construction (feeds the debug HUD and `plantZero` info).
7. `compute_sortCombine_POST` (no `_PRE`): recompute bucket layout for the NEXT frame: `sort[i].size = requested*2 + 2024; offset = running sum; current=requested=0` (sortCombine.hlsl:52-60). One-frame latency self-balancing allocator.

### Offline bake path (authoring; bodies kept under STRIP-REVIEW tags)

`renderGui_Baking` (cpp:2793, STRIP-REVIEW cpp:2792) is the orchestration: iterate bake slots
**backwards `for(i=10; i>=0; i--)`** "so the last PNG file is of lod-0" (cpp:2815); per slot set
`settings.includeTip/excludeDead` from the `lodBake`, rebuild at full res, derive the bake view from
root→tip (cpp:2827-2835), `calculate_extents`, then `bake()`. **Slot 0 is baked three times with
seeds 100, 101, 102** producing `bake_0_100/101/102.vegetationMaterial` — the 3 billboard texture
variations matching the 3 exported plant variants (cpp:2844-2867). Afterwards seed restored to 100,
`excludeDead=false, includeTip=true` (cpp:2878-2882). `renderGui_Lodding` (cpp:2610, STRIP-REVIEW)
holds the per-LOD rebuild: `settings.pixelSize = lod.pixelSize*0.001*0.95` ("scale down for rounding
reasons in float") and **`settings.exclusionCylinder = bakeInfo->alphaOval`** when the LOD uses a
bake (cpp:2691-2696) — the alpha oval doubles as the geometry-exclusion volume.

`bake()` (cpp:3725): 8× supersample (`bakeSuperSample`), 5-MRT FBO (albedo RGBA8, normal RGBA16F,
normal8, pbr, extra), ortho `glm::orthoRH(-W,W,H0,H1,-100,100)`, width quantized to texture-block
multiples with per-`pixHeight` tables (cpp:3755-3798), `compute_bakeFloodfill` ×128 passes to bleed
colors past alpha edges (cpp:3886-3895), `generateMips`, capture **mip 3** (`bakeMipToSave` — undoes
the 8× supersample), then shells out to Compressonator CLI for BC7 (albedo/translucency) and BC6H
(normal) with `-miplevels maxMIPY` (cpp:3940-3968), finally writes the `.vegetationMaterial` JSON.

---

## 2. Core tricks & clever mechanisms (MUST NOT be lost)

### 2.1 The 32-vertex block as the universal render/LOD/cull unit
`VEG_BLOCK_SIZE 32` (vegetation_defines.hlsli:124). All plant geometry is a flat array of packed
vertices; a **block** = 32 consecutive vertices drawn as one LineStrip instance; `block_data`
{instance_idx, vertex_offset} (defines:5-11) is the only per-draw-instance state. Consequences:
- LOD ranges are just `{startVertex(=block index), numBlocks}` per `_plant_lod` — switching LOD = emitting different block lists, zero geometry work.
- GPU culling/sorting moves 8-byte block records, never vertices.
- The GS turns each line segment into a quad; `startBit` marks ribbon starts so no quad spans two ribbons. **`startBit` semantics are inverted**: first vertex of a ribbon has bit 0, continuation vertices 1 ("badly named its teh inverse", ribbonBuilder.cpp:207); GS emits only `if (L[1].start_BIT)` (render_vegetation_ribbons.hlsl:667).
- **Block-boundary repair**: when a ribbon crosses a 32-vertex boundary, the previous vertex is duplicated as the new block's first vertex with `startBit=false`, and the `leafRoot` back-pointers of both vertices are advanced so the leaf-root chain (§2.3) stays intact (ribbonBuilder.cpp:211-231). Losing this corrupts both geometry and animation at exactly 1/32 of segment crossings.
- `finalizeAndFillLastBlock` pads the tail with copies of the last vertex, radius=0 + startBit=false ⇒ degenerate, invisible (ribbonBuilder.cpp:346-363).

### 2.2 The 32-byte packed vertex (`ribbonVertex8`, 8×uint)
Pack: `ribbonVertex::pack()` ribbonBuilder.cpp:16-79. Unpack: render_vegetation_ribbons.hlsl:111-190.

| Word | Bits (hi→lo) | Content |
|---|---|---|
| a | 31 faceCamera, 30 startBit, 29..16 x14, 15..0 y16 | position: `(int)((p+objectOffset)/objectScale)` — 14/16/14 bits; y gets 16 for tall trees |
| b | 31..18 z14, 17..8 material(10 bit), 1..0 DiamondFlag (bit0 diamond, bit1 pointSprite) | 6 bits free |
| c | 31..23 up_yaw9, 22..15 up_pitch8, 14..0 v15 | binormal (growth dir) + V texcoord (`v*255`, 16 repeats) |
| d | 31..23 left_yaw9, 22..15 left_pitch8, 14..8 u7, 7..0 radius8 | tangent + U + radius |
| e | 31..23 coneYaw9, 22..15 conePitch8, 14..8 cone7, 7..0 depth8 | baked light cone dir/width + depth-inside-plant |
| f | 31..24 AO8, 23..16 shadow8, 15..8 albedoScale8, 7..0 translucencyScale8 | per-vertex lighting/scales; albedo/translucency: `0.1 + n*0.008` |
| g | 4× pivot index (8 bit each, A..D) | wind-pivot chain; 255 = unused |
| h | 31..24 L_index(dead), 23..16 L_stiff, 15..8 L_freq, 7..0 leafRoot | leaf animation params + back-pointer |

Key encodings that must match bit-for-bit on both sides:
- **radius sqrt encoding**: pack `pow(min(1,r/radiusScale),0.5)*255` (ribbonBuilder.cpp:26); unpack `pow(radius/255,2)*PLANT.radiusScale*INSTANCE.scale` (hlsl:480). Gives precision to thin stems.
- **directions as yaw/pitch 9+8 bits**: pack scale `81.487 = 512/2π` (ribbonBuilder.cpp:30); unpack `sincos((yaw-256)*0.01227 …)` where `0.01227 = 2π/512` (hlsl:164-170). The pack loop even contains a leftover self-check reconstruction (ribbonBuilder.cpp:37-48). ⚠ lightCone packs with **81.17** (cpp:53) but is decoded with the same 0.01227 — see §10.
- position unpack macro `unpackPosition()` (hlsl:114) then `* PLANT.scale - PLANT.offset` — scale/offset live in the `plant` record, per plant, so different plants can use different quantization volumes (trees use objectSize=30, radiusScale=1.5 — vegetationBuilder_Trees.cpp:546-549).

### 2.3 Hierarchical GPU wind: 4 pivot chain + leaf bezier via back-pointer
`bezierAnimate` (hlsl:290-330):
- Word g holds up to 4 nested pivot indices (trunk→branch→twig→leaf-stem). Each <255 applies `bezierPivotSum` (hlsl:225-252): treats the vertex as a point on a quadratic bezier whose control point is pushed by wind + two sinusoids (sway 1.283×, side 0.83× frequency, per-pivot random `offset` phase), with arc-length compensation `scale=(1/len(c)+2.5)/3.5*S`. `_plant_anim_pivot.extent` is pre-divided by length (`normalize(extent)/ext_L`, cpp:1719-1721) so `t = length(rel)/S` is already 0..1 — that trick is documented in the struct (vegetation_defines.hlsli:50).
- **Leaf animation needs the leaf's root position but stores no extra data**: byte F of word h is a backwards vertex offset — `vRoot = vertex_buffer[_vId - F]` (hlsl:299). `ribbonBuilder` maintains this counter as verts are added (`S_root`/`leafRoot`, ribbonBuilder.cpp:197-231). This is why block-boundary duplication must fix up leafRoot.
- Pivot buffer is strided **256 per plant**: `pivotOffset = plant_idx * 256` (hlsl:308); `pushPivot` caps at 255 and returns 255 (=off) on overflow (ribbonBuilder.cpp:245-272); `pivotDepth < 4` enforced at build (cpp:1731-1747).
- `animateWind` (hlsl:269-282): procedural gust field (dot-based traveling wave + swirl via `AngleAxis3x3`), rotated **into the plant's frame** with `rot_xz(_wind, _rot)` so the vertex math can stay in build space; returns true (skip animation) when strength ≈ 0 because the cross products degenerate. Wind is applied BEFORE the instance yaw rotation/scale/translate (hlsl:448-455).
- Frequencies scale physically: `rootFrequency() = (1/2π)·√(g/L)`-style, `frequency·sqrt(ext_L)` at pivot creation (cpp:1726), `freq_scale = _instance.scale` in shader.

### 2.4 Deterministic seed discipline (LOD coherence)
One static `std::mt19937 _rootPlant::generator` (cpp:40). Every builder stage re-seeds:
`build_NODES` (cpp:1545), `build_leaves` (cpp:1397) with `_settings.seed`, child seeds derived
arithmetically (`oldSeed + i*10 + j + 3` cpp:1515; clump `oldSeed + (i+1)*1000` cpp:2112; tip
`seed+99` cpp:1358). Result: **rebuilding at a different pixelSize yields the same plant** — only
vertex density and bake substitution change, which is what makes the pre-built LOD ranges
cross-fade seamlessly. The controlled exception: `getLOD(pixelSize, _rnd)` jitters the LOD pick by
±20% per sub-part when `callDepth>0` (cpp:1201-1216, 921) so LOD transitions dissolve stochastically
instead of popping plant-wide. Perlin detail comes from a **pre-averaged 1024-entry lookup table**
(mean subtracted so it never drifts, cpp:2557-2572; used at cpp:1646-1657) instead of live noise.

### 2.5 Extents/bake substitution (BAKE_DIAMOND/QUAD/4/N)
`extentsCalculator` (h:421, cpp:358-409): given a view whose Y is root→tip, records per-vertex
|x|-projection into 6 height buckets → `du6` → collapsed `du4` width profile (cpp:386-402). Bakes:
- `build_2`: 2 verts spanning `bake_V.x..y` of the extent axis, width `extents.x*bakeWidth` — the GS turns it into a diamond (2 tris, `diamond&1` path hlsl:669-707 with 0.4/1.2 overshoot factors and UV 0.6 midpoint) or a quad.
- `build_4`/`build_n` (cpp:1238-1351): 4/7-vertex ribbons that follow the actual curved NODES chain (positional lerp search along node axis) with the `du4`-shaped widths — curved impostors, stem only.
- Diamond deliberately overshoots ~10% in the GS, so bake width shrinks 10% (comment cpp:1220).
- Bakes are skipped when `w <= pixelSize` (cpp:1227).

### 2.6 128-bucket GPU depth binning (the "sort")
compute_vegetation_lod.hlsl:117-151 + sortCombine + the 128-draw loop:
- Bucket = `log(distance/1)/log(1.07)` clamped 0..127 — log-spaced shells, ~7% growth per shell.
- Blocks are appended into per-bucket regions of `blockData_preSort` (3× MAX_PLANT_BLOCKS, cpp:2390) at `sort[z].offset + InterlockedAdd(sort[z].current)`.
- Region sizes come from LAST frame's `requested*2 + 2024` (sortCombine POST) — self-balancing with headroom; overflow redirects the instance to bucket 127 (lod.hlsl:131-136).
- Draw = 128 tiny indirect draws with only a cbuffer `drawIndex` changing → near-perfect front-to-back order for opaque alpha-tested foliage (early-Z wins) without any real sort. **Do not collapse into one draw**: `startInstanceLocation` cannot deliver the offset pre-SM6.8 (sortCombine.hlsl:46), which is exactly why the per-draw constant exists.

### 2.7 Baked "egg" lighting + dappled shadow + JHFAA
- `lightBasic` (ribbonBuilder.cpp:292-319): plant modeled as an egg (`egg()` squashes ±Y by `yOffset`); per vertex store normalized direction from egg center (`lightCone`), depth-to-surface / `plantDepth` (`lightDepth`), and AO from radial+height position. Evaluated per vertex in VS: `a = saturate(dot(normalize(lightCone - sunDir*PLANT.sunTilt), sunDir)); Shadow = saturate(a*sunDepth + sunDepth)` (hlsl:487-495) — sun-facing side of the canopy lights up, core stays dark, for free.
- PS modulates with `gDappledLight` noise: two chained smoothsteps around `vOut.Shadow` with `PLANT.shadowSoftness`, UV = worldPos projected onto `sunRightVector/sunUpVector` × `shadowUVScale*10` (hlsl:1082-1087) — gives sun-flecked foliage without any shadow map.
- **JHFAA**: alpha is clipped at 0.5 then `smoothstep(0.45,0.8)`; instead of alpha blending, edge pixels lerp toward the **previous frame's** color (`JHFAA_alpha`, render_Common.hlsli:215; called hlsl:1120). Blend state of the main pass is One/Zero — no ROP blending at all (cpp:2529). This is both an AA and a perf trick; it requires `gPreviousFrame` bound and front-to-back order to look right.
- PS writes `SV_DepthGreaterEqual` (`outDepth = vOut.pos.z`, hlsl:962-965) with optional `[earlydepthstencil]` under `_EARLY_Z` — the author measured both (comment block hlsl:929-948: pixel-bound; too many small triangles are "VERY BAD").
- `pbr_Vegetation` (PBR.hlsli:226-265): two-sided — `NoS>0` albedo, else translucency color `gray*pow(albedo/gray,2)*scale` (hlsl:1096-1102); env light from `gEnv` cube with z-flip `float3(1,1,-1)`.

### 2.8 Continuous stem V coordinate
`build_NODES` starts `V = V_MAX = 127` and decrements `V -= L/W * Vaspect` per grow step
(cpp:1572-1630) — V accumulates distance measured in stem-widths, corrected by the albedo texture's
aspect ratio, so bark texture density is uniform regardless of stem taper; v15 packing allows 16
wraps (`v*255` into 15 bits; `V_MAX=127` ribbonBuilder.cpp:13).

### 2.9 Grove OBJ tree import (vegetationBuilder_Trees.cpp)
Parses a Grove-exported OBJ **stream-wise** by geometric inference (no faces used): consecutive
ring cross-sections are detected via planarity (readahead1:645), radius from ring perimeter
`0.19*(l1+l2+l3)` (:935), leaves recognized as triangles whose center lies on an existing branch
node (:681-713). Then: `findSideBranches` (point-in-cylinder parent search, :852), `disableFloating`,
`countLeavesEtc` (leaf counts propagate down; branches sorted by root radius), `calcSubTwigs`
(assigns twigs to the `numBranches` biggest branches), and `buildTreeRootAndBranches` (:1285) cuts
the tree into reusable `_branch` assets (yaw-normalized so leaves point +X, stats for matching via
`_branchCollection::compare`), generating pivots per branch (`generatePivots/propagatePivots`
:813-850) and serializing `branch_*.treeBranch` + `branch.collection` JSON. Runtime `build_BRANCH`
(:331) re-emits nodes with adaptive decimation `step = stepFactor*pow(endRadius/radius,0.3)` and
grafts procedural `twigs` (a `randomVector<_plantRND>`) onto branch tips. `_cubemap` (:15-218) is a
66×66×6 distance-to-canopy cube used by `calcLight` to derive light cones for imported trees.

### 2.10 Ecotope → plant index table
`ecotopeSystem::rebuildRuntime` (ecotope.cpp:108-207): per ecotope × 16 LODs, plant densities are
normalized into a **64-slot roulette table** `plantIndex[24][16][64]` (+ `plantDensity` totals
scaled to 65535); uploaded as R32Uint typed buffers consumed by `compute_tileEcotopes` (lookup at
compute_tileEcotopes.hlsl:242: `plantIndex.Load(ect*16*64 + lod*64 + (offset>>4))`). It calls
`pVegetation->importBinary(P.path)` for every referenced plant — the ecotope file is what populates
the plant/vertex/pivot buffers in production.

---

## 3. Invariants & conventions

- `VEG_BLOCK_SIZE = 32` is shared by C++ and HLSL (vegetation_defines.hlsli:124); draw args always carry `vertexCountPerInstance = VEG_BLOCK_SIZE` (clear.hlsl:19); block==LineStrip instance.
- All matrices right-handed; a builder "node" matrix has **[0]=tangent, [1]=growth axis (bitangent/up), [2]=normal, [3]=position**; growth = `GROW(mat,len): mat[3] += mat[1]*len`; `ROLL`=rotate about Y(=growth), `PITCH` about X, `YAW` about Z (macros cpp:590-596). `ribbonBuilder::set` takes `bitangent=_node[1], tangent=_node[0]` (ribbonBuilder.cpp:183-184).
- Units: builder parameters are **mm** (converted `*0.001`); world is meters; stem lengths divided by 100/20 segment-substep conventions (cpp:643, 1614). Preview plants live at **y ≈ 1000 m** (instance positions cpp:4329-4413, camera terrain.cpp:4523).
- Quantization frame per plant: `plant.scale = objectScale = objectSize/16384`, `plant.offset = objectOffset*objectSize` with default `objectOffset=(0.5,0.1,0.5)` (h:284-291) — position must fit `[-offset, 16384*scale-offset]`; y has 16 bits (4× range).
- Pivot index 255 = "no pivot"; ≤255 pivots per plant; pivot buffer strided 256/plant; pivot chain depth ≤ 4.
- Material index is 10 bits in the vertex (max 1024 despite `sb_vegetation_Materials` having 8192 slots, terrain.cpp:1017).
- Seeds: single-plant build default 100; bake variants **100/101/102**; `buildAllLods` variants `1000+pIndex`; `semiRandomBranch::buildArray` fixed seed 1000 (cpp:510).
- Bake slots iterate **backwards** (both `renderGui_Baking` i=10..0 and `buildAllLods` lod=100..1).
- `exclusionCylinder == lodBake.alphaOval` (set cpp:2695, computed from extents in render cpp:4111-4112); `buildSetting::testExclusion` uses combined-radius 0.7 threshold (h:261-272) — note the call site currently forces `CylinderExclusion |= true` (cpp:747), i.e. exclusion is bypassed in leaf build.
- Buffer capacities (h:28-33): blocks 1,048,576 (preSort ×3), instances 65,536, billboards 65,536, plants 1,024 (GPU) but `plantBuf` CPU array is 256, pivots plants×256, verts 524,288. Uploads clamp `numV = min(65536*8, packed)` (cpp:3547).
- All vegetation indirect draws are **non-indexed 16-byte args** (`t_DrawArguments`, groundcover_defines.hlsli:42) — Diligent `DrawIndirect`, never indexed (BRINGUP F22/engine invariant).
- Topologies are load-bearing: ribbons/bake/depth = **LineStrip + GS**, billboards/textureExtract = **PointList + GS** (cpp:2336, 2496, 2508, 2970).
- Cull **None** for all veg passes (cpp:2521); blend One/Zero main, alpha-to-coverage variant (`render_alphaBlend`), and the special 8-target `blendstateBake` with `SrcAlphaSaturate` dest-alpha accumulation (cpp:2539-2548) used by bake AND `bake64kplants`.
- Sampler `sampler_Ribbons` = Clamp U / **Wrap V** (cpp:2457) — required by the V-accumulation scheme (§2.8); `sampler_Depth` is a LessEqual comparison sampler.
- Front faces CCW / RH camera / depth [0,1] (engine-wide; see conventions doc + EarthworksFX CLAUDE.md).

---

## 4. Performance-critical details

- **Author's own measurements are in the shader** (hlsl:929-948 and inline `//0.06ms…0.26ms` VS annotations at hlsl:423-482): the pass is pixel-bound; triangle count and `clip()` count are the levers. Anything that adds triangles on edges (e.g. "fixing" the diamond overshoot factors) or extra clips will silently cost ms.
- The One/Zero blend + alpha clip + front-to-back z-binning + `SV_DepthGreaterEqual` + optional `[earlydepthstencil]` together form the opacity strategy. Replacing any piece with conventional alpha blending re-introduces overdraw the design exists to avoid.
- 128 small indirect draws are intentional (§2.6). Also note fixed **4096-entry `Texture2D T[]` array** (hlsl:23-28): under the old compat layer this exhausted descriptor pools (BRINGUP F13/F18); a native port should make it a bindless/mutable array, not per-draw dynamic.
- `compute_vegetation_lod` writes blocks with a per-instance `for(i<NUMBLOCKS)` loop of global-memory stores (hlsl:144-148) — fine because instances are 256/group and most have few blocks; don't "vectorize" into per-block threads without keeping the bucket-append atomicity.
- Billboard cutover at `lods[0].pixSize` with view-from-above boost `pixBoost = pix*(1+smoothstep(0.97,1,viewDir.y))` (lod.hlsl:87) — commented as needing care; billboards look wrong top-down.
- Per-vertex work deliberately front-loaded: light cone, atmosphere (`vs_atmosphere` in VS, hlsl:471), sun color, fog distance are all VS/vertex-rate; PS reads at most 3 textures + dappled.
- CPU build files are compiled with **`#pragma optimize("", off)`** (vegetationBuilder.cpp:13, ribbonBuilder.cpp:4, Trees.cpp:5, ecotope.cpp:10 — the latter marked "CRITICAL FOR SOME REAASON JSON error"). Build-time only, but treat as load-bearing until the underlying (likely cereal/UB) issue is understood; conversely, re-enabling optimization is a free CPU-build speedup IF that issue is found.
- Feedback readback (cpp:4294-4298) is an every-frame GPU→CPU sync point. Keep it out of the way (async readback / N-frame delay) in the port, but keep the data — the debug HUD and lod tuning depend on it.
- Degenerate padding verts (radius=0) rely on the GS emitting zero-area quads — cheap, but only because radius scales the tangent offset; don't cull them CPU-side, block alignment is the point.
- `blockData_preSort` is 3× block capacity because bucket regions carry 2×requested + 2024 slack each (cpp:2390).

---

## 5. GPU resources & shader interface

### Buffers (created in `_rootPlant::onLoad`, cpp:2385-2399 unless noted)

| Buffer | Stride/struct | Count | Flags | Producer → Consumer |
|---|---|---|---|---|
| `plantData` | `plant` (see below) | 1024 | SRV | CPU setBlob → lod CS, clipLod CS, ribbon VS, tile_sprite GS/PS |
| `plantpivotData` | `_plant_anim_pivot` 48 B | 1024×256 | SRV | CPU → ribbon VS (`plant_pivot_buffer`) |
| `instanceData` | `plant_instance` 32 B | 65536 | SRV(+UAV via clipLod `instance_out`) | CPU `builInstanceBuffer` / clipLod CS → lod CS, ribbon VS |
| `instanceData_Billboards` | `plant_instance` | 65536 | SRV+UAV+IndirectArg | lod CS append → billboard VS |
| `blockData_preSort` | `block_data` 8 B | 3×1,048,576 | SRV+UAV+IndirectArg | lod CS (bucketed) → main ribbon VS (`block_buffer`!) |
| `blockData` | `block_data` | 1,048,576 | SRV+UAV+IndirectArg | CPU build / clipLod CS → RGB/DEPTH/BB/bake shaders |
| `vertexData` | `ribbonVertex8` 32 B | 524,288 | SRV | CPU pack/import → all ribbon shaders |
| `drawArgs_vegetation` | `t_DrawArguments` 16 B | numRenderViews×128 | UAV+IndirectArg | clear/lod/sortCombine CS → 128 indirect draws |
| `drawArgs_billboards` | `t_DrawArguments` | numRenderViews | UAV+IndirectArg | clear/lod CS → billboard indirect draw |
| `buffer_gpuSort` | `veg_sort` uint4 | 1024 (128 used) | SRV/UAV | lod/sortCombine CS ↔ ribbon VS (`sort`) |
| `buffer_feedback` (+`_read` staging) | `vegetation_feedback` | 1 | UAV / CPU-read | everything (globallycoherent) → CPU map each frame |
| `sb_vegetation_Materials` | `sprite_material` 64 B | 8192 (terrain.cpp:1017) | SRV | `rebuildStructuredBuffer` → all veg PS |
| ecotope `piBuffer`/`pdBuffer` | R32Uint typed | 12×16×65 (⚠ writes 24×16×64 — see §10) | SRV | ecotope.cpp:196-206 → compute_tileEcotopes |

### Struct layouts (shared C++/HLSL via `vegetation_defines.hlsli` — padding is load-bearing)

- `plant` (defines:59-94): 4 float4-groups of scalars (`size,scale,radiusScale`, `offset+unused_01`, lighting 4, shadows/flutter 4), `numLods, billboardMaterialIndex, padd1, padd2`, then **fixed `_plant_lod lods[16]`** (16 B each) and a trailing `_plant_anim_pivot rootPivot` (48 B). The `padd1/padd2` and `unused_01` keep the 16-byte alignment of the lods array — a past port agent deleted "unused" padding and broke rendering; do not touch.
- `plant_instance` (defines:17-26): `float3 position; float scale; float rotation; uint plant_idx; uint padd1; uint padd2` — 32 B; comment says deliberately uncompressed until leaf instancing lands.
- `_plant_anim_pivot` (defines:45-57): `float3 root; float frequency; float3 extent(=dir/len); float stiffness; int offset; float shift; int padd1; int padd2` — 48 B; `shift` is "getting misused" as the leaf flutter strength byte's decode target.
- `veg_sort` (defines:152-159): `{current, size, offset, requested}` = uint4.
- `vegetation_feedback` (defines:128-149): scalar block + `numLod[32] + numPlantsType[32]` + counters + `numBlock_Z[128]`. Layout mismatches here were BRINGUP F9 — port byte-exact.
- `sprite_material` (groundcover_defines.hlsli:358-373): 4 texture ints, translucency int+2 floats, `float3 albedoScale[2]`, `float roughness[2]` — C++ default-initializers, HLSL sees plain layout.

### cbuffers

- `gConstantBuffer` (render_vegetation_ribbons.hlsl:31-55): `view, viewproj (float4x4); eyePos+padd1; camRight; camUp; time,bake_radius_alpha,bake_height_alpha,bake_AoToAlbedo; windDir+windStrength; shadowViewProj; drawIndex,toneMap; bake_AlphaOval(float2)`. `padd1` after eyePos required.
- `LightsCB` (render_Common.hlsli:35-60, register b2): sunDirection+numLights / sunColour+padd / sunRightVector+padd2 / sunUpVector+padd3 / screenSize+fog_far params / fog_near params, then `gpu_LIGHT Lights[128]`. The CPU mirror `shaderLightBuffer` (vegetationBuilder.h:91-114) is the same layout **without** the lights array — fields are set individually via ShaderVar, not blob-copied.
- `compute_vegetation_lod` cbuffer (lod.hlsl:24-41): `view, frustum (4x4); eyePos+padd; firstPlant,lastPlant,firstLod,lastLod; lodBias, halfAngle_to_Pixels` — debug range filters are functional (default firstPlant=0 passes all).
- `compute_clipLodAnimatePlants` cbuffer (hlsl:23-29): `view, clip, halfAngle_to_Pixels`.

### Textures & samplers (ribbon pass)

`textures.T[4096]` ParameterBlock (albedo/alpha/normal/translucency by index from `sprite_material`),
`gDappledLight` (t3), `highResShadow` (t4, 8192² D24S8 from `shadowFbo`, SAMPLE_MODE only),
`gAtmosphereInscatter/Outscatter` (3D), `SunInAtmosphere`, `gEnv` cube, `gPreviousFrame`,
`terrainShadow` (set via `updateShaderConstants`, cpp:4003-4004). Samplers: `gSmpLinear`
(ClampU/WrapV aniso1), `gSmpLinearClamp` (aniso 4), `gSamplerDepth` (cmp LessEqual).

### Shader permutations of `render_vegetation_ribbons.hlsl` (one file, 6 PSOs; cpp:2315-2516)

| Define | Object | Topology | Purpose |
|---|---|---|---|
| (none) [+`_DEBUG_PIXELS`/`_PIXEL_COUNT`/`_Z_ONLY`/`_EARLY_Z`] | `vegetationShader` | LineStrip+GS | main render |
| `_RGB_SAMPLE` | `vegetationShader_RGB_SAMPLE` | LineStrip+GS | BRDF sampling research (SAMPLE_MODE) |
| `_DEPTH` | `vegetationShader_DEPTH` | LineStrip+GS | shadow-map bake (alpha clip 0.5, returns 1) |
| `_BILLBOARD` | `billboardShader` | PointList+GS | impostors (per-vertex instance fetch) |
| `_BAKE` | `bakeShader` | LineStrip+GS | 5-MRT billboard bake; stochastic alpha `clip(alpha - rnd)` hlsl:789-790 |
| `_GOURAUD_SHADING` | (none — dead, see §9) | — | vertex-lit experiment |

---

## 6. Dependencies

Consumes:
- `terrafector.h`: `terrafectorEditorMaterial::rootFolder` (asset root for ALL paths), `materialCache::getRelative`, `terrafectorSystem::_logfile`.
- Atmosphere/common shaders: `sunLight()`, `shadow()` (terrain CPU shadow texture), `vs_atmosphere`, `JHFAA_alpha`, `gPreviousFrame`, `LightsCB` — fed by `terrainManager::updateShaderConstants` → `plants_Root.updateShaderConstants(prevFrame, terrainShadowTexture, lightBuffer)` (terrain.cpp:1778).
- Terrain GPU tile pipeline: `compute_tileEcotopes` spawns packed `instance_PLANT` per tile using ecotope roulette buffers; `compute_tileClear` zeroes `plants_Root.drawArgs_vegetation` (terrain.cpp:1050); `compute_clipLodAnimatePlants` (dispatched indirect per plant-lookup block, terrain.cpp:2892-2896) unpacks tile plants → `plants_Root.instanceData/blockData/drawArgs`.
- `PerlinNoise.hpp` (header-only), cereal (JSON for authoring files, binary for `.earthworksPlant`), Compressonator CLI + `system()` for DDS compression, `imgui.h` (header pulls it in even post-strip for the R_FLOAT macros).

Provides:
- `_plantMaterial::static_materials_veg` (global material/texture cache) — also bound by terrain's `ribbonShader` (roads! terrain.cpp:704) and `terrainSpiteShader`/`render_tile_sprite` (billboards over terrain: reads `plant_buffer` + `materials` + the same 4096 texture array, render_tile_sprite.hlsl:24-29).
- `plantData/vertexData/plantpivotData/blockData` for the terrain-driven path.
- `ecotopeSystem` (placement definitions consumed by the tile pipeline; holds `static _rootPlant* pVegetation` set at terrain.cpp:524).
- `Sprites.{h,cpp}` + `render_sprite.hlsl`: a self-contained legacy quad-sprite/marker system (10×10 static blocks with CPU AABB frustum culling via `camera->isObjectCulled`, 101-slot indirect args table, triple-buffered dynamic markers/lines) — used for debug markers and old tree sprites; independent of the ribbon system.

Camera inputs: `halfAngle_to_Pixels = resolution * length(proj[1]) / 2` (terrain.cpp:2867-2868) — the pixel-size metric everything LODs by; frustum = plane-rows matrix (`frustumMatrix`), extraction example in `bake64kplants` cpp:3621-3639 (kept precisely for this reference, per its STRIP-REVIEW tag).

## 7. Falcor API surface actually used

- `Buffer::createStructured(stride, count [, BindFlags, CpuAccess::Read])`, `Buffer::createTyped<R32Uint>`, `buf->setBlob(ptr, offsetBytes, sizeBytes)`, `map(Read)/unmap`, `getUAVCounter()->setBlob` (Sprites only).
- `Texture::createFromFile(path, genMips, sRGB)` (+ `setSourcePath/setName/getWidth/getHeight`), `Texture::create2D(..., UAV|SRV)`, `captureToFile(mip, slice, path, format, exportAlpha)`, `generateMips(ctx)`, `RenderContext::updateTextureData`, `clearTexture`, `clearFbo/clearRtv`, `copyResource`, `flush(true)`.
- `Fbo::create2D(w, h, desc, arraySize, mips)` with multi-RT descs (bake: 5 MRT + D24S8; shadow: 8192² D24S8 + R8; rgb: 1024×256).
- `Sampler::create` (addressing per-axis, aniso, comparison mode), `RasterizerState` (CullMode::None), `BlendState` (per-RT params, alpha-to-coverage, independent blend, `SrcAlphaSaturate`), `DepthStencilState` (Sprites: depth off).
- Custom wrappers `pixelShader`/`computeShader`: `.add/remove(define)`, `.load(path, vs, ps, topology, gs)` — **VS+GS+PS from one file with LineStrip/PointList topologies**, `.Vars()->setBuffer/setTexture/setSampler`, `["cbuffer"]["member"] =`, `getParameterBlock("textures")->findMember("T")` then `var[i] = tex` (arrayed binding — BRINGUP F15), `.State()->setFbo/setViewport/setRasterizerState/setBlendState`, `.drawInstanced(ctx, vertsPerInstance, instances)`, `.renderIndirect(ctx, argBuf [, blend, argIndex, count])`, `.dispatch(ctx, x, y)`, `.dispatchIndirect(ctx, argBuf, offset)`.
- `FALCOR_PROFILE(name)` + `Profiler::instance().getEvent(path)->getGpuTimeAverage()` (drives `gputime/gputimeBB`), `gpFramework->getFrameRate().getAverageFrameTime()` (animation `time` accumulator, cpp:4184-4185).
- GS-stage shaders (`gsMain`, maxvertexcount 4) — Diligent supports GS; the port decision is whether to keep GS or move expansion to compute/vertex-pulling. Everything in §2.1 assumes per-segment expansion semantics.
- `openFileDialog/saveFileDialog` (ecotope load/save, tree OBJ import), `reportError`, glm throughout (`rmcv::mat4` only at the shader-upload boundary, transposed copy loops e.g. cpp:3827-3833).

## 8. Port drift notes (vs `EarthworksFX/src/core` + BRINGUP findings)

The extract is **newer** than the previous port's June import. Verified deltas:
- **pointSprite feature is extract-only**: `ribbonVertex.pointSprite`, 2-bit DiamondFlag (`&0x3` vs port `&0x1`), the `diamond & 0x2` camera-facing quad GS path (hlsl:630-666), `camRight/camUp` cbuffer members, `_leafBuilder.pointSprite` serialized (version 102). Port shader lacks all of it.
- `toneMap` cbuffer flag (bake tonemapping, hlsl:1110-1114) — extract only.
- sortCombine fix: port still writes `DrawArgs_Plants[i].startInstanceLocation = sort[i].offset` (does nothing pre-SM6.8); extract sets 0 and documents why.
- Bake alpha tweaks: `smoothstep(0.90,1.1)` vs port `0.8,1.1`; the `bake_height_alpha` line commented out in extract (hlsl:412); `_BAKE` worldPos scaling block commented out (hlsl:441-446).
- Texture array binding style: extract uses `ParameterBlock<ribbonTextures> textures` / `textures.T[i]`; port flattened to `Texture2D textures_T[4096]` (their F13/F15/F18 workaround territory). A native Diligent port should choose its own bindless strategy — but the F13/F18 lesson (descriptor pool sizing for 4096-entry arrays) still applies.
- Port `vegetationBuilder.cpp` is 7063 lines vs extract 4718 — the difference is the editor GUI we stripped (plus their compat edits); port `ecotope.cpp` 558 vs 230 (GUI). Port tree also contains stale shader backups (`render_vegetation_ribbons - Copy.hlsl` etc.) — ignore.
- Relevant BRINGUP F-findings encoding real semantics: **F9** (feedback struct layout must match byte-exact), **F11** (`Texture::create2D` arraySize), **F13/F18** (4096-texture descriptor budgets, Vulkan pool vs D3D12 fixed heap — first crash was literally the first `_rootPlant::render` draw), **F15** (arrayed texture binding element-wise), **F16** (the shared quad index buffer must not be USAGE_DYNAMIC; veg draws are non-indexed but the wrapper owned an IB), **F22** (non-indexed indirect draws took the indexed path — veg draws listed among victims), **F20** (RH camera; `vertex.bitangent = _node[1] // right handed matrix : up` depends on it).
- BRINGUP "hardcoded fake" warning: some debug-HUD metrics were misread from `vegRibbonOKPixels/ClippedPixels` (notes line ~208) — those feedback fields exist in `GC_feedback`, not `vegetation_feedback`.

## 9. Bad code / removable candidates (OPINION — do not act without developer sign-off)

- Dead in-shader paths: `_GOURAUD_SHADING` (no C++ ever defines it; `vegetationShader_GOURAUD` is commented out), `_DEBUG_PIVOTS`, the commented human/helicopter/vehicle wind experiments (hlsl:1141-1212), `extractFlags()` unused (hlsl:185-190).
- Research/scaffolding in `_rootPlant`: `SAMPLE_MODE`/`buildOneMap`/`buildBDRF`/`bakeShadowMap`+`shadowFbo`(8192²!)+`rgbFbo`+`RGB_MAP`+`compute_sampleRGBtoPixel*` write hardcoded `e:/test_RGB/*.png` (cpp:4616-4628, 4708-4716) — an offline BRDF experiment; the 8192² shadow FBO is allocated unconditionally at onLoad (256 MB). Candidate to gate behind a flag in the port.
- `binaryPlantOnDisk` (fully commented out), `recentFiles` (empty), `_rootPlant_Runtime` (empty), LaTeX residue comment cpp:3297-3308, duplicated `replaceAllVEG/replaceAllveg`.
- `sprite.hlsl` ≡ `render_sprite.hlsl` (byte-identical); Sprites.cpp is a legacy debug/marker system with hardcoded 2000 m world assumptions — port only if the debug markers are wanted.
- Debugger-bait no-op branches (`bool bCM = true;`) scattered through pack/set (ribbonBuilder.cpp:37-48,158-170, Trees.cpp:171) — removable, but the reconstruction check at ribbonBuilder.cpp:37-48 documents the encoding and is worth keeping as a comment.
- `#pragma optimize("", off)` on four files — see §4; investigate root cause (ecotope.cpp:9 claims a JSON error depends on it) before removing.
- `lodBake.forceDiamond`, `leafIndex`/`ossilation_power` ("shift") deprecated-but-serialized — keep for file compatibility; the cereal class versions (`CEREAL_CLASS_VERSION`, e.g. `_leafBuilder` 102, `lodBake` 102, `_stemBuilder` 103) are the authoring-file format contract.
- `bake64kplants` STRIP-REVIEW: only value is the multi-view indirect-render + frustum-extraction reference; the jpg output has no consumer anymore.

## 10. Open questions / uncertainties (flag, don't guess)

1. **Material extraction mask**: shader unpacks `material_IDX = (v.b >> 8) & 0x2ff` (hlsl:422 — note `0x2ff`, not `0x3ff`) while pack and CPU remap use `0x3ff`. Materials with bit 8 set (256–511) would alias. Present in port AND extract. Latent bug or "never >255 materials in practice"?
2. **lightCone pack constant 81.17** vs 81.487 for tangents (ribbonBuilder.cpp:53 vs 30), both decoded with 2π/512. ~0.4% angular skew — intentional bias or typo? Harmless either way, but port byte-exact.
3. **Terrain-driven mode wiring** looks mid-integration: (a) `render(terrainMode=true)` dispatches `compute_clearBuffers` (zeroing `drawArgs_vegetation`) AFTER `compute_clipLodAnimatePlants` filled them earlier in the same frame (terrain.cpp:2880 → cpp:4146 → draw cpp:4241); (b) clipLod writes `blockData` (terrain.cpp:1041) but the main `vegetationShader` binds `block_buffer = blockData_preSort` (cpp:2341); (c) the VS adds `sort[drawIndex].offset` which is stale/foreign in that path. Either the terrain path was known-broken/WIP in the original, or there is frame-ordering subtlety I can't see from static reading. **Ask the developer what the last known-good terrain-vegetation state was.**
4. `sort[i].size = requested*2 + 2024` — comment says 1024 (sortCombine.hlsl:52). Typo that became behavior; keep 2024 unless measured.
5. `compute_vegetation_lod` gate `if (firstPlant == 0 || idx == 0)` (lod.hlsl:71) — the commented-out range test above it suggests the debug filter degenerated; as shipped, `firstPlant != 0` renders only instance 0.
6. Ecotope buffer size mismatch: `piBuffer` created for `12*16*65` uints but blob-written with `24*16*64` (ecotope.cpp:198-200) — writes 1.97× the created size (setBlob presumably clamps or the create args are bytes-vs-elements confused; `createTyped` count semantics here differ from `createStructured`). Verify against Falcor semantics before porting literally.
7. `importBinary` pivot offset accounting: `binPivotOffset += plants*256*sizeof(pivot)` but pivots written at `binPivotOffset` from a vector sized `256*numBinaryPlants` — author comment "LIKELE veryy wrone" (cpp:3456-3461). Works because every export carries exactly 3 plants? Needs a test.
8. `plantData->setBlob(plantBuf.data(), 0, 8*sizeof(plant))` in `buildAllLods` (cpp:3219) uploads 8 records for 3 plants — harmless slack or intent for 8 variants?
9. `_flowerBuilder::build` uses `!(pivot_leaf)` (a constant enum ⇒ always false) where `_leafBuilder` uses `!(pivotType == pivot_leaf)` (cpp:961,974) — copy-paste bug affecting flower-stem leafRoot chains?
10. Billboard VS shadow test `dot(output.pos.xyz, sunRightVector) in (5,8)` sets Shadow=1 then is immediately overwritten `Shadow = 0` (hlsl:358-377) — leftover experiment; billboards effectively render unshadowed (`SS` computed then discarded).
11. `compute_clipLodAnimatePlants` writes `drawArgs_Plants[0]` only ("?? WRONG use depth slices", hlsl:107-108) — z-binning was never hooked into the terrain path.
12. `numBinaryPlants = 3` hardcoded (cpp:3142) with "FIXME VERY VERY BAD _ always loads just one" at export (cpp:3241) — variant count is a known construction site.
