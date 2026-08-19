# Phase 3 — B+ Execution Plan

**Read `../task.md` (§0 ground rules) and `../path_decision.md` first.**
Catalog docs in `../catalog/` are the authority on what must survive; `source_extract_3/` is the
code authority. The old port stays available as reference in `EarthworksFX/legacy/`.
Statuses here are the single source of progress truth — update on every step completion.

## Target tree (after step 1)

```
EarthworksFX/
  legacy/                  <- moved, EXCLUDED from build: old src/core/, src/compat/, hlsl/, interface/Falcor.h
  interface/               <- shell headers (ApplicationBase, Window, camera, TestFlight) — decoupled from Falcor.h
  src/app/                 <- shell impl (kept, decoupled)
  src/gpu/                 <- NEW native layer (see below)
  src/core/                <- NEW fresh port (starts as skeleton)
  hlsl/                    <- NEW, filled per-subsystem from source_extract_3/hlsl (extract = authority)
```

## Native GPU layer design (src/gpu/, namespace `ew`)

Purpose-built, NOT a Falcor emulator. Contents:
- `GpuContext` — owns IRenderDevice/IDeviceContext/ISwapChain/IEngineFactory pointers (set by shell).
- `Texture`, `Buffer`, `Fbo` — thin RefCntAutoPtr wrappers with the handful of operations the
  engine uses (create2D/3D/structured/typed/raw, setBlob upload, map/read-back, views, UAV
  counter replacement: explicit counter buffer — see terrain_quadtree_streaming.md §9).
- `computeShader`, `pixelShader` — SAME shape as the original wrappers (catalog:
  conventions_app_wiring.md §5 documents the exact contract) implemented directly on Diligent:
  load(path, entries, defines), named resource binding (BIND BY NAME — never by register,
  shader_interface.md), State (blend/depth/raster/FBO), drawInstanced (must NOT bind the IB),
  drawIndexedInstanced (binds shared 128-quad IB), renderIndirect (non-indexed 16-byte args,
  offset view*16), dispatch, dispatchIndirect.
- SALVAGE the proven mechanics from `legacy/src/compat/FalcorGpu.cpp`: DXC compile path, PSO/SRB
  caching, shader path resolution — carve out, rename into `ew::`, drop ShaderVar-tree emulation
  (bindings become explicit named sets on the wrapper).
- Invariants baked in as defaults/asserts: front CCW, depth [0,1], GLM_FORCE_DEPTH_ZERO_TO_ONE
  public, RH camera conventions (conventions_app_wiring.md §3).

## Bring-up steps

| # | Step | Deliverable (developer builds & runs after each) | Status |
|---|------|--------------------------------------------------|--------|
| 1 | Restructure + native layer + stub renderer | Tree per above; shell compiles without Falcor.h; `ew::` layer compiles; sample shows debugGrid via new layer | **DONE — developer-verified: identical on D3D12+VK, zero warnings (2026-08-02)** |
| 1b | Fix tool apps broken by Falcor.h removal | TextureSplitTool +28/−10 (includes/aliases), Editor +1, TerrainGenerator ok (commit `dcc740e`; enum-class collision fix by developer) | **DONE — developer-verified** |
| 2 | Terrain minimal | quadtree + JP2 streaming + tile bake + flat-shaded tiles. Fix-round root causes in `bringup_notes.md` P1-P8 (GenerateMips view flag, data-path resolution + `-terrain` override, setCamera glm convention, readback age-gate) | **DONE — developer-verified 2026-08-03: full terrain renders on VK + D3D12, ~1-1.2k fps D3D12, bottom-tile flicker fixed** |
| 3 | Full tile bake + atmosphere + shadowEdges | spec below (`Step 3 spec`); launch prompt: `port_effort_3/execution/step3_prompt.txt` | **DONE — developer-verified 2026-08-03: full bake + atmosphere + shadowEdges + HDR/tonemap path render on VK + D3D12; one fix round (P10)** |
| 4 | Vegetation | full system ported, running DORMANT (dataset has no plant data — compile/run correctness is the acceptance path). Prompt: `step4_prompt.txt` | **DONE — developer-verified 2026-08-04: dormant-but-healthy on VK + D3D12, zero fix rounds. Visual validation deferred until plant data exists.** |
| 5 | Terrafectors + roads bake | y=0 checklist enforced in code from day one; **BINDLESS decision point — developer chose faithful arrays at launch (2026-08-04); STEP5-NOTE marks the upgrade path**. Prompt: `step5_prompt.txt` | **DONE — developer-verified 2026-08-04 on D3D12 (terrafectors + roads render correctly, ~850 fps; 3 fix rounds P14/P15). Open: VK-only solid-white terrafectors (NonUniform-indexing suspect) + a rare both-API hang — carried as step-5 follow-ups, see step log** |
| 6 | Salvage (buildings/debug tools) + measured perf pass (vsync uncap, fence batching, testflight numbers table). **Sprites DROPPED from salvage scope (developer, 2026-08-04: the sprite data files are 0-byte and the system is unused per the original dev)** | Prompt: `step6_prompt.txt` | ready (after 5) |
| 7 | terrainGenerator subsystem (first-class port, GUI survives with minimal edits) | Prompt: `step7_prompt.txt` | ready (after 6; independent enough to run earlier if desired) |
| 8 | Parity wrap: STUB/marker sweep, S1-goal parity table, CLAUDE.md truth pass, legacy retirement proposal, backlog handoff | Prompt: `step8_prompt.txt` | ready (last) |

