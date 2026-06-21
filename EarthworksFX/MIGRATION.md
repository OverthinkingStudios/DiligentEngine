# EarthworksFX Migration Guide

This folder contains a **1:1 port** of the original Falcor *Earthworks 4* terrain system from `docs/source_extract_2/`. The C++ algorithms, data layouts, and HLSL shaders are copied verbatim. Only the rendering host API is adapted through `interface/FalcorCompat.hpp` and `src/compat/FalcorCompat.cpp`.

## Non‑negotiable: preserve the algorithms

The terrain system is **highly sophisticated** — quadtree LOD splitting, GPU tile bake chains, ecotope/vegetation instancing, road networks, terrafector stamping, optional CFD, and more. These paths are hyper‑tuned and interdependent.

**Do not rewrite, simplify, or “clean up” algorithm code in `src/core/` or `hlsl/`** to fix port issues. Fixes belong in the compat layer (`FalcorCompat`), CMake wiring, or guarded `#if 0` deferrals for subsystems we are not enabling yet. When in doubt, stub the Falcor/Diligent bridge — not the math.

## What was ported

| Area | Source | Destination |
|------|--------|-------------|
| Application shell | `Earthworks_4.cpp/.h` | `src/core/` + `EarthworksFXSample.*` |
| Terrain core | `terrain.cpp/.h` + tile compute/render shaders | `src/core/` + `hlsl/terrain/` |
| Atmosphere / fog | `atmosphere.cpp/.h` + `hlsl/atmosphere/` | `src/core/` + `hlsl/atmosphere/` |
| Roads / terrafector / ecotope / vegetation | all `.cpp/.h` from extract | `src/core/` |
| Shaders (53 files) | `docs/source_extract_2/hlsl/` | `EarthworksFX/hlsl/` |
| Diligent entry | — | `src/EarthworksFXSample.cpp` |
| CFD / glider (deferred) | `terrain.cpp` sections, `glider_runtime.cpp` | present but disabled for now |

## Architecture

```
EarthworksFXSample (Diligent SampleBase)
    └── Earthworks_4 (original app logic, unchanged)
            └── terrainManager (quadtree tile pipeline, roads, veg, …)
                    └── computeShader / pixelShader (Falcor-style wrappers)
                            └── FalcorCompat → Diligent IRenderDevice / IDeviceContext
```

Dependency chain for CPU libraries (no duplicated vendoring in EarthworksFX):

```
EarthworksFX → Earthworks::Earthworks → OTSCommon (cereal, glm, …)
                                        → openjph / assimp (when enabled)
```

## Current state (2025)

| Item | Status |
|------|--------|
| Core sources copied 1:1 | Done |
| HLSL copied to `hlsl/` | Done |
| `FalcorCompat.hpp` API surface | Substantially expanded; many methods still stub GPU work |
| **cereal** | Real library via OTSCommon (`cereal::cereal`); local `interface/cereal/` stub **removed** |
| **OpenJPH / Assimp** | Via Earthworks (FetchContent or `extern/`); local `interface/stubs/` **removed**; CMake fails fast if disabled |
| **`terrain.cpp` compile** | Isolated compile clean (compat fixes only; no algorithm edits) |
| **`FalcorCompat.cpp` compile** | Clean (`CopyTextureAttribs` uses Diligent field names) |
| **CFD (`_cfdClipmap`)** | Algorithm preserved in source; active use wrapped in `#if 0 // EARTHWORKSFX_DEFERRED_CFD` |
| **Glider / `_gliderRuntime`** | Not being ported; paraglider paths wrapped in `#if 0 // EARTHWORKSFX_DEFERRED_GLIDER`; `glider_runtime.cpp` still defer |
| **Full link / run** | Not yet — GPU draw/dispatch path is still stubbed |

### Compat layer already wired (non‑exhaustive)

- `DepthStencilState`, expanded `Input`/`KeyboardEvent`/`MouseEvent`, `Camera` helpers
- `ComputeVars::setBlob`, `Texture::create2D(ResourceFormat)`, `copyResource`, `copySubresource`, `clearRtv`
- `DeviceInterface::getRenderContext`, `RenderContext::readTextureSubresource` (returns empty — needs Phase 4)
- CPU mirrors for `_buildingVertex` / `_gliderwingVertex` in `terrain.h` (HLSL layout match)

