# source_extract_3 — Inventory & Analysis

Fresh extract taken 2026-08-02 from `C:\dev\git\os\Earthworks_4-F5_2\Source\Samples\Earthworks_4\`
(read-only original Falcor project) into `port_effort_3/source_extract_3/`, preserving directory
structure (`earthworks_scene/`, `hlsl/`, `hlsl/atmosphere/`, `hlsl/terrain/`).

**Copied: 115 files, 100,921 lines. All copies verified byte-identical via `diff -rq`.**

## Excluded from the copy

| Excluded | Reason |
|---|---|
| `cereal/` (whole tree incl. embedded rapidjson/rapidxml) | vendored third-party |
| `nlohmann/` | vendored third-party (note: `earthworks_scene/json.hpp` is the same lib single-header, kept per instruction since it sits among core files) |
| `rapidjson/` | vendored third-party |
| `libharu/`, `harupdf/` | vendored third-party (PDF lib) |
| `hpdf.dll`, `hpdf.lib` | binaries |
| `about.dds` | binary asset |
| `log.txt` | runtime junk |
| `hlsl/terrain/render_vegetation_ribbons - Copy.hlsl` (870 ln) | editor backup junk |
| `hlsl/terrain/render_vegetation_ribbons - Backup after new animations.hlsl` (1076 ln) | editor backup junk |

No `imgui.ini` exists in the original tree (extract_2 had one). No subdirectories exist beyond
`earthworks_scene/`, `hlsl/` and the vendored dirs — nothing unexpected was found.

## a+b. File inventory with classification draft

Classification is a **proposal for developer review** — nothing has been deleted.
`PORT*` = port, but needs a partial-strip pass (editor GUI / glider / PDF code woven in).

### Top level

| File | Lines | Purpose | Class |
|---|---|---|---|
| `CMakeLists.txt` | 151 | Falcor sample build file; useful as authoritative file list | UNCERTAIN (reference) |
| `Earthworks_4.cpp` | 728 | App shell: `IRenderer` host, main loop, GUI dispatch, camera hookup | UNCERTAIN (app shell exists port-side; reference for wiring) |
| `Earthworks_4.h` | 124 | App shell header | UNCERTAIN |
| `barrier.hpp` | 64 | Thread barrier utility (used by glider thread + terrainGenerator) | UNCERTAIN (keep if terrainGenerator survives) |
| `cfd.cpp` | 2106 | CFD wind/thermal simulation | STRIP |
| `cfd.h` | 345 | CFD header | STRIP |
| `computeShader.cpp` | 24 | Thin Falcor compute-shader wrapper (load/dispatch/defines) | PORT (concept/API-surface reference) |
| `computeShader.h` | 25 | ditto | PORT |
| `pixelShader.cpp` | 62 | Thin Falcor graphics-shader wrapper (VS/PS/GS, indirect draws) | PORT (concept/API-surface reference) |
| `pixelShader.h` | 31 | ditto | PORT |
| `glider.cpp` | 2071 | Paraglider builder (12 ImGui calls) | STRIP |
| `glider.h` | 1432 | Paraglider types | STRIP |
| `glider_runtime.cpp` | 9210 | Paraglider runtime + GUI (472 ImGui calls) | STRIP |
| `latexgen.h` | 1443 | LaTeX report generation (used by vegetationBuilder PDF export) | STRIP |
| `earthworks4_presets.xml` | 6 | Presets config data | UNCERTAIN (data, trivial) |
| `lastFile.xml` | 14 | Last-opened-file config data | UNCERTAIN (data, trivial) |

### earthworks_scene/

| File | Lines | Purpose | Class |
|---|---|---|---|
| `PerlinNoise.hpp` | 658 | siv::PerlinNoise single-header (used by vegetationBuilder) | PORT (utility dep) |
| `Sprite_defines.h` | 27 | Sprite GPU struct defines | PORT |
| `Sprites.cpp` / `.h` | 483 / 100 | Sprite/billboard system (blocks, indirect args, frustum culling) | PORT |
| `atmosphere.cpp` / `.h` | 280 / 161 | Sun/sky/fog atmosphere params + compute passes | PORT |
| `cascadeShadowMaps.cpp` / `.h` | 109 / 39 | Sun shadow map render targets (the shadow tech — small!) | PORT |
| `earthworksScene.cpp` / `.h` | 5 / 16 | Scene aggregate — an empty stub (header includes terrain/atmosphere/shadows, empty class body) | PORT (trivial scaffold) |
| `ecotope.cpp` / `.h` | 559 / 229 | Ecotope definitions for vegetation placement, cereal serialization (76 ImGui) | PORT* (minor GUI strip) |
| `imGuiHelper.h` | 203 | GUI macro helpers (BEGIN/changed patterns, fonts) | UNCERTAIN (editor-only, but included by GUI code in core files) |
| `json.hpp` | 24765 | Vendored nlohmann/json single header | UNCERTAIN (third-party — replace with proper dependency) |
| `lru_cache.h` | 48 | Generic LRU cache (tile/texture caching) | PORT |
| `ribbonBuilder.cpp` / `.h` | 416 / 105 | Vegetation ribbon vertex packing/building | PORT |
| `roadNetwork.cpp` / `.h` | 2587 / 179 | Road network: bezier graph, intersections, GUI editing (222 ImGui) | PORT* (editor GUI woven in) |
| `roads_AI.cpp` / `.h` | 543 / 92 | AI racing-line/driving analysis over road network | UNCERTAIN |
| `roads_Intersection.cpp` / `.h` | 294 / 71 | Road intersection geometry | PORT |
| `roads_bezier.cpp` / `.h` | 320 / 235 | Cubic bezier math for roads | PORT |
| `roads_cubicDouble.cpp` / `.h` | 3 / 4 | Near-empty stubs | PORT (trivial) |
| `roads_materials.cpp` / `.h` | 436 / 118 | Road material definitions + GUI (46 ImGui) | PORT* |
| `roads_physics.cpp` / `.h` | 690 / 231 | Road physics (bezier cache, surface queries — for vehicles/AI) | UNCERTAIN |
| `roads_road.cpp` / `.h` | 1509 / 330 | Road representation: lanes, profiles, ribbon generation | PORT |
| `terrafector.cpp` / `.h` | 2326 / 549 | Terrafector system: terrain-affecting stamps, materials (225 ImGui) | PORT* |
| `terrain.cpp` / `.h` | 8735 / 952 | Core terrain: quadtree, tile streaming/baking, render dispatch. 331 cfd/glider references, 400 ImGui calls; `terrain.h` includes `glider.h`, `cfd.h`, `terrainGenerator.h` | PORT* (heavy partial-strip: glider/cfd/GUI) |
| `terrainGenerator.cpp` / `.h` | 1601 / 319 | Terrain raw-data generation pipeline (cereal, async, 155 ImGui). Included by `terrain.h` | UNCERTAIN |
| `terrain_Builder_GIS.cpp` / `.h` | 8762 / 929 | **Dead code**: near-copy fork of terrain.cpp/.h (only 881 diff lines vs terrain.cpp); NOT in CMakeLists, included by nothing | STRIP (dead; possibly mine the 881-line delta first) |
| `vegetationBuilder.cpp` / `.h` | 8309 / 2124 | Vegetation builder: plant assembly, GPU buffers, LOD/billboards. 881 ImGui calls, includes `latexgen.h` + `hpdf.h` (PDF plant-catalog export ~L6359+) | PORT* (strip PDF/latex + GUI) |
| `vegetationBuilder_Trees.cpp` | 1647 | Tree-specific vegetation building (39 ImGui) | PORT* |
| `volumeFogPhaseFunctions.cpp` | 224 | Precomputed fog phase functions | PORT |

### hlsl/

| File | Lines | Purpose | Class |
|---|---|---|---|
| `PBR.hlsli` | 265 | PBR lighting functions | PORT |
| `material.hlsli` | 186 | Material sampling | PORT |
| `render_Common.hlsli` | 244 | Common render declarations (view data etc.) | PORT |
| `compute_tonemapper.hlsl` | 50 | Tonemapping | PORT |
| `render_sprite.hlsl` / `sprite.hlsl` | 221 / 222 | Sprite rendering / sprite common | PORT |
| `atmosphere/compute_fogCloudAtmosphereCommon.hlsli` | 18 | Fog/cloud common | PORT |
| `atmosphere/compute_sunlightInAtmosphere.hlsl` | 78 | Sun transmittance | PORT |
| `atmosphere/compute_volumeFog.hlsli` | 386 | Volume fog core | PORT |
| `atmosphere/compute_volumeFogAtmosphericScatter.hlsl` | 169 | Atmospheric scatter | PORT |
| `atmosphere/compute_volumeFogLights.hlsl` | 78 | Fog light injection | PORT |
| `atmosphere/compute_volumeFogSmokeAndDust.hlsl` | 106 | Smoke/dust in fog | PORT |
| `atmosphere/noise.inc` | 697 | Noise functions | PORT |
| `terrain/compute_bakeFloodfill.hlsl` | 116 | Tile-bake floodfill | PORT |
| `terrain/compute_bc6h.hlsl` + `_functions.hlsli` | 143 + 480 | BC6H compression of baked tiles | PORT |
| `terrain/compute_clipLodAnimatePlants.hlsl` | 142 | Vegetation clip/LOD/animation | PORT |
| `terrain/compute_sampleRGBtoPixel.hlsl` | 72 | RGB sampling helper (used only by vegetationBuilder tooling paths) | UNCERTAIN |
| `terrain/compute_terrain_under_mouse.hlsl` | 132 | Terrain picking under mouse (editor + possibly runtime queries) | UNCERTAIN |
| `terrain/compute_tileBicubic.hlsl` | 172 | Tile bicubic upsample | PORT |
| `terrain/compute_tileBuildLookup.hlsl` | 193 | Tile lookup build (indirect draw gen) | PORT |
| `terrain/compute_tileClear.hlsl` | 115 | Tile clear | PORT |
| `terrain/compute_tileDelaunay.hlsl` | 101 | Tile delaunay pass | PORT |
| `terrain/compute_tileEcotopes.hlsl` | 265 | Ecotope evaluation per tile | PORT |
| `terrain/compute_tileGenerate.hlsl` | 58 | Tile generation | PORT |
| `terrain/compute_tileJumpFlood.hlsl` | 40 | Jump flood (distance fields) | PORT |
| `terrain/compute_tileNormals.hlsl` | 47 | Tile normals | PORT |
| `terrain/compute_tilePassthrough.hlsl` | 114 | Tile passthrough copy | PORT |
| `terrain/compute_tileSplitMerge.hlsl` | 36 | Quadtree split/merge | PORT |
| `terrain/compute_tileVertices.hlsl` | 219 | Tile vertex generation | PORT |
| `terrain/compute_vegetation_clear.hlsl` | 53 | Vegetation buffer clear | PORT |
| `terrain/compute_vegetation_lod.hlsl` | 157 | Vegetation LOD selection | PORT |
| `terrain/compute_vegetation_sortCombine.hlsl` | 66 | Vegetation sort/combine | PORT |
| `terrain/extractTextures.hlsl` | 165 | Texture extraction (vegetationBuilder tooling) | UNCERTAIN |
| `terrain/gpuLights_defines.hlsli` + `_functions.hlsli` | 22 + 104 | GPU lights | PORT |
| `terrain/groundcover_defines.hlsli` + `_functions.hlsli` | 464 + 99 | Groundcover shared defines/functions | PORT |
| `terrain/materials.hlsli` | 267 | Terrain material evaluation | PORT |
| `terrain/render_Buildings_Far.hlsl` | 119 | Far building rendering | PORT |
| `terrain/render_GliderWing.hlsl` | 156 | Glider wing rendering | STRIP |
| `terrain/render_Tiles.hlsl` | 437 | Main terrain tile rendering | PORT |
| `terrain/render_cfdSlice.hlsl` | 115 | CFD debug slice visualization | STRIP |
| `terrain/render_meshTerrafector.hlsl` | 185 | Mesh terrafector stamping | PORT |
| `terrain/render_ribbons.hlsl` | 692 | Ribbon rendering (used by terrain.cpp) | PORT |
| `terrain/render_spline.hlsl` | 299 | Road spline overlay (editor visualization, used by terrain.cpp) | UNCERTAIN |
| `terrain/render_splineTerrafector.hlsl` | 245 | Spline terrafector stamping | PORT |
| `terrain/render_thermalRibbons.hlsl` | 120 | Thermal (glider) ribbon visualization | STRIP |
| `terrain/render_tile_sprite.hlsl` | 232 | Tile sprite rendering | PORT |
| `terrain/render_triangles.hlsl` | 182 | Triangle debug/overlay rendering | PORT |
| `terrain/render_vegetation_ribbons.hlsl` | 1212 | Main vegetation ribbon rendering (largest shader) | PORT |
| `terrain/terrainDefines.hlsli` / `terrainFunctions.hlsli` | 36 / 13 | Terrain shared defines/functions | PORT |
| `terrain/vegetation_defines.hlsli` | 158 | Vegetation shared defines | PORT |

**Tallies: PORT 86 files (of which 8 flagged PORT* partial-strip), STRIP 11 files (~26.4k lines incl. terrain_Builder_GIS 9.7k, glider 12.7k, cfd 2.5k, latexgen 1.4k, 3 shaders), UNCERTAIN 18 files.**
(json.hpp alone is 24.8k of the 100.9k total lines.)

## c. Staleness: extract_3 vs extract_2 (old flat extract)

Matched by basename (extract_2 is flat; hlsl subtree matched by relative path).

### Only in extract_3 (missing from extract_2 — confirms task.md §3 staleness)

`earthworks_scene/`: `cascadeShadowMaps.cpp/.h`, `earthworksScene.cpp/.h`, `terrainGenerator.cpp/.h`,
`terrain_Builder_GIS.cpp/.h`, `vegetationBuilder_Trees.cpp`, `lru_cache.h`, `imGuiHelper.h`;
top level: `latexgen.h`;
shaders: `compute_sampleRGBtoPixel.hlsl`, `compute_vegetation_sortCombine.hlsl`, `extractTextures.hlsl`.

### Only in extract_2

`log.txt`, `imgui.ini`, and the two `render_vegetation_ribbons - Copy/Backup` files — all deliberately
excluded junk. **Every real source file of extract_2 exists in extract_3.**

### Matching files with differences (changed = diff lines `< + >`; 0-change files omitted — 66 files are identical)

| File | e3 lines | e2 lines | changed | Size |
|---|---|---|---|---|
| `vegetationBuilder.cpp` | 8309 | 4289 | 6770 | LARGE |
| `terrain.cpp` | 8735 | 8190 | 2353 | LARGE |
| `vegetationBuilder.h` | 2124 | 1242 | 1686 | LARGE |
| `hlsl/terrain/render_vegetation_ribbons.hlsl` | 1212 | 1071 | 1329 | LARGE |
| `Earthworks_4.cpp` | 728 | 900 | 684 | LARGE (e2 is *larger* — see surprises) |
| `hlsl/terrain/render_tile_sprite.hlsl` | 232 | 348 | 308 | LARGE (e2 larger) |
| `hlsl/terrain/compute_tileBuildLookup.hlsl` | 193 | 160 | 251 | LARGE |
| `terrain.h` | 952 | 911 | 209 | LARGE |
| `hlsl/terrain/render_Tiles.hlsl` | 437 | 420 | 189 | LARGE |
| `hlsl/terrain/compute_terrain_under_mouse.hlsl` | 132 | 113 | 171 | LARGE |
| `hlsl/render_Common.hlsli` | 244 | 98 | 154 | LARGE |
| `terrafector.cpp` | 2326 | 2231 | 141 | LARGE |
| `hlsl/PBR.hlsli` | 265 | 240 | 131 | LARGE |
| `CMakeLists.txt` | 151 | 123 | 100 | small |
| `hlsl/terrain/compute_vegetation_lod.hlsl` | 157 | 104 | 97 | small |
| `hlsl/terrain/compute_tileClear.hlsl` | 115 | 96 | 87 | small |
| `hlsl/terrain/groundcover_defines.hlsli` | 464 | 430 | 86 | small |
| `ribbonBuilder.cpp` | 416 | 366 | 84 | small |
| `terrafector.h` | 549 | 476 | 73 | small |
| `hlsl/terrain/vegetation_defines.hlsli` | 158 | 126 | 62 | small |
| `Earthworks_4.h` | 124 | 99 | 51 | small |
| `hlsl/terrain/compute_vegetation_clear.hlsl` | 53 | 34 | 35 | small |
| `hlsl/terrain/render_triangles.hlsl` | 182 | 192 | 22 | small (e2 larger) |
| `hlsl/terrain/render_Buildings_Far.hlsl` | 119 | 139 | 20 | small (e2 larger) |
| `hlsl/terrain/compute_clipLodAnimatePlants.hlsl` | 142 | 137 | 19 | small |
| `hlsl/terrain/groundcover_functions.hlsli` | 99 | 80 | 19 | small |
| `hlsl/terrain/render_GliderWing.hlsl` | 156 | 174 | 18 | small (e2 larger) |
| `ribbonBuilder.h` | 105 | 101 | 10 | trivial |
| `hlsl/terrain/render_ribbons.hlsl` | 692 | 692 | 10 | trivial |
| `computeShader.cpp/.h` | — | — | 6 / 2 | trivial |
| `hlsl/atmosphere/compute_volumeFogAtmosphericScatter.hlsl` | 169 | 166 | 5 | trivial |
| `hlsl/terrain/terrainDefines.hlsli` | 36 | 32 | 4 | trivial |
| `atmosphere.cpp` | 280 | 279 | 3 | trivial |
| `compute_tonemapper.hlsl`, `pixelShader.cpp/.h`, `ecotope.cpp` | — | — | ≤2 each | trivial |

Notably **unchanged**: all glider/cfd files, all roads_* files, roadNetwork, Sprites, json.hpp,
most tile compute shaders — the churn since extract_2 concentrated in vegetation, terrain, and a
handful of shaders.

## d. Drift: extract_3 vs current port (`EarthworksFX/src/core/` + `EarthworksFX/hlsl/`)

Matched by basename. changed = diff lines. Remember: the port recently had cfd/glider/deprecated
code *deleted* (commits 75c6f14, c91c0c1), which inflates C++ diffs; and shaders went through a DXC port.

### Largest divergences (the 5–10 to look at)

| File | orig | port | changed | Note |
|---|---|---|---|---|
| `terrain.cpp` | 8735 | 5681 | 7658 | mostly the port-side cfd/glider/editor deletions + port edits interleaved |
| `vegetationBuilder.cpp` | 8309 | 7063 | 4090 | PDF/latex + strip + port edits |
| `terrainGenerator.cpp` | 1601 | 753 | 1201 | heavily cut in port |
| `Earthworks_4.cpp` | 728 | 905 | 415 | port app-shell adaptations |
| `vegetationBuilder.h` | 2124 | 1894 | 390 | |
| `terrafector.cpp` | 2326 | 2318 | 314 | port edits despite near-equal length |
| `terrainGenerator.h` | 319 | 151 | 269 | |
| `terrain.h` | 952 | 897 | 227 | |
| `hlsl/terrain/render_vegetation_ribbons.hlsl` | 1212 | 1154 | 120 | largest shader drift |
| `hlsl/atmosphere/compute_volumeFog.hlsli` | 386 | 423 | 77 | |

### Other matched files

C++ small drifts: `ribbonBuilder.cpp` 51, `atmosphere.cpp` 43, `roads_materials.cpp` 40,
`terrafector.h` 38, `vegetationBuilder_Trees.cpp` 46, `roadNetwork.cpp` 25, `roads_AI.cpp` 22,
`pixelShader.cpp` 15, `lru_cache.h` 11, `Sprites.cpp` 8, `roads_road.cpp` 5.
**Byte-identical in port:** `cascadeShadowMaps.*`, `earthworksScene.*`, `computeShader.*`,
`barrier.hpp`, `PerlinNoise.hpp`, `json.hpp`, `volumeFogPhaseFunctions.cpp`, `atmosphere.h`,
`Sprites.h`, all `roads_bezier/Intersection/physics/cubicDouble` files, `roadNetwork.h`.

Shader drifts 20–60: `render_spline` 51, `groundcover_defines.hlsli` 49, `render_splineTerrafector` 38,
`compute_volumeFogAtmosphericScatter` 37, `compute_tonemapper` 32, `render_meshTerrafector` 28,
`render_ribbons` 25, `materials.hlsli` 22, `compute_tileVertices` 21.
Shader drifts ≤16: `compute_tileBuildLookup` 15, `compute_tileEcotopes` 15, `compute_bakeFloodfill` 13,
`render_tile_sprite` 12, `compute_vegetation_lod` 12, `render_Tiles` 11, `compute_sampleRGBtoPixel` 10,
`compute_vegetation_sortCombine` 10, `render_Buildings_Far` 8, `compute_bc6h_functions` 6,
`compute_volumeFogLights` 6, `noise.inc` 5, `PBR.hlsli` 3, `vegetation_defines` 16.
**28 shaders byte-identical** in the port (incl. all remaining tile computes, `render_Common.hlsli`,
`material.hlsli`, sprites, `render_GliderWing`, `render_cfdSlice`, `render_thermalRibbons`,
`render_triangles`, `compute_terrain_under_mouse`, `extractTextures`).

### Only in port (no counterpart in original)

C++: `EarthworksDebug.h` (220), `TestFlightData.h` (175), `buildings.cpp/.h` (292/105) — known
port-side additions per task.md.
Shaders: `debugGrid.hlsl` (180, port debug tool), `viewRenderData_lookupBuffers.hlsli` (133 — see surprises),
`atmosphere/CSVolumeFogCommon.hlsli` (1 line), and — surprisingly — the two
`render_vegetation_ribbons - Copy/Backup` junk files (870/1075 lines) sitting in the port hlsl tree.

### Only in original (no port counterpart)

`cfd.*`, `glider.*`, `glider_runtime.cpp`, `latexgen.h` (stripped from port in recent commits),
`terrain_Builder_GIS.*` (never imported — it is dead code, see below), `imGuiHelper.h`.
Every original shader has a port counterpart.

## e. Surprises / anomalies

1. **`terrain_Builder_GIS.cpp/.h` (9,691 lines) is dead code in the original**: not listed in
   `CMakeLists.txt`, `#include`d by nothing. It is a near-copy fork of `terrain.cpp/.h`
   (only 881 diff lines vs `terrain.cpp`). Task.md flagged it as an UNCERTAIN subsystem
   ("terrain_Builder_GIS"); in reality it looks like an abandoned GIS-import variant of terrain.
   Proposal: STRIP, optionally after mining its 881-line delta for anything GIS-relevant.