**Cold-context resume recipe:** read `../task.md` → this file (statuses above are the truth) →
launch the first non-DONE step by passing its `stepN_prompt.txt` content (or the file reference)
to a general-purpose agent. After the agent returns: review its diff against the relevant
catalog docs, commit on `port_effort_3` (main context commits, agents never), hand the build to
the developer, fix the fallout (new root causes go into `bringup_notes.md` as P-entries), mark
the step DONE only after developer verification.

Rules for every step: extract code is authority; adapt to `ew::` layer mechanically; consult the
subsystem's catalog doc BEFORE changing anything it lists as a must-keep; keep `// STRIP-REVIEW`
and original comments; zero warnings; no commits by subagents; developer compiles/tests.

## Step 1 spec (cold-agent executable)

1. `git mv` old `EarthworksFX/src/core` → `EarthworksFX/legacy/core`, `src/compat` → `legacy/compat`,
   `hlsl` → `legacy/hlsl`, `interface/Falcor.h` → `legacy/Falcor.h`. EXCEPTIONS staying live:
   `EarthworksDebug.h`, `TestFlightData.h` (move to `src/app/` or `interface/`), `buildings.*`
   stays in legacy for now (salvaged in step 6).
2. CMake: remove legacy from `EarthworksFX/CMakeLists.txt` sources; add `src/gpu/` + new `src/core/`.
3. Create `src/gpu/` per design above (carving from legacy/compat where proven).
4. New `src/core/Earthworks_4.h/.cpp` skeleton: class `Earthworks_4` with the same lifecycle the
   shell calls (onLoad/onFrameUpdate/onFrameRender/onShutdown/onResize/onKey/onMouse — signatures
   now in `ew::` types), containing only: clear to a color + debugGrid render (salvage
   `legacy/hlsl/debugGrid.hlsl` + its host code from legacy core) + camera hookup.
5. Decouple shell: `EarthworksFXApplicationBase.*`, `EarthworksFXSample` — replace Falcor types
   (RenderContext/Fbo/gpFramework/EarthworksWrapper) with `ew::` equivalents; keep testflights,
   debug UI, ImGui, input paths working. Smallest possible diff to the shell.
6. Zero warnings. Developer then builds (rebuild-all.bat) and runs the sample; expected: debugGrid
   + Earthworks debug panel, both APIs (VK primary, D3D12 check).

## Step 3 spec (cold-agent executable — full prompt in step3_prompt.txt)

Scope: everything tagged `STEP2-STUB` except vegetation (step 4) and terrafector/roads bake
(step 5). Concretely:
1. **Full tile bake**: ecotopes block re-inserted at its marked position in `splitChild`
   (ecotope.cpp/.h port comes with it — placement data only, GUI stays stripped),
   compute_tileEcotopes, compute_bakeFloodfill, compute_bc6h(+functions) + PBR array publish.
2. **render_Tiles real psMain**: restore `../PBR.hlsli`, `gpuLights_functions.hlsli`,
   `render_Common.hlsli` includes (port those files from the extract), LightsCB replaces
   Step2StubCB, full material/fog/shadow sampling.