## GPU wiring roadmap (FalcorCompat only)

All items below are implemented **only** in `src/compat/FalcorCompat.cpp` (guardrail: Diligent links live in `EarthworksFX/CMakeLists.txt` and nowhere else).

### Phase 1 — Device bootstrap

1. **`SetFalcorDevice`** — call from `EarthworksFXSample` before terrain `onLoad`; verify `g_pDevice` / `g_pContext` are non-null.
2. **`Texture::create2D/3D` with initial data** — pass `pData` through `CreateTexture` or staging upload (currently ignored).
3. **`Buffer::setBlob` / CPU shadow** — complete `uploadShadowRange()` → `UpdateBuffer` on real `IBuffer`.

### Phase 2 — Shader pipeline (required before anything draws)

4. **`ComputeProgram::createFromFile`** — compile HLSL via Diligent shader stream factory + `CreateShader`; build compute PSO.
5. **`GraphicsProgram::create`** — VS/PS/GS + graphics PSO; map `Program::DefineList` to shader macros; use `RemapShaderPath()` (strips `Samples/Earthworks_4/`).
6. **`ProgramReflection` / binding** — parse reflection or maintain binding maps so `ShaderVar`, `ComputeVars::setTexture/setBuffer/setBlob`, and `getParameterBlock()` reach SRB slots.
7. **`ComputeVars::setBlob`** — bind constant-buffer blobs through SRB after reflection exists.

### Phase 3 — Draw / dispatch path

8. **`RenderContext::dispatch` / `dispatchIndirect`** — set compute PSO, bind SRB, call `IDeviceContext::Dispatch*`.
9. **`RenderContext::drawInstanced` / `drawIndexedInstanced` / `drawIndirect`** — set graphics PSO, VB/IB, RTV/DSV from `GraphicsState::Fbo`, bind SRB, `Draw*`.
10. **`RenderContext::clearFbo`** — clear all FBO color attachments + depth (`clearRtv` works for individual RTVs today).
11. **`GraphicsState`** — apply rasterizer / blend / depth-stencil descs when committing PSO.

**Checkpoint after Phase 3:** terrain tiles, compute split/clip passes, and vegetation indirect draws should execute on GPU.

### Phase 4 — Resource ops (bake, picking, deferred CFD)

12. **`RenderContext::updateTextureData`** — staging + upload (CFD volume slices when CFD is re-enabled).
13. **`RenderContext::copySubresource`** — structurally done; verify array-texture tile picking (`height_Array` slice copies) at runtime.
14. **`RenderContext::resourceBarrier`** — map `Resource::State` → `TransitionResourceStates` before readback/copies.
15. **`RenderContext::readTextureSubresource`** — staging readback (bake height export, tile readback); currently returns zeros.
16. **`Texture::captureToFile`** — readback + PNG/write helper.
17. **`Texture::generateMips`** — `GenerateMips` or compute mip pass.

### Phase 5 — Polish

18. **`RenderContext::blit`** — overlay thumbnails (`Earthworks_4::onRenderOverlay`).
19. **`Sampler::create`** — real `ISampler` objects bound through SRB.
20. **FBO mip UAVs** — `getColorTexture(n)->getUAV(mip)` for tile elevation mip chain.
21. **`TextRenderer::render`**, **`Gui::addFont`** — SampleBase / ImGui font path.
22. **`openFileDialog` / `saveFileDialog`** — native or SampleBase wrappers for editor UX.

## Runtime configuration (current defaults)

| Setting | Value |
|---------|-------|
| **API** | Vulkan only (`GetDesiredApplicationSettings` → `RENDER_DEVICE_TYPE_VULKAN`) |
| **Working directory** | Repo root (CMake `VS_DEBUGGER_WORKING_DIRECTORY`) |
| **Test terrain** | `terrains/switserland_Steg/` (relative to pwd) |
| **Elevation manifest** | `terrains/switserland_Steg/elevations.txt` |
| **Tile data** | `terrains/switserland_Steg/elevations/*` (`.bil` LOD0 root, `.jp2` tiles) |
| **Optional JSON** | `terrains/switserland_Steg/terrainSettings.json` — skipped if missing; folder bootstrap applies |