2. **`viewRenderData_lookupBuffers.hlsli` does NOT exist in the original.** Task.md §3 listed it
   among files "the June-21 import brought in"; it is actually port-side-only (133 lines) —
   presumably an include extracted during the DXC shader port, not original content.
3. **The port's `hlsl/` tree contains the editor backup junk** `render_vegetation_ribbons - Copy.hlsl`
   and `render_vegetation_ribbons - Backup after new animations.hlsl` — copied over in the June
   import; candidates for deletion in the port.
4. **extract_2 was not simply older — some of its files are *larger* than the fresh original**:
   `Earthworks_4.cpp` 900 vs 728 (684 changed lines), `render_tile_sprite.hlsl` 348 vs 232,
   `render_Buildings_Far.hlsl` 139 vs 119, `render_GliderWing.hlsl` 174 vs 156,
   `render_triangles.hlsl` 192 vs 182. The original repo evolved with deletions too; extract_2
   is a genuinely different snapshot, not a subset.
5. **extract_2 lacked `latexgen.h` and `imGuiHelper.h`** even though both sit in the original
   top level / earthworks_scene — the old extract silently filtered files that the June import
   later also needed context for.
6. **The shadow tech is tiny**: `cascadeShadowMaps.*` totals 148 lines (render-target setup;
   the class is called `shadowMap` with near/far FBOs) — and the port copy is byte-identical
   to the fresh original. Ditto `earthworksScene.*`, which is an empty scaffold (21 lines total)
   despite its central-sounding name. Answers task.md §8.3: yes, the original has it, and it is
   already faithfully in the port; the real shadow logic must live in terrain/vegetation shaders
   and callers, not in this class.
7. **Fresh extract fully supersedes both references** (answers §8.4): every extract_2 source
   file and every June-import file has a counterpart here, and the June-import-only files
   (`cascadeShadowMaps`, `earthworksScene`, `extractTextures`, `compute_terrain_under_mouse`,
   `render_Common.hlsli`, …) match the fresh original closely or exactly — so the original repo
   has not moved meaningfully past the June-21 import, and `source_extract_3` can serve as the
   single source of truth for the re-port.
8. **PDF export is woven into vegetationBuilder.cpp** (`#include "harupdf/include/hpdf.h"`,
   `latexgen.h`; `pdf_*` functions from ~line 6359): stripping PDF/latex is a partial-strip
   inside a PORT-classified core file, not just a file deletion.
9. `terrain.h` directly includes `glider.h`, `cfd.h`, and `terrainGenerator.h`, and holds glider
   runtime members (`paraBuilder`, `paraRuntime`, `newGliderRuntime`, cfd thread methods) —
   confirming that glider/cfd stripping is surgery inside terrain.*, mirroring what the recent
   master commits did to `EarthworksFX/`.
