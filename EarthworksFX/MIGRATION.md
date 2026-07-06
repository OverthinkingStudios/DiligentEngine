# EarthworksFX Migration Guide

**1:1 port** of the original Falcor *Earthworks 4* terrain system. C++ algorithms, data layouts, and HLSL shaders are copied verbatim; only the rendering host API is adapted through the compat shim (`interface/Falcor.h`, `src/compat/Falcor.cpp`, `src/compat/FalcorGpu.cpp`).

Companion docs: `PROJECT_OVERVIEW.md` (renderer internals, camera/matrix conventions, debug tooling), `BRINGUP_NOTES.md` (living findings log F1–F26+, session history — the authoritative "what broke and why").

## Non-negotiable: preserve the algorithms

The terrain system is highly sophisticated — quadtree LOD splitting, GPU tile bake chains, ecotope/vegetation instancing, road networks, terrafector stamping, optional CFD. These paths are hyper-tuned and interdependent.

**Do not rewrite, simplify, or "clean up" algorithm code in `src/core/` or `hlsl/`** to fix port issues. Fixes belong in the compat layer, CMake wiring, or guarded `#if 0` deferrals. When in doubt, stub the Falcor/Diligent bridge — not the math. The bring-up history validates this rule: every major bug so far (F7 dead cbuffer blobs, F10 missing UAV flags, F16 dynamic-IB device loss, F20 handedness, F26 stubbed texture upload) was in the shim, not the core.

## What was ported

| Area | Destination |
|------|-------------|
| Application shell (`Earthworks_4.*`) | `src/core/` |
| Terrain core (`terrain.*` + tile compute/render shaders) | `src/core/` + `hlsl/terrain/` |
| Atmosphere / volumetric fog | `src/core/` + `hlsl/atmosphere/` |
| Roads / terrafector / ecotope / vegetation | `src/core/` |
| Shaders (53 files, Slang→HLSL by hand) | `hlsl/` |
| Buildings | re-enabled via new delegate `src/core/buildings.*` (edits to core kept to one-line calls) |
| CFD / glider | present in source, deferred behind `#if 0` (`EARTHWORKSFX_DEFERRED_CFD` / `_GLIDER`) |

## Current state (2026-07-03)

**The port runs and renders.** Vulkan and D3D12 both pass the automated smoke testflight (12 cameras, exit 0). Working end-to-end:

- Terrain: quadtree split/merge, JP2 streaming (OpenJPH), full GPU tile-bake chain, indirect draws, real relief and per-tile albedo/normal/PBR/height texture arrays
- Atmosphere + volumetric fog, skydome, tonemapper, CPU-solved terrain shadows
- Camera is genuinely right-handed (matrices bit-compatible with original Falcor/glm through `toGLM`); world compass NORTH = -Z verified against real maps
- Terrafector tile bakes, texture loading from file (DDS/KTX/JPG/PNG via Diligent TextureLoader), ImGui editor GUI + `ew::gDebug` debug/metrics panel, testflight automation
- Buildings pass re-enabled (delegate class; verified in later runs)

⚠ Testflight reference images from before 2026-07-03 are mirrored (pre-F20) and/or have placeholder shadows (pre-F26) — re-baseline before comparing.

## Remaining compat gaps (checked against source, 2026-07-05)

Still stubbed or incomplete in `src/compat/Falcor.cpp`:

| API | State | Blocks |
|-----|-------|--------|
| `RenderContext::readTextureSubresource` | returns zeros | bake height export, tile readback |
| `Texture::generateMips` | stub | ecotope low-res sampling (`rootElevation` wants 8 mips) |
| `Texture::captureToFile` | stub | screenshots from core code |
| `RenderContext::resourceBarrier` | no-op | explicit state transitions (implicit transitions cover today's paths) |
| `openFileDialog` / `saveFileDialog` | return false | editor load/save flows (roads, terrains) |
| `TextRenderer::render`, `RenderContext::clearTexture` | stubs | minor |
| `GraphicsState::setViewport` | ignored (full-FBO viewport always) | sub-viewport rendering |

Known open issues (details in BRINGUP_NOTES):

- **F24** — `settings.dirResource` resolves empty at runtime → `<dirResource>` assets (skies, color cubes, road materials) missing; data workaround known, code fix pending
- Sprite `gTex`/`gNorm`/`gTranclucent` load 2D files but shaders declare `Texture2DArray` → dimension guard substitutes dummies
- Fog RW-target format mismatches beyond the fixed ones (F17 tail); vkCmdCopyImage layout warnings on two tile-copy images (benign on NVIDIA)
- Dummy-texture dimension is guessed from resource *name* (`GuessResourceDimFromName`) — fragile, exact-match only

Deferred subsystems: CFD (`_cfdClipmap` port needed), glider mode (out of scope unless revived).

## Runtime configuration

| Setting | Value |
|---------|-------|
| API | Vulkan primary; D3D12 works (`-m d3d12`), used for testflights/comparison |
| Working directory | gameroot (`ACSMP_GAMEROOT`, e.g. `C:\dev\git\os\gameroot_dev`); `hlsl/` is deployed there at build time (`DeployHLSL.cmake`) — source-tree shader edits need a build/copy to take effect |
| Test terrain | `terrains/switserland_Steg/` (`elevations.txt` manifest, `.bil` LOD0 root + `.jp2` tiles; optional `terrainSettings.json`) |
| State files | `lastFile.xml` (last terrain; see F24 for the dirResource trap), `camera.bin` (raw CameraData — delete if the camera starts somewhere insane), `earthworks4_presets.xml` |

Threading active today: JP2 hash/cache paging thread, `_shadowEdges::solveThread` (4096² CPU shadow solve). CFD thread deferred with the CFD port.

## Validation

`--testflight smoke` (see `TESTFLIGHTS.md`) is the regression gate: 12 cameras across the terrain, per-camera stability check and reference-image comparison, on both APIs. Re-baseline references after any change that legitimately alters output.