There is **no** single `.terrain` file. Optional `terrainSettings.json`; if absent, `terrainManager::onLoad` bootstraps `_terrainSettings` when `elevations.txt` exists.

Copy `EarthworksFX/assets/lastFile.xml` to pwd (or rely on struct defaults). Place elevation data under `terrains/switserland_Steg/` per `assets/terrains/switserland_Steg/README.md`.

## Assets & data paths

```
terrains/switserland_Steg/
  elevations.txt
  elevations/          # .bil + .jp2
  terrainSettings.json # optional
terrains/_resources/   # shared vegetation / color cube assets
```

- **OpenJPH** — required via Earthworks (`EARTHWORKSFX_ENABLE_OPENJPH=ON`); JP2 decode in `hashAndCache*`.
- **Assimp** — required via Earthworks (`EARTHWORKSFX_ENABLE_ASSIMP=ON`); mesh export paths in roads/terrafector.
- **cereal** — binary/JSON/XML saves for presets, roads, ecotopes (real cereal from OTSCommon).

## Threading & background work (enable after GPU baseline)

Original uses detached threads; verify lifetime before enabling:

| Thread | Entry | Purpose | Notes |
|--------|-------|---------|-------|
| CFD | `terrainManager::cfdThread` | Fluid clipmap simulation | Source commented; re-enable with `_cfdClipmap` port |
| Shadow | `_shadowEdges::solveThread` | 4096² sun shadow solve on CPU | |
| Hash/cache | `hashAndCache_Thread` | JP2 elevation paging | OpenJPH path active |

**TODO:** graceful shutdown (original calls `gpFramework->getWindow()->shutdown()` from terrain UI).

## Input & UI

- Map Diligent `InputController` → `KeyboardEvent` / `MouseEvent` (compat enums expanded; wiring incomplete).
- Large ImGui surface in `terrain.cpp` — no algorithm changes needed.
- CFD / glider GUI blocks remain in source but are inside deferred `#if 0` regions.

## Sample integration

1. Register EarthworksFX in top-level `DiligentSamples/CMakeLists.txt` (if not already)
2. Tutorial / sample executable wiring
3. Copy runtime assets next to executable (`hlsl/`, terrain data, fonts)
4. Set VS debugger working directory to gameroot

## Subsystems — enable order (after terrain tiles render)

Enable incrementally; each subsystem’s **algorithm code stays untouched**:

- [ ] Core terrain clipmap + atmosphere bind
- [ ] Roads (`roadNetwork`, bezier/physics/AI)
- [ ] Terrafector editor
- [ ] Vegetation ribbons / billboards
- [ ] Tonemapper + 33³ color cube LUT
- [ ] CFD smoke / streamlines (un-`#if 0` + port `_cfdClipmap` from Falcor)
- [ ] Glider mode — **out of scope** unless explicitly revived
- [ ] Overlay material thumbnails

## Shader inventory (unchanged)

All shaders copied to `hlsl/`. Key terrain pipeline:

**Compute (tile bake):** `compute_tileClear`, `compute_tileBicubic`, `compute_tileVertices`, `compute_tileNormals`, `compute_tileDelaunay`, `compute_tileJumpFlood`, `compute_tileSplitMerge`, `compute_tileBuildLookup`, `compute_tileGenerate`, `compute_tileEcotopes`, `compute_bc6h`

**Render:** `render_Tiles.hlsl` (main terrain draw), `render_tile_sprite`, vegetation/road/spline variants

**Atmosphere:** `compute_sunlightInAtmosphere`, `compute_volumeFogAtmosphericScatter`, smoke/dust variants

## Validation order

1. Phase 1–3 complete: single compute dispatch and one `drawInstanced` hit the GPU without stub logs
2. Single tile compute chain produces vertices/normals (capture GPU buffers)
3. `render_Tiles.hlsl` draws one clipmap level
4. Atmosphere inscatter textures bound to terrain shader
5. Shadow thread uploads `terrainShadow` RG32F
6. Full `terrainManager::update` + `onFrameRender` loop
7. Tonemapper to swap chain

## Files to defer during first bring-up

- `glider_runtime.cpp` (7.4k lines) — not planned for initial port
- CFD active paths in `terrain.cpp` — preserved under `#if 0`, revive with `_cfdClipmap` port
- Backup shader copies (`render_vegetation_ribbons - Copy.hlsl`, etc.)