3. **Atmosphere**: atmosphere.cpp/.h + volumeFogPhaseFunctions.cpp + hlsl/atmosphere/* (skip the
   dead volumeFogLights/SmokeAndDust — catalog atmosphere_shadows.md documents them as dead even
   in the original; cfd smoke seam stays stubbed), skydome/triangleShader, sky textures
   (bind by exact name — F16), sun transmittance + Mie phase LUTs, froxel volume.
4. **shadowEdges**: CPU scanline solver + solveThread + terrainShadowTexture (RG32F 4096²) +
   onFrameUpdate atomic sun/texture update. GUI that retriggered `requestNewShadow` was
   stripped — add a `ew::gDebug` toggle/slider (sun angle + re-solve button) instead.
5. **HDR pipeline**: hdrFbo (R11G11B10 + D24S8), tonemapper (a GRAPHICS pass despite the
   filename — bind by name, never by register), colorCube 33³ LUT from dirResource/colorcubes,
   hdrPreviousFrame half-res blit (feeds JHFAA later), `updateShaderConstants` full fan-out,
   bind gHDRBackbuffer in compute_terrain_under_mouse.
6. Housekeeping: silence the "bound as render target will be unset" Info by unbinding
   explicitly; clean-gameroot-hlsl reminder to the developer at the end.

## Step log

- 2026-08-02: plan authored; B+ approved; step 1 launched.
- 2026-08-02: step 1 executed (cold agent), not yet compiled:
  - `git mv`: old `src/core`→`legacy/core`, `src/compat`→`legacy/compat`, `hlsl`→`legacy/hlsl`,
    `interface/Falcor.h`→`legacy/Falcor.h`, `interface/GlmBasicMath.h`→`legacy/GlmBasicMath.h`.
    Kept live (moved to `interface/`): `EarthworksDebug.h`, `TestFlightData.h`, **`json.hpp`**
    (TestFlightData/-Controller depend on it). `debugGrid.hlsl` moved to new `hlsl/`.
  - NEW `src/gpu/` (`ew::`): `ewTypes.h` (glm aliases, Input/Keyboard/MouseEvent mirrors,
    toDiligent/toGlm), `ewGpuContext.h/.cpp` (device ptrs singleton, data dirs + shader search
    paths, clearFbo, scaling blit — carved from legacy compat), `ewResources.h/.cpp` (Texture,
    Buffer, Sampler, Fbo incl. swap-chain proxy + all-MRT Formats), `ewShader.h/.cpp`
    (computeShader/pixelShader per the original wrapper contract; DXC compile path, dx/dy
    workaround, NO_DYNAMIC_BUFFERS, bind-to-every-stage, per-instance cbuffers with named
    setVariable/setBlob instead of the ShaderVar tree; PSO cache per state-desc+FBO-formats;
    CCW front forced), `ewCamera.h/.cpp` (glm-native RH lookAt + perspectiveRH_ZO, film-back FOV).
  - NEW `src/core/Earthworks_4.h/.cpp` skeleton: clear + debugGlobe/debugGroundGrid (salvaged;
    placeholder 8x8 static grid until the quadtree lands) + camera (original defaults).
  - Shell decoupled: ApplicationBase hpp/cpp (GpuContext replaces EarthworksWrapper/RenderContext/
    Framework/Gui; ew::Fbo target; ew::MouseEvent SyncInput; testflights use ew::Camera and
    `GetEarthworks().getTerrainName()` instead of terrainManager::lastfile). Sample unchanged.
    Editor/TextureSplitTool/TerrainGenerator only consume the base — expected to keep building.
  - CMake: legacy excluded from build; `src/gpu` added (sources + PUBLIC include dir).
  - PORT-REVIEW markers: camera.bin/presets persistence deliberately dropped in the skeleton
    (CameraData layout changed); debug-aid toggles forced ON in onLoad for first-run visibility;
    dummy-texture fallback is 2D-only (no name-based dimension guessing anymore); CCW front
    forced at PSO build.
  - Known compile risk: relies on Diligent `operator==` for BlendStateDesc/DepthStencilStateDesc/
    RasterizerStateDesc in the pixelShader PSO cache (DiligentCore headers are read-denied to
    agents — could not verify). If it doesn't compile, replace with explicit field comparisons.
- 2026-08-02: step 2 executed (cold agent), not yet compiled:
  - NEW `src/core/terrain.h/.cpp` — structure-preserving port of extract terrainManager:
    lastFile.xml (cwd) + terrainSettings resolution (file dialogs → loud logs + graceful
    disable; first-run bootstrap → `<cwd>/terrains/switserland_Steg`), 997-tile quadtree,
    split/merge (one split/frame, 150/300 lod_Pix, 0.9/1.0 radii), two-stage async JP2
    decode state machines (OpenJPH, LRU 45/45/40), frustumFlags 16 KB blob, full minimal
    bake chain per child: splitMerge → clearFbo+per-RTV clears → bicubic hgt+albedo →
    height_Array publish → passthrough → vertex seed copies → albedo publish → normals →
    vertices → jumpflood 4/2/1 → delaunay. Jumpflood+delaunay ARE in (tileVertices output
    is unusable without them — render_Tiles reads delaunay's VB). Ecotopes/bc6h/topdown
    bake/vegetation/skydome/atmosphere/shadows all `STEP2-STUB`-tagged at their exact
    chain positions.
  - hlsl/terrain/: 16 extract shaders copied (14 verbatim); groundcover_defines gets the
    F9 GC_feedback pads + static_asserts (field list re-audited = legacy's, offsets 48/
    724/1232/1280/1480, stride 1888) and DXC default-initializer strips; tileVertices
    groupshared-init strip + `uint idx = 0` (PORT-REVIEW); render_Tiles = verbatim VS +
    STEP2-STUB flat N·L PS (albedo × sun N·L + const ambient, gConstColor debug pattern;
    Step2StubCB carries sunDirection).
  - **ParameterBlock flattening decision**: `viewRenderData_lookupBuffers.hlsli` — 54
    individually named RWStructuredBuffers + Store*Lookup switch helpers (legacy's proven
    approach); consumers stay byte-identical (bind one per-view buffer as "tileLookup").
  - **Readback decision**: `ew::ReadbackBuffer` — 3-slot staging ring + IFence
    (EnqueueSignal per copy, map only fence-completed slots, MAP_FLAG_DO_NOT_WAIT).
    1–2 frame latency for tileCenters origin.y patch + GC_feedback picking/metrics; the
    two EnqueueSignals/frame each flush the command buffer (cheaper than the original's
    full stalls; revisit step 6).
  - ew:: layer additions (kept in layer style): Texture::generateMips + mips-with-initdata
    (rootElevation), GpuContext::clearRtv/copyResource/copySubresource, ReadbackBuffer.
  - Earthworks_4 wiring: terrain member; onFrameUpdate = lightBuffer(sun only) →
    updateShaderConstants → setCamera(Main_Center, …, 1920 hardcoded) → update; render
    straight to swap-chain FBO (hdrFbo+tonemapper = step 3), fullResetDoNotRender gate;
    debug ground grid now shows LIVE quadtree leaves (lod-coloured); gDebug feeds:
    split diagnostics, tilesUsed/Free, gpuTerrainTiles/Blocks/Tris from GC_feedback.
  - PORT-REVIEW deviations: reset() uploads cpuTiles.data() (extract uploaded the vector
    OBJECT — UB); no `#pragma optimize off`; no buffer_terrain UAV counter (nothing
    increments it); indirect-arg/tileCenters/feedback buffers created explicitly ZEROED
    (Diligent doesn't zero-init; startVertexLocation relies on it); tileFbo single-mip
    (extract's 8 mips unused); cull NONE on terrain draw until winding is verified;
    dir concat via filesystem join (extract needed a leading '/' in dirRoot);
    terrainMode forced off vegetation-mode early-out; fscanf %u for uint fields.
  - Known risks (check first if broken): RWTexture2D<float3> gOutNormals on R11G11B10
    without vk::image_format annotation (legacy shipped the same — watch VK validation);
    56-buffer bind count of tileBuildLookup on VK; BC6H PBR array is created but never
    written (flat PS doesn't sample it); Step2 renders into sRGB swap chain directly
    (colours will look different from the future HDR path).
- 2026-08-03: step 3 executed (warm-context main agent), not yet compiled:
  - **Shaders** (extract = authority; files whose legacy twin differed ONLY by
    known-good DXC edits were taken from `legacy/hlsl` after diffing each one):
    NEW hlsl/PBR.hlsli + material.hlsli + render_Common.hlsli (legacy = extract,
    include-order flip only), hlsl/atmosphere/{compute_fogCloudAtmosphereCommon.hlsli,
    noise.inc (+`#define noise gnoise`), compute_volumeFog.hlsli (gCfd ParameterBlock
    flattened to 12 named Texture3D + explicit `cbuffer gCfdParams : register(b4)`),
    compute_sunlightInAtmosphere.hlsl (verbatim), compute_volumeFogAtmosphericScatter.hlsl
    (float4 UAVs + [[vk::image_format]] per F17)}, hlsl/terrain/{materials.hlsli
    (PB->flat, inert until step 5), gpuLights_functions.hlsli (verbatim),
    compute_tileEcotopes.hlsl (typed RWTexture2D + flat gmyTextures_T[256]),
    compute_bakeFloodfill.hlsl (typed UAVs + read-modify-write for the .a
    swizzle-store), compute_bc6h.hlsl (verbatim) + _functions.hlsli (HLSL2021
    select() fix), render_triangles.hlsl (verbatim)}. render_Tiles.hlsl replaced
    by the FULL original psMain (verbatim + the gConstColor debug block);
    Step2StubCB deleted. compute_tonemapper.hlsl: original 3-vertex fullscreen
    triangle kept (the legacy 6-vertex quad was a pre-F22 workaround); `hdr`
    u0 -> t1 (bogus register annotation); +debugView member [PORT-DEBUG] driven
    by the existing tonemapperView UI. Dead-in-the-original
    compute_volumeFogLights/SmokeAndDust NOT ported (broken include upstream,
    no load site - catalog atmosphere_shadows.md).
  - NEW src/core/atmosphere.h/.cpp + volumeFogPhaseFunctions.cpp (verbatim data
    table): FogVolume (64x32x256 R11G11B10F far volume; dormant mainNear/
    parabolicFar kept allocated ~130 MB - step-6 candidate), per-member cbuffer
    sets exactly as the extract, sunlight LUT dispatch (16,8), scatter (8,4,1).
    **PORT-REVIEW find: the extract left most fogAtmosphericParams /
    FogVolume::m_Slice* members UNINITIALIZED and survived only because
    make_unique<Earthworks_4>() value-initialized the whole renderer object** -
    every member now has an explicit zero/default initializer
    (parabolicProjection != 0 would flip the fog compute into its parabolic
    path). setCamera ordering quirk + atan half-angle kept verbatim
    (do-not-fix list). cfd smoke seam STEP3-STUB: setSMOKE/setSmokeTime
    rewritten against flattened gCfd_* names, never called; 12 dummy 1x1x1
    volumes + a dummy cube for envMap bound.
  - NEW src/core/ecotope.h/.cpp: placement/serialization port; `#pragma
    optimize off` kept (author-pinned cereal-JSON issue); importBinary
    (vegetation) STEP3-STUB - P.index keeps serialized values; plantIndex/
    plantDensity as typed R32-uint buffers (new ew::Buffer::createTypedUint);
    blob-layout static_asserts on ecotopeGpuConstants (1200 B, ect at 48).
    Dialog-based load()/save() (editor GUI flows) not carried.
  - terrain.h/.cpp: `_shadowEdges` ported (solve/load/solveThread verbatim);
    PORT-REVIEW deviations: ~270 MB arrays heap-allocated, atomic flags,
    owned + joined thread (launchSolveThread), the fragile nested-comment
    experiment block (extract 181-293) dropped with a pointer instead of
    copied. Ecotope bake block reinserted at its exact splitChild position
    (gLowresHgt/plantIndex/plantDensity/texture-array rebinds +
    ecotopeGpuConstants blob, dispatch gated on numEcotopes>0); bc6h copy_PBR
    block reinserted (RGBA32U 64^2 scratch -> compressed_PBR_Array BC6H slice
    copy). Skydome: cube.fbx via Assimp (requireFile'd, F24), triangleShader +
    rasterstate/blendstateSplines pulled forward from init_TopdownRender,
    gSky = alps_bc.dds or a dummy cube (Steg ships no skies/).
    updateShaderConstants = full fan-out (terrainShadow + LightsCB sun basis /
    screenSize / fog_far_*). **mEcosystem.load was COMMENTED OUT in the
    extract** - re-enabled data-driven (scan <dirRoot>/ecosystem/*.ecosystem;
    Steg has none -> pass dormant, identical to shipped behaviour). gSmpLinear
    explicitly bound (extract relied on a Falcor default sampler). Remaining
    STEP2-STUB markers renamed STEP3-STUB.
  - Earthworks_4: extract wiring restored - atmosphere.onLoad BEFORE
    terrain.onLoad, texture handoff after; hdrFbo (R11G11B10F + D24S8) +
    half-res hdrPreviousFrame on resize; scene -> hdrFbo -> tonemapper
    (drawInstanced(3,1), ACES + 33^3 LUT) -> swap chain -> half-res blit;
    bypassHdr/tonemapper toggles honoured; colorCube repacked RGB32F->RGBA32F
    (RGB32F is not filterable) with an identity-LUT fallback (requireFile'd);
    shadowEdges load prefers <dirRoot>/elevation/root4096.bil and falls back
    to gis/_export/ (F23 - Steg only has the latter); shadow texture RG32F
    4096^2; thread handoff uploads texture + global_sun_direction ATOMICALLY
    together; debug-panel sun-angle DragFloat + re-solve button
    (gDebug.toggles.shadowSunAngle/shadowResolve) replaces the stripped GUI.
  - ew:: layer additions (kept in layer style): Texture::createFromFile
    (Diligent TextureLoader; 1x1 transparent-black fallback),
    Texture::createCube, Buffer::createTypedUint,
    GpuContext::unbindRenderTargets; copySubresource now unbinds RTs first
    (silences the "bound as render target will be unset" Info; also called
    before compute_TerrainUnderMouse samples the hdr backbuffer).
  - Fix round after the first build (developer-verified): the whole scene came up
    as the clear colour with 'bypass HDR' being the only thing that brought terrain
    back — the tonemapper's fullscreen triangle was BACK-CULLED (P10; the extract
    borrowed graphicsState's CullMode::None rasterizer, which the port had dropped).
    Fixed with an explicit cull-NONE + depth-disabled state on postProcess.tonemapper.
    Everything else came up correct on the first try on both APIs.
  - Known risks (check first if broken): first frames use the F26 shadow
    placeholder (dark patches on coarse LODs) until the first CPU solve lands
    (~seconds); RGBA32U->BC6H copySubresource relies on Diligent accepting the
    format-compatible CopyTexture (the previous port shipped the same copy);
    render_Tiles psMain is the original triple-experiment shader (discarded
    lightLayer cost included - do not "clean up", catalog section 9.5);
    render_triangles hardcodes 2550x1440 for the sky bicubic UV (original
    behaviour, catalog open question 8); tileEcotopes/bakeFloodfill UAVs carry
    no vk::image_format annotations (same as the shipped step-2 tile chain).
- 2026-08-04: step 4 executed (warm-context main agent), not yet compiled:
  - **Shaders** (extract = authority): NEW hlsl/terrain/
    render_vegetation_ribbons.hlsl (1212-line extract verbatim incl. the NEWER
    camRight/camUp/toneMap cbuffer members + pointSprite GS path; single
    deviation: ParameterBlock textures.T[4096] -> flat `textures_T[4096]`),
    render_tile_sprite.hlsl (same PB flatten), compute_vegetation_clear/lod/
    sortCombine.hlsl (lod: cbuffer default-initializers stripped [DXC]),
    compute_clipLodAnimatePlants.hlsl (verbatim),
    compute_sampleRGBtoPixel.hlsl (groupshared initializers stripped [DXC];
    loaded at plants onLoad - a missing shader file is FATAL in the ew layer),
    extractTextures.hlsl (verbatim copy).
  - NEW src/core/: ribbonBuilder.h/.cpp (pack() bit-exact incl. 81.487/81.17
    quantizers, inverted startBit, block-boundary duplication + leafRoot
    fixups), vegetationBuilder.h/.cpp (~4.6k lines) + vegetationBuilder_Trees.cpp
    (Grove OBJ import), PerlinNoise.hpp (verbatim), terrafector.h/.cpp
    (STEP4-NOTE minimal seed: rootFolder + getRelative/cleanPath +
    archive_float* - step 5 grows the real classes here).
  - **Include scheme**: terrain.h includes ecotope.h -> ribbonBuilder.h ->
    vegetationBuilder.h after the hlsli block (hlsli files have no include
    guards); shaderLightBuffer moved back to vegetationBuilder.h (its original
    home). veg .cpps include terrain.h (ecotope.cpp pattern).
  - terrain wiring: plants_Root member (public - Earthworks_4 hands the
    atmosphere textures over BEFORE terrain.onLoad); terrainSpiteShader load +
    binds + per-frame billboard draw (drawArgs_quads, main view);
    compute_clipLodAnimatePlants load + dispatchIndirect(dispatchArgs_plants)
    at its exact pre-terrain-draw position; step-3 stub buffers deleted, real
    plants_Root buffers bound after plants_Root.onLoad (extract order 1010-1051);
    buffer_clippedloddedplants real (32 MB, consumers still drawless);
    ecotopeSystem::pVegetation wired; ecotope.cpp importBinary STEP3-STUB
    completed (P.index resolves through importBinary; -1 keeps serialized value).
    updateShaderConstants full fan-out (terrainSpiteShader LightsCB block +
    plants_Root.updateShaderConstants with hdrPreviousFrame -> JHFAA).
  - PORT-REVIEW deviations (mechanical, all tagged in-code): Falcor Vars() ->
    ew named binds; 4096-texture arrays via setTextureArray (pools already
    sized, F13/F18); rmcv matrix feeds -> glm::transpose uploads (ewCamera.h
    convention - includes the frustum matrices and the element-copy sites);
    vegetation feedback readback = ReadbackBuffer latency ring instead of the
    extract's EVERY-FRAME map(Read) stall (P5 reviewed: struct is frame-global
    counters, no recyclable slots - ring age surfaced in the debug panel);
    2 MB/0.5 MB stack arrays heap-allocated; cereal loads try/catch loud;
    importBinary missing-file/overflow guards return -1 (dormant, never crash);
    captureToFile = STEP4-STUB (bake/export authoring outputs log-and-skip);
    renderGui_Lodding/Baking bodies KEPT (STEP4-NOTE editor-deferred, Falcor
    Gui* font plumbing dropped, ImGui power-slider arg -> SliderFlags);
    P9 explicit initializers (plantBuf, sprite_material mirror, buildSetting
    ::root, plant zero-fill of GPU buffers Diligent doesn't zero).
  - Faithful-but-noted: 8192^2 shadowFbo (~320 MB) + rgbFbo allocated
    unconditionally like the extract (SAMPLE_MODE research target - step-6
    gating candidate); the terrain-mode draw order oddity (veg clear zeroes
    the drawArgs clipLod just filled - catalog vegetation.md open question 3)
    ported VERBATIM, so terrain-driven ribbons draw 0 instances even with data.
  - Debug panel: new "Vegetation (feedback)" block - instances/blocks/
    frustum-discards/feedback-age + the numBillboard=13 clear-shader sentinel
    as a "compute chain alive" proof.
  - Deferred in-step: render_ribbons RV6 grass + veghumanShader (drawless even
    in the extract - flagged STEP4-STUB, developer sign-off before dropping);
    Sprites.cpp (step 6); cascade shadow drawArg variants (never loaded).
- 2026-08-04: step 4 DEVELOPER-VERIFIED, first try (no fix round):
  - VK: terrain renders correctly; fps not measurable (pre-existing vsync-off
    issue, step-6 item). D3D12: ~900 fps (baseline uncertain - step 3 vs
    earlier; re-measure properly in the step-6 perf pass).
  - Dormant-but-healthy confirmed: veg feedback all 0, billboard sentinel 13,
    feedback age 2 frames, 1 plant + 1 billboard submission/frame, no PSO
    failures. Total VRAM ~800 MB (accepted for the 40x40 km area; the 8192^2
    shadowFbo gating stays a step-6 candidate).
  - REVISIT once plant data exists: visual validation + the catalog
    vegetation.md open questions (terrain-mode clear-vs-clipLod draw order OQ3,
    importBinary offset accounting OQ7, 0x2ff material mask OQ1).
- 2026-08-04: step 5 executed (warm-context main agent), not yet compiled.
  **BINDLESS: developer chose the faithful descriptor-array path at launch** -
  `ew::setTextureArray` binds the used prefix, the layer dummy-pads the rest of
  the declared 4096 slots (F15); `// STEP5-NOTE` at
  `materialCache::setTextures` documents the bindless upgrade path.
  - **Shaders** (extract = authority; legacy diffed per file = known-good DXC
    edits only): NEW hlsl/terrain/render_splineTerrafector.hlsl,
    render_meshTerrafector.hlsl, render_spline.hlsl - verbatim except the
    [DXC] ParameterBlock flatten (`Texture2D<float4> gmyTextures_T[4096]`
    declared BEFORE the materials.hlsli include). Legacy's dbgTagSuspicious
    (port-debug, not original) NOT carried. materials.hlsli got an include
    guard (now also compiled by MSVC via terrafector.h).
  - NEW src/core/: terrafector.h/.cpp (full system replacing the step-4 seed:
    triVertex/tileTriangleBlock/lodTriangleMesh/loadCombiners/materialCache/
    terrafectorEditorMaterial/terrafectorElement/terrafectorSystem;
    **TF_material byte contract static_asserted - the "464 bytes" comments are
    STALE, real size is 512 (15x16 + 32 subMaterials + 240 ecotopeMasks),
    offsets 240/272 pinned**; JLogger dropped, fprintf(_logfile) fabric kept -
    terrain.onLoad opens `earthworks_terrafectors.log` in cwd),
    roads_bezier.h/.cpp, roads_road.h/.cpp, roads_Intersection.h/.cpp,
    roads_materials.h/.cpp, roads_physics.h/.cpp (bezierLayer ctor lives
    here; ODE_bezier::bezierBounding stays unpopulated - catalog OQ1, ported
    faithfully), roadNetwork.h/.cpp (external-version cereal load, version
    103, upgrade() reworked dialog-free). roads_cubicDouble.* (empty
    include-only stubs) not carried per catalog.
  - terrain wiring: init_TopdownRender at its extract position (zeroed
    sb_Terrafector_Materials 2048x512 B, 3 bake/overlay shaders, depth states,
    **blendstateRoadsCombined incl. the RT0 One/OneMinusSrcAlpha "??? hoekom"
    override**, zeroed spline buffers); splitRenderTopdown reinserted at the
    exact STEP3-STUB position (after bicubic, before the height_Array copy)
    with the full priority-ordered stack (bakeLow -> road bakeOnly -> bakeHigh
    -> mesh LOD6/4/2 -> overlay -> road LOD bins -> stamps -> _top);
    **bake camera = transpose(orthoLH * axis-swap view), PORT-REVIEW comment
    derives why transpose(P*V) is byte-identical to the original's rmcv
    element copy**; update() update_roads block (dynamic road, testHit,
    isDirty spline upload + bezierRoadstoLOD(4)); terrafectors.loadPath +
    **data-driven road/stamps load from lastfile.road/.stamps (PORT-REVIEW:
    the extract only loaded roads via GUI buttons)**; stamps
    (stamp_to_Bezier/currentStamp_to_Bezier/allStamps_to_Terrafector -> LOD7
    mesh combiner); render_spline 3D overlay + terrafector-mode stamp preview
    in onFrameRender (after skydome, extract order); destructor saves
    road/stamps filenames.
  - **y=0 checklist enforcement**: (a) ew::pixelShader logs the effective
    per-RT blend at every independent-blend PSO build + **H6 CONFIRMED IN
    DILIGENTCORE and fixed: DeviceFeatures::IndependentBlend defaults DISABLED
    and EngineFactoryVk only enables VK independentBlend when ENABLED - the
    app shell now requests it on VK and D3D12 (very plausible root cause of
    the old "terrafectors broken on Vulkan")**; (b) TF_material
    static_asserts (sizeof 512 + key offsets, C++ mirror == HLSL layout);
    (c) debug probe: `ew::GpuContext::debugReadTexelR32F` (1x1 staging + full
    stall, debug only) reads the elevation centre before/after each bake
    while the panel counter runs, logs "y=0 SIGNATURE" when a good height
    collapses to ~0, sticky readout in the panel; (d) requireFile-pattern
    loud errors: missing .terrafectorMaterial / .earthworks.dds / absolute-
    elevation material with failed elevation textures / road material outside
    rootFolder / LOD-bin file-write failures.
  - PORT-REVIEW deviations (all tagged in-code): bSplineAsTerrafector is
    extract-default FALSE and editor-key-toggled - driven from a new debug
    panel section instead, **default ON** so the live bake is testable
    (+ per-stage toggles 1-8 for bisecting a broken bake + a "rebake all
    tiles" button = reset(true)); bezierRoadstoLOD keeps the extract quirk of
    uploading LOD bins INSIDE the `if (file)` blocks - onLoad now
    create_directories(<dirRoot>/bake) and failure logs loudly; LOD-bin
    window clamped to the grid (extract could index past lodN[][] for roads
    near the terrain edge); catalog OQ7 (endInside/endOutside use perpStart)
    ported AS-IS with a note; getDone() div-by-zero guarded; roadNetwork.load
    signed/unsigned -1 wrap made explicit; loadToGPU clears stale buffer
    refs in empty grid cells; Compressonator shell-out gated on the exe
    existing; thumbnails gated on file existence; #pragma optimize off (x2)
    and JLogger and the stray roads_physics file-scope `layers` vector not
    carried; P9 explicit initializers on the uninitialized _constData fields.
  - Editor flows STEP5-STUB'd (log-and-return): file-dialog load/save/
    upgrade pickers, exportBinary dialog, saveType/loadSelected/
    loadCompleteRoad, reFindMaterial relocation dialog. mSpriteRenderer
    marker calls stubbed in place (sprites = step 6). Offline bake
    (bake_start/bake_frame/bake_Setup/bake_RenderTopdown/sceneToMax)
    deferred STEP5-STUB: no live trigger in the extract (GUI-stripped) and
    it needs ew texture readback + JP2 re-encode; terrain.h note pins the
    bake_RenderTopdown gate difference (offline ALWAYS bakes roads).
  - Debug panel: new "Terrafectors / roads (step 5)" section (bake roads
    toggle + rebake, 3D overlay toggle, 8 bake-stage checkboxes, elevation
    probe button + sticky readout, RT0-blend A/B); scene counts reuse
    staticSplines/dynamicSplines.
  - Known risks (check first if broken): VK dynamic descriptor pressure -
    the bake adds up to ~10 commits x 4096-entry arrays per split frame on
    top of step 4's consumers (pool chunk is 32768, Diligent allocates more
    chunks on demand; D3D12 dynamic heap budget 786432 should hold, watch
    the end-of-run heap stats); tfBakeRoads default ON deviates from the
    extract's shipped behaviour (roads pre-baked into JP2) - flip it off to
    compare against pristine terrain; Steg's gameroot may or may not ship
    roads/steg_010.roadnetwork + terrafectors/ + _resources material data -
    every miss is a loud log, not a crash; the terrafector textures want
    precompressed .earthworks.dds next to the sources (no Compressonator on
    this machine).
- 2026-08-04: step 5 fix round 1 (first launch crashed in
  terrafectorSystem::loadPath): root cause **P14** - Steg's pre-v101
  forests.terrafectorMaterial threw through cereal's hardcoded-version read
  (port 2's band-aid was a path-exact early-return). import() is now
  exception-guarded (loud + no-op material) and 001_forest.fbx is skipped
  outright (developer decision - stale forest data until the plant-data
  update; STEP5-STUB). Also fixed: MAP_FLAG_DO_NOT_DISCARD -> MAP_FLAG_NONE
  in the debug readback (no such Diligent flag). NOTE: the run's
  efx_run_stdout.log turned out to be from the LEGACY port binary
  (Mainhardter Wald + compat-layer messages) - its errors do not apply to
  this build.
- 2026-08-04: step 5 fix round 2 (first working VK run: roads visibly bake -
  "milky" flattened beds; blend log confirms the RT0 override on both bake
  PSOs; 2882 beziers / 3396 layers / 1392 bake-only uploaded):
  **P15** rootFolder backslashes made every .roadMaterial fail the
  rootFolder prefix test -> material 0 (normalized once at assignment), and
  P14 extended: pre-v101 materials are EVERYWHERE in the gameroot - import()
  now retries as v100 (lossless) instead of no-op-ing them. Both fixes
  should replace the milky beds with actual asphalt/markings, provided the
  .earthworks.dds textures are synced. (Correction: steg.ecosystem was in the
  gameroot all along - developer confirmed the rsync folder unchanged; plant
  binaries still point at F:\ and stay dormant/loud.)
- 2026-08-04: step 5 developer test results: **D3D12 renders terrafectors +
  roads correctly ("perfectly clean"), ~850 fps.** Two open items carried
  past the step-5 commit:
  1. **VK-only: terrafectors/roads render SOLID WHITE** (shapes + flattened
     beds correct -> RT0 elevation path fine; identical symptom existed in
     port 2 despite its different binding layer). Prime suspect:
     NON-UNIFORM descriptor indexing - gmyTextures_T[MAT.*Texture] uses a
     per-pixel-varying index without NonUniformResourceIndex(); UB that NV
     D3D12 tolerates and NV Vulkan does not (undefined descriptor often
     samples as 1.0 = white). Queued experiment (post-commit):
     NonUniformResourceIndex() at the materials.hlsli/bake-shader sample
     sites + EngineCI.Features.ShaderResourceRuntimeArrays = ENABLED (the
     descriptor-indexing feature chain is default-off - same P12 pattern).
     NOTE: the white was observed BEFORE fix round 2 (all materials were
     no-ops then) - re-test first.
     **Experiment RESULT (2026-08-04 23:29 run): DISPROVEN — still solid white.**
     Proof the fix was live: deployed gameroot materials.hlsli identical to the
     edited repo file, DXC warnings reference the post-edit line numbers
     (materials.hlsli:184 for SV_Target0), and NO PSO error about the NonUniform
     decoration (PipelineStateVkImpl errors loudly when the feature is missing
     -> ShaderResourceRuntimeArrays was active). Facts that kill adjacent
     theories: ew dummy pad texture is BLACK zeros and ALL 4096 array elements
     are written (white != unbound descriptor); CommitShaderResources uses
     TRANSITION mode everywhere (weakens VK-image-layout theory); no
     "no resource bound" warnings for the bake shaders. Remaining discriminator
     experiments (developer-driven, zero/low code): (1) panel toggle
     tfShowRoadSpline OFF - white mesh SHAPES are bake-only, white ROADS could
     be the 3D overlay on top; (2) VK run with `-validation 1` command line -
     VVL names invalid descriptors/layouts instantly (this run had it off);
     (3) if needed, a temp debug tint in solveElevationColour to discriminate
     MAT-content-wrong vs texture-read-returns-1.0 on VK. The NonUniform change
     is kept for now (spec-required UB removal, zero D3D12 impact) - revert
     one-liner below if the developer prefers.
     **Experiment APPLIED post-ba76f18 (uncommitted, awaiting VK test)**:
     `TF_TEX(idx)` = `gmyTextures_T[NonUniformResourceIndex(idx)]` macro in
     materials.hlsli wrapping all 11 material-driven sample sites (covers both
     bake shaders via the shared functions) + the 2 direct sites in
     render_spline.hlsl; `EngineCI.Features.ShaderResourceRuntimeArrays =
     ENABLED` in the app shell's VK block only (the D3D12 factory force-enables
     it unconditionally, EngineFactoryD3D12.cpp:815). Revert = `git checkout --
     EarthworksFX/hlsl/terrain/materials.hlsli
     EarthworksFX/hlsl/terrain/render_spline.hlsl
     EarthworksFX/src/app/EarthworksFXApplicationBase.cpp`. If verified: same
     latent UB exists in the step-4 vegetation shaders
     (render_vegetation_ribbons.hlsl, render_tile_sprite.hlsl:
     textures_T[MAT.*]) - dormant today, fix when vegetation goes live.
  2. **Rare hang on BOTH APIs** (reproduced once on VK after long runs; not
     mode-related per developer). VK side shows DynamicHeapSize-exhaustion
     warnings when it happens (GPU falling behind - symptom, not cause).
     Uninvestigated; panel stage toggles are the bisect tool.
