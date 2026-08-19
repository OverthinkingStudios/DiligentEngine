# Cross-cutting: Conventions, App-Shell Wiring, Shader Wrappers, File Formats, Falcor API Surface, Threading

> Concept-Catalog doc (task.md §5, subsystem 7 + app shell). Sources:
> `port_effort_3/source_extract_3/` (stripped originals — all extract line refs below),
> `EarthworksFX/src/app|core/` (previous port, drift reference),
> `EarthworksFX/BRINGUP_NOTES.md` (F-findings) + `PROJECT_OVERVIEW.md`.
> Audience: cold-context re-porting agent + reviewer. Extract paths are relative to
> `port_effort_3/source_extract_3/`.

---

## 1. Purpose & data flow

This doc covers everything that is not one subsystem: world/engine conventions, the
`Earthworks_4` app shell (`Earthworks_4.cpp/.h`, 402+106 lines), the two tiny shader
wrapper classes (`computeShader.*`, `pixelShader.*`, ~140 lines — the GPU-API funnel for
the entire codebase), persistence/file formats, the global Falcor API inventory, and the
threading map.

### Frame flow (original, `Earthworks_4.cpp`)

```
onFrameRender(ctx, pTargetFbo)                          [Earthworks_4.cpp:251]
 ├─ onFrameUpdate(ctx)                                  [:200]
 │   ├─ if shadowEdges.shadowReady: updateTextureData(terrainShadowTexture, shadowH);
 │   │    global_sun_direction = shadowEdges.sunAng     [:204-213]  (CPU shadow thread handoff)
 │   ├─ build shaderLightBuffer (sun basis + fog params, see §5)  [:219-232]
 │   ├─ terrain.updateShaderConstants(hdrPreviousFrame, lightBuffer)  [:234]
 │   ├─ terrain.setCamera(CameraType_Main_Center, toGLM(view), toGLM(proj), pos, true, 1920)  [:236]
 │   ├─ terrain.update(ctx)          (quadtree split/merge, streaming, tile bakes)  [:238]
 │   └─ atmosphere: setSunDirection, getFar().setCamera(camera),
 │        computeSunInAtmosphere, computeVolumetric     [:241-244]
 ├─ if terrain.fullResetDoNotRender: skip EVERYTHING below  [:257]
 ├─ clear pTargetFbo AND hdrFbo (color (0,0,0,1), depth 1.0)  [:262-267]
 ├─ terrain.onFrameRender(ctx, hdrFbo, camera, viewport3d)    [:269]   — the whole 3D scene
 ├─ tonemapper: hdrFbo color0 + colorCube LUT → pTargetFbo, drawInstanced(3,1)  [:271-279]
 ├─ blit hdrFbo color0 → hdrPreviousFrame (HALF-res)          [:281]   — next-frame feedback
 └─ if refresh.minimal: Sleep(20)                             [:283-286] — ~15 fps power mode
```

`onFrameUpdate` is NOT called by the framework — `onFrameRender` calls it first thing
(`:253`). Update and render are one serial per-frame path on the render thread.

### onLoad order (load-order dependencies are real — keep the sequence)

`Earthworks_4::onLoad` (`Earthworks_4.cpp:40-158`):

1. `graphicsState` + default blend (SrcAlpha/OneMinusSrcAlpha add), depth on/write on,
   cull **None** (`:48-61`).
2. Camera create: depthRange **0.1 .. 40000**, aspect 1920/1080, focal **15 mm**,
   pos (0,900,0), target (0,900,100) → looking +Z at 900 m altitude (`:63-68`).
3. `atmosphere.onLoad` FIRST (`:81`), then hand its textures to vegetation:
   `terrain.plants_Root.{inscatter,outscatter,sunlightTexture} = atmosphere...` (`:85-87`)
   — plants get atmosphere by **member pointer copy before terrain.onLoad**, because the
   vegetation shaders don't exist yet.
4. `terrain.onLoad` (`:101`) — loads lastFile.xml/terrainSettings.json, all shaders,
   tile pools (see §4 and the terrain catalog doc).
5. Atmosphere → terrain shader texture wiring AFTER terrain.onLoad (`:104-118`):
   `terrainShader`/`terrainSpiteShader` get `gAtmosphereInscatter`, `gAtmosphereOutscatter`,
   `SunInAtmosphere`; `triangleShader` (skydome) gets `gAtmosphereInscatter_Sky`.
6. `camera.bin` restore: raw `fread` of `CameraData` (`:124-130`).
7. `earthworks4_presets.xml` restore via cereal XML, `serialize(archive, 100)` (`:132-136`).
8. Tonemapper load (`compute_tonemapper.hlsl`, vs/ps, TriangleList) + color-cube LUT
   `<dirResource>/colorcubes/ColdChrome.cube` (`:138-140`, loader `:162-193`).
9. Shadow solver setup (`:142-156`): `shadowEdges.load(dirRoot + "/elevation/root4096.bil", ...)`
   ⚠ (for Steg the real file is `gis/_export/root4096.bil` — F23; the extract carries the
   author's own "// BAD - for STEG" flip), `sunAngle = 0.95605f`, `dAngle = 0.0001f`,
   `requestNewShadow = true`; create `terrainShadowTexture` 4096² **RG32Float**
   (UAV|SRV) initialized with the CPU placeholder `shadowH`; **detach** `solveThread`;
   `atmosphere.setTerrainShadow(texture)`.

`main()` (`:360-402`): opens `log.txt` (global `FILE* logFile`, `:36`) + JLogger
(`log.cpp`), window 2560×1340 resizable, `-allscreens` flag parsed (effect commented
out), `Sample::run`.

### Resize (`onResizeSwapChain`, `:335-356`)

- `viewport3d` = full window, depth 0..1.
- `hdrFbo = Fbo::create2D(w, h, D24UnormS8 depth + R11G11B10Float color0)` (`:347`).
- `hdrPreviousFrame = Texture::create2D(w/2, h/2, R11G11B10Float, BindFlags::AllColorViews)`
  (`:349`) — **half res**, needs RTV (blit target) + SRV.
- `screenMouseScale/Offset` computed (identity here; hooks for sub-viewport layouts).

### Shutdown (`onShutdown`, `:292-307`)

`terrain.onShutdown()` (a no-op, `terrain.cpp:1328`; the REAL terrain persistence is in
`~terrainManager` which writes `lastFile.xml`, `terrain.cpp:379-387`) → write `camera.bin`
(raw fwrite of `CameraData`) → write `earthworks4_presets.xml`.

### Input

- `onKeyEvent` (`:311-324`): forwards to terrain first; `V` toggles `refresh.minimal`.
- `onMouseEvent` (`:328-331`): forwards to
  `terrain.onMouseEvent(evt, screenSize, screenMouseScale, screenMouseOffset, camera)`.
  Falcor delivers mouse pos **normalized [0,1]** — core code gates on `pos.x > 0 && < 1`.

---

## 2. Core tricks & clever mechanisms (must not be lost)

| Trick | Where | Why it matters |
|---|---|---|
| **hdrPreviousFrame half-res feedback** | blit `Earthworks_4.cpp:281`; consumed as `gPreviousFrame` (t23) by `JHFAA_alpha()` `hlsl/render_Common.hlsli:75,215-222`; bound in `terrain.cpp:1758-1759` (ribbons, sprites) and `vegetationBuilder.cpp:4000-4001` (billboards, vegetation) | Vegetation/ribbon alpha "fake AA": pixels with alpha < 0.9 lerp toward LAST frame's tonemap input. Kills alpha shimmer for ~free. Needs the blit every frame and half-res linear-clamp sampling; drop it and vegetation edges sparkle. |
| **CPU semi-static terrain shadow** | `_shadowEdges` `terrain.h:76-92`, solve `terrain.cpp:104-155`, thread handoff `Earthworks_4.cpp:204-213` | A 4096² RG32Float texture `(shadowLineHeight, softDepth)` solved on a background thread by marching sun rays across the heightfield (texel = 9.765625 m = 40000/4096). Whole-terrain soft shadows (terrain, buildings, fog) for one texture sample; there is NO shadow map for terrain. Unshadowed sentinel = `(-5000, 0)`; the load-time placeholder `(height-5, 0)` is only pre-solve filler (F26 explains why rendering from it looks "almost right"). |
| **Color-cube tonemap LUT** | `loadColorCube` `Earthworks_4.cpp:162-193` | 33×33×33 `.cube` file (5 header lines skipped, then z-fastest triple loop) → RGB32Float 3D texture, sampled in `compute_tonemapper.hlsl` with `sampler_Clamp`. RGB32Float is non-filterable on some backends — the previous port survived because the shim promoted formats; verify filterability or promote to RGBA32F/RGBA16F. |
| **One indirect-args convention everywhere** | `pixelShader::renderIndirect` `pixelShader.cpp:47` | ALL indirect draws are **non-indexed**, 16-byte D3D12 `DrawInstancedArgs`, offset `_startArg * 16` where `_startArg` is a `CameraType` view index. Mapping these to DrawIndexedIndirect was a historical invisible-terrain bug (PROJECT_OVERVIEW; F6). |
| **Shared 128-quad index pattern IB** | `pixelShader::load` `pixelShader.cpp:26-38` | Every pixelShader VAO carries a static 768-index R16 IB `(0,1,2, 1,3,2), +2 per quad`. Used ONLY by `drawIndexedInstanced` (splines/ribbons build quads from SV_VertexID pairs); `drawInstanced` must stay NON-indexed regardless of the attached IB (Falcor semantics — F22 was exactly this confusion). The IB is written once → must NOT be a dynamic/discarded buffer (F16 device-loss). |
| **`refresh.minimal` power mode** | `Earthworks_4.cpp:283-286,319`; persisted in presets | `Sleep(20)` at end of frame → ~15 fps idle mode for editor use, toggled by `V`. Trivial but user-visible; keep the toggle + persistence. |
| **Camera state raw persistence** | `camera.bin`, `:124-130,296-301` | Raw `CameraData` fwrite/fread. Any change to a field's meaning silently survives restarts (BRINGUP "interesting behaviour"; the port force-writes `FrameHeight = 24` after fread). If the new port's camera struct differs AT ALL, version or drop this file. |
| **Atmosphere-before-terrain texture plumbing** | `Earthworks_4.cpp:81-119` | The dependency order at load (atmosphere → plants pointers → terrain.onLoad → shader texture binds) is the entire "who owns which texture" contract between the three subsystems. Re-wire deliberately. |

---

## 3. Invariants & conventions (world/engine)

Mined from extract code + PROJECT_OVERVIEW + BRINGUP F-findings. F-numbers cited where the
finding encodes ORIGINAL semantics (not a compat-layer bug).

| Convention | Value | Evidence |
|---|---|---|
| Handedness | **Right-handed**, glm math throughout core | F20 (making the shim RH un-mirrored the world); `toGLM(view)` ≡ `glm::lookAt` |
| Up / compass | **UP = +Y, NORTH = −Z, EAST = +X** | F20.1 (two independent proofs: `terrainManager::writeGdal` negates Z for northing; real-map comparison) |
| World extent | terrain spans **[−20000, +20000] m** in X/Z (size 40000, offset −20000) | `jp2Map::set` defaults `terrain.h:292`; `_terrainSettings.size = 40000` `terrain.h:169`; Steg terrainSettings.json |
| Units | meters, seconds; sun angles in radians (`sunAngle`, `sunAng` unit vector) | `terrain.cpp:89-93` |
| Depth range | **[0,1]** (D3D/Vulkan). Original Falcor globally defined `GLM_FORCE_DEPTH_ZERO_TO_ONE`; must be a PUBLIC compile definition or `glm::unProject` (mouse picking, `terrain.cpp` ~3100) and ortho bakes silently go [−1,1] | F3.1, F20 (CMake note) |
| Winding | Front = **CCW** (Falcor default). With the RH camera, CW-default PSOs back-cull the terrain | F20 ("winding flip is the trap") |
| Projection / FOV | Falcor film-back convention: `fovY = 2·atan(0.5·frameHeight/focalLength)` with **frameHeight = 24 mm**, focal 15 mm → ≈ 77.3°. Feeds the `lod_Pix > 150` split heuristic — wrong FOV skews LOD everywhere | F3.2 |
| ⚠ Atmosphere half-angle exception | `FogVolume::setCamera` uses `atan(0.5·h/f)` **without** the factor 2 — it wants the HALF angle. Original code, do not "fix" | `atmosphere.cpp:36-37`; BRINGUP "interesting behaviour" |
| View-matrix decomposition | `W = toGLM(view.getTranspose()); dir = W[2] * −1` (backward → forward). ANY code that decomposes the view matrix into basis vectors assumes glm RH lookAt | `atmosphere.cpp:29-33`; F19/F20 lesson |
| Camera matrices to shaders | CPU passes `getViewProjMatrix().getTranspose()`; HLSL does `mul(float4(pos,1), viewproj)` with default column-major cbuffer interpretation | PROJECT_OVERVIEW "Matrix & camera conventions" |
| Camera defaults | near 0.1, far 40000, start pos (0,900,0) | `Earthworks_4.cpp:64-68` |
| Sun default | `global_sun_direction = (0.96593, −0.25882, 0)` — 15° above horizon, from +X; overwritten by shadow thread (`sunAng` from `sunAngle=0.95605` ≈ 54.8°) after first solve | `Earthworks_4.h:38`, `terrain.cpp:89-93`, `Earthworks_4.cpp:146` |
| View indices | `enum CameraType` `terrain.h:242-262`: Main_Left=0, **Main_Center=1**, Main_Right, Rear_*, Cascade_0..3, Cube_1..6, Parabolic_low/medium, MAX=18. Indirect-arg/lookup buffers are indexed per view; main view byte offset = 1·16 | F6 |
| Tile texture size | `tile_numPixels = 256` (`hlsl/terrain/terrainDefines.hlsli:6`); tile pool `numTiles = 997` (`terrain.h:477`) | |
| Heightfield texel | 4096² over 40 km → **9.765625 m/px**, used as a literal in shadow solve (`terrain.cpp:129,174`) and in the port's grid draping | |
| Mouse coords | normalized [0,1] window coords (Falcor convention); host must normalize pixels | port `SyncInput` `EarthworksFXApplicationBase.cpp:1159-1197` |
| Time source | `gpFramework->getFrameRate().getAverageFrameTime()` (ms) for GUI fps; `high_resolution_clock` for load timing (`terrafectorSystem::logTimeX`) | `Earthworks_4.cpp:363` |
| C++↔HLSL struct layout | DXC/SPIR-V aligns float3/float4x4 to 16 B, glm packs to 4 B. Any readback/upload struct shared via .hlsli needs explicit pads + static_asserts | F9 (lesson) |

---

## 4. Performance-critical details

- **`fullResetDoNotRender` gate** (`Earthworks_4.cpp:257`): while terrain does a full
  reset, NOTHING renders (not even clear→tonemap). Dropping the gate draws from
  half-initialized tile pools.
- **hdrPreviousFrame is half-res** — full-res doubles the blit + sample cost for zero
  visual gain (shader samples with linear filter anyway).
- **hdrFbo formats**: R11G11B10Float color + D24UnormS8 depth. R11G11B10 halves HDR
  bandwidth vs RGBA16F; several compute passes also write R11G11B10 UAVs (F17: needs
  `[[vk::image_format]]` on Vulkan/DXC). D24S8 may be emulated as D32S8 on some Vulkan
  drivers — fine, but keep depth [0,1].
- **One clear per FBO per frame** (`:262-267`), no redundant clears mid-frame.
- **`updateTextureData` of the 4096² RG32Float shadow texture (128 MB) happens only when
  `shadowReady` flips** (a few times per run) — never per frame. Don't poll-upload.
- **The tonemapper is the ONLY fullscreen post pass** (drawInstanced(3,1), oversized
  triangle) — the whole post stack is one draw + one blit.
- **`setCamera(..., 1920)`**: the `resolution` param (hardcoded 1920, `:236`) feeds the
  `lod_Pix` screen-space split metric. It is NOT the actual window width in the original;
  changing it rescales the entire quadtree LOD ladder (perf/visual balance).
- Indirect draws mean CPU draw-call counters prove nothing — instance counts live on the
  GPU (BRINGUP lesson; keep the GC_feedback-style readback in the new port's debug UI).

---

## 5. GPU resources & shader interface (app-shell level)

### shaderLightBuffer (`vegetationBuilder.h:91-114`) — CPU-filled, consumed by most render passes

```cpp
struct shaderLightBuffer {
    float3 sunDirection;   int   numLights;
    float3 sunColour;      float padd;      // sunColour currently unused by app shell
    float3 sunRightVector; float padd2;     // volumetric shadow projection basis
    float3 sunUpVector;    float padd3;
    float2 screenSize;
    float  fog_far_Start;  float fog_far_log_F;
    float  fog_far_one_over_k; float fog_near_Start;
    float  fog_near_log_F; float fog_near_one_over_k;
};   // 96 bytes; the padd/padd2/padd3 members ARE the float3→16B alignment. LOAD-BEARING.
```

Filled in `onFrameUpdate` (`Earthworks_4.cpp:219-232`):
`sunRightVector = normalize(cross(up, sunDir))`, `sunUpVector = normalize(cross(sunDir, sunRight))`,
fog params pulled from `atmosphere.getFar()/getNear()` (`m_logEnd`, `m_oneOverK`,
`m_params._near`). Passed BY VALUE into `terrain.updateShaderConstants` which fans it out
(`terrain.cpp:1752-1778`, also → `plants_Root.updateShaderConstants` with the shadow texture).

### App-shell-owned resources

| Resource | Format / size | Producer → consumer |
|---|---|---|
| `hdrFbo` | color0 R11G11B10Float + D24UnormS8, window size | terrain.onFrameRender → tonemapper `hdr`, blit source |
| `hdrPreviousFrame` | R11G11B10Float, **w/2 × h/2**, AllColorViews | blit(hdrFbo) → `gPreviousFrame` (t23) in ribbons/sprites/vegetation/billboards |
| `postProcess.colorCube` | 33³ RGB32Float 3D | loadColorCube → tonemapper `cube` |
| `terrain.terrainShadowTexture` | 4096² RG32Float, UAV\|SRV | shadow thread (CPU) → terrain/buildings/fog `shadow()` in `render_Common.hlsli`; also `atmosphere.setTerrainShadow` |
| tonemapper vars | `hdr`, `cube`, `linearSampler` (= `terrain.sampler_Clamp`), FBO = swap chain, rasterizer borrowed from `graphicsState` | `Earthworks_4.cpp:271-279` |

### Shader wrapper semantics (`computeShader.*`, `pixelShader.*`) — exact contract

`computeShader` (`computeShader.cpp`, 24 lines):
- `load(path)`: adds dummy define `CHUNK_SIZE=256` ("just so we can get reflection data"),
  `ComputeProgram::createFromFile(path, "main", defineList)` — **entry point always
  `main`** — then ComputeState + ComputeVars from the program.
- `add(name, val)` mutates `defineList` — only affects programs loaded AFTER the call.
- `dispatch(ctx, w, h, slices=1)`: dispatches **thread GROUPS** (w,h,slices) — callers
  pre-divide by group size.
- `dispatchIndirect(ctx, argBuffer, byteOffset)`.
- `Reflector()` exposes `program->getActiveVersion()->getReflector()`.

`pixelShader` (`pixelShader.cpp`, 62 lines):
- `load(path, vsEntry, psEntry, topology, gsEntry="")`: with GS → `GraphicsProgram::Desc`
  + `setShaderModel("6_5")` (GS passes: tile sprites, ribbons need SM6.5); without GS →
  `createFromFile(path, vs, ps, defines)`. Creates GraphicsState + GraphicsVars from
  reflection, then builds the VAO: **empty vertex layout, no VBs**, plus the 128-quad
  R16 index buffer (§2). All geometry is generated from `SV_VertexID` + structured buffers.
- `renderIndirect(ctx, argsBuffer, blendState=null, startArg=0, numArgs=1)`:
  optional per-call blend-state override, then
  `drawIndirect(state, vars, numArgs, args, startArg*16, nullptr, 0)` — non-indexed.
- `drawIndexedInstanced(ctx, indexCount, instanceCount)` — uses the quad-pattern IB,
  offsets 0.
- `drawInstanced(ctx, vertexCount, instanceCount)` — non-indexed, IGNORES the IB (F22).
- `add/remove(define)` — again only pre-load.
- Instance census (extract): 18 pixelShader + 25 computeShader members (grep
  `pixelShader \w+` / `computeShader \w+`); call-site counts:
  `dispatch` 34, `renderIndirect` 18, `drawInstanced` 21, `drawIndexedInstanced` 12,
  `dispatchIndirect` 2.

These two classes are the natural seam for the re-port: every draw/dispatch in ~38k lines
funnels through them plus ~14 direct `RenderContext` calls (§7).

---

## 6. Dependencies

- **App shell consumes**: `terrainManager` (terrain.h), `atmosphereAndFog` (atmosphere.h),
  `earthworksScene` (a stub — `earthworksScene.cpp` is 5 lines; member `scene` is unused
  scaffolding), cereal, `terrafectorSystem::logTimeX/_logfile` (logging statics),
  `JLogger`, `MonitorInfo`.
- **Provides to subsystems**: camera (view/proj + position), `shaderLightBuffer`,
  `hdrPreviousFrame`, `hdrFbo`, `global_sun_direction` (a GLOBAL defined in
  `Earthworks_4.h:38`, written by the shadow-thread handoff, read by atmosphere +
  lightBuffer), the shadow texture, tonemapping.
- **Host (current port) provides**: Diligent device/context/swap chain, ImGui, first-person
  camera → `camera->setPosition/setTarget` sync, input normalization, testflight harness.
- `Earthworks_4.h` includes `RenderGraph/RenderGraph.h` and `Utils/Video/VideoEncoderUI.h`
  — **both unused** in the extract; drop in the re-port.

---

## 7. Falcor API surface actually used (whole extract, grep-based)

Size: **~30 distinct types, ~120 distinct methods/entry points**. The heavy traffic is
concentrated in resource creation + variable binding; everything else is a thin tail.

### Types by usage (occurrences, whole extract)

| Type / API | Count | Notes |
|---|---|---|
| `ShaderVar` `Vars()[...]["member"] =` | 300 | cbuffer member assignment tree |
| `Vars()->setBuffer / setTexture / setSampler / setUav` | 178 / 130 / 33 / 3 | |
| `Vars()->getParameterBlock(...)` + `setBlob` | 20 + 64 | bulk cbuffer upload (F7 pattern!) plus `Buffer::setBlob` |
| `Texture::` (SharedPtr 83, create2D 33, createFromFile 29, create3D 3) | ~150 | plus `->getSRV` 7, `->getRTV` 7, `->generateMips` 7, `->captureToFile` 22, `->getWidth/Height` 10 |
| `Buffer::` (SharedPtr 64, createStructured 55, create 3, createTyped 2) | ~125 | `->setBlob` (shared count above), `->map/unmap` 4/4, `CpuAccess` 8, `MapType` 4 |
| `BlendState` (BlendFunc 52, BlendOp 26, create 9, Desc 4) | ~100 | many per-pass blend configs |
| `Sampler` (Filter 36, AddressMode 30, create 10, Desc 4, ComparisonMode 1) | ~80 | 4 shared samplers created in `terrain.cpp:537-550` |
| `FALCOR_PROFILE` | 66 | profiler scope macro |
| `ResourceFormat::*` | ~65 | RGBA8Unorm, R11G11B10Float, D24UnormS8, RG32Float, R32Float, R16Uint/Unorm, RGB32Float, RGBA16/32Float, RGBA32Uint, BC6HU16, BGRA8UnormSrgb, RGB10A2Unorm, R8Unorm/Uint |
| `Resource::BindFlags::*` | ~90 | ShaderResource 37, UnorderedAccess 33, **IndirectArg 15**, None 5, RenderTarget, AllColorViews |
| `State()->` (setFbo 23, setRasterizerState 19, setBlendState 19, **setViewport 16**, setDepthStencilState 7) | 84 | ⚠ compat shim IGNORED setViewport — a real gap if sub-viewports ever used |
| `Input::Key/Modifier/MouseButton`, `MouseEvent`, `KeyboardEvent` | ~70 | editor input |
| Camera (getPosition 18, getViewMatrix 8, getProjMatrix 7, set* 15, getViewProjMatrix 2, getFrameHeight/getFocalLength 4, getData 2, `isObjectCulled` 1 (`Sprites.cpp:160`), setUpVector 1, setDepthRange, setFocalLength, setAspectRatio, getAspectRatio) | ~60 | `isObjectCulled` = Falcor AABB frustum test — shim implements it |
| `Fbo` (SharedPtr 22, create2D 10, Desc 8, `->getColorTexture` 73, `->getDepthStencilTexture` 2) | ~115 | |
| RenderContext direct calls | 40 | blit 7, copySubresource 6, copyResource 6, clearFbo 6, clearRtv 5, updateTextureData 2, resourceBarrier 1, readTextureSubresource 1, flush 1, + the 5 draw/dispatch entry points used via wrappers |
| `Bitmap::FileFormat/ExportFlags` | 30 | via `Texture::captureToFile` (debug/bake exports) |
| `GraphicsState::Viewport` | 16 | viewport structs |
| `Gui::Window` + Gui fonts | 11+ | terrainGenerator GUI (kept per developer decision) + about box |
| File dialogs: `openFileDialog` 14, `saveFileDialog` 8, `chooseFolderDialog` 3 | 25 | editor flows + **first-run directory recovery** (`terrain.cpp:435-469` — see §10) |
| `TextRenderer::render/setColor` | 7 | debug overlay text (`terrain.cpp:3154+`) |
| `DepthStencilState`, `RasterizerState`, `Vao`, `VertexLayout`, `GraphicsProgram(::Desc, setShaderModel)`, `ComputeProgram`, `ProgramReflection`, `DefineList` | each < 20 | mostly inside the two wrappers |
| `gpFramework` (getFrameRate, getWindow()->shutdown) | 3 | GUI fps + fatal-exit path |
| `gpDevice` | 2 | |
| `Profiler::instance` | 2 | |
| `MonitorInfo::getMonitorDescs` | 1 | |
| `TriangleMesh` | 1 | (`SharedPtr` mention only) |

### Comparison vs the previous port's shim (`EarthworksFX/interface/Falcor.h`, 1301 lines)

The shim already models this exact surface 1:1 (same class list, same RenderContext
methods incl. `dispatchIndirect`, `updateTextureData`, `readTextureSubresource`,
`captureToFile`, file dialogs). Conclusion for the path decision: **the API surface is
small and closed** — dominated by {create texture/buffer, bind by name, setBlob, 5 draw
entry points}. A direct-Diligent port replaces ~30 types but only ~5 of them carry real
semantics (ShaderVar binding tree, RenderContext, Texture, Buffer, the PSO-ish state
objects); the rest are descriptor structs. Known shim gaps that a re-port must NOT
reproduce: setViewport ignored, PSO cache keyed without state contents (F4),
name-guessed dummy-texture dimensions (F12), global cbuffer cache by `name:size`
(BRINGUP "interesting behaviour").

---

## 8. File formats & persistence (version compatibility matters — Steg data must load)

### Config files (cwd of the process)

| File | Format | Contract |
|---|---|---|
| `lastFile.xml` | cereal XML, `_lastFile` (`terrain.h:103-161`), **CEREAL_CLASS_VERSION 104**; loaded via `archive(CEREAL_NVP(lastfile))` → version IS embedded in the file (`<cereal_class_version>`), old files load fine (fields gated `_version >= 101/102/103/104`) | Written by `~terrainManager` (`terrain.cpp:379-387`, also stores `mode`, road + stamps last filenames). **Missing file = hard shutdown** (`terrain.cpp:407-414`). Fields: terrain/road/stamps/roadMaterial/terrafectorMaterial/texture/fbx/EVO, weed/twig/leaves/trees/vegMaterial dirs, `dir_Resource`/`dir_Terrains`/`dir_GIS` (v103), mode (v104). F24: empty dir_* + absolute terrain path silently breaks every `<dirResource>` asset — add loud logging in the re-port. |
| `earthworks4_presets.xml` | cereal XML, `Earthworks_4::serialize` (`Earthworks_4.h:91-103`) | ⚠ `CEREAL_CLASS_VERSION(Earthworks_4, 101)` is declared but load/save call `serialize(archive, 100)` with a HARDCODED 100 (`Earthworks_4.cpp:135,305`) → the `_version >= 101` layout fields are dead code and NO version is embedded. Persists `refresh.vsync`, `refresh.minimal`. |
| `camera.bin` | raw `fwrite(&CameraData, sizeof, 1)` | No versioning at all. Delete-on-incompatibility or version it in the re-port (BRINGUP trap: stale FrameHeight). |
| `<terrain>.terrain` / `terrainSettings.json` | cereal JSON, `_terrainSettings` (`terrain.h:164-189`), manual `serialize(archive, 100)` — no embedded version; fields: name, projection (proj4 string), size, dirRoot/dirExport/dirGis/dirResource | Resolution logic `terrain.cpp:416-520`: absolute path → dirs prefixed from lastFile `dir_*` (with openFileDialog recovery for missing dirs); relative → cwd prepended to dirRoot/dirGis/dirResource. Steg ships `size=40000`, relative dirs (see `EarthworksFX/assets/terrains/switserland_Steg/terrainSettings.json`). |

### Terrain data directory (`dirRoot`)

| Path | Format | Reader |
|---|---|---|
| `elevations.txt` | text, per line: `lod y x texSize origin.x origin.y size hgt_offset hgt_scale filename` (fscanf `terrain.cpp:1244`) | `loadElevationHash` `terrain.cpp:1227-1289`. **lod 0** line = raw `.bil` float32 (texSize², typically 4097) → R32Float root texture with **8 mips** (`generateMips`); lod > 0 = JP2 (OpenJPH) 1024², decoded to **R16Unorm** with per-tile `hgt_offset/hgt_scale` dequant. Tile hash = `(lod<<28)+(y<<14)+x` (`:1223`). |
| `orthophotos.json` | cereal JSON of `jp2Dir` (`terrain.h:356-382`): `jp2File{filename, hash, sizeInBytes, tiles[]}`, `jp2Map{lod,y,x, origin,size, hgt_offset/scale, fileOffset, sizeInBytes}` — tiles can be RANGES inside one physical file (fileOffset/sizeInBytes) | `loadImageHash` `terrain.cpp:1292-1324`; photo payloads under `dirRoot + "/orthophoto/"`; LRU cache (`lru_cache.h`, default 50 entries) of decoded byte vectors; `saveBinary/loadBinary` fast-path variants exist. |
| `gis/_export/root4096.bil` | raw 4096²·float32 = 67,108,864 B heightfield | `_shadowEdges::load` `terrain.cpp:157+` (F23: `elevation/root4096.bil` variant does not exist for Steg — the extract's onLoad uses the elevation/ path; pick per-terrain, warn loudly). |
| `*.roadnetwork` | **cereal Binary**, `roadNetwork::serialize` with EXTERNAL version param: `load(path, _version=ROADNETWORK_CEREAL_VERSION /*103*/)` (`roadNetwork.cpp:368-396`) | ⚠ Version is NOT stored in the file — the manual `serialize(archive, _version)` call bypasses cereal's class-version embedding. Old files need the explicit `upgrade(_FROM)` flow which re-saves as `<name>.<103>.roadnetwork` (`:399-413`). Member versions that matter: splinePoint 102, roadSection 103, intersection 101 (`roads_bezier.h:221`, `roads_road.h:193`, `roads_Intersection.h:70`). Post-load fixup re-links intersections and calls `solveRoad()` TWICE per section (`:378-388`) — deliberate (two-pass convergence), keep. |
| terrafector materials / stamps | cereal, `TFMATERIAL_VERSION 101` (`terrafector.h:248`); `lodTriangleMesh`/`tileTriangleBlock`/`triVertex` v100; stamps `stampCollection` etc. v100 (`roads_road.h`) | |
| ecotopes | cereal **JSON** (`ecotope.cpp:76-94`), textures reloaded from `ecotopeSystem::resPath` | |
| plant binaries | `binaryPlantOnDisk` (v100, `vegetationBuilder.h:1581`), `onLoad(path, vOffset)` `vegetationBuilder.cpp:3348`; ~55 cereal-versioned vegetation classes total (max version 103 `_stemBuilder`) — see vegetation catalog doc | |
| `<dirResource>` shared assets | `colorcubes/*.cube` (33³ text LUT), `skies/alps_bc.dds`, `skies/alps_IR_bc.dds`, `cube.fbx` (skydome geometry!), `vegetationHuman.fbx`, `sprites/sprite_{diff,norm,trans}.dds` | F24: all silently missing when dirResource resolves to "" |

**Version-compatibility rule for the re-port**: keep cereal + the exact class layouts and
CEREAL_CLASS_VERSION numbers for anything read from disk (roadnetwork 103, lastFile 104,
splinePoint 102, TFMATERIAL 101, vegetation classes as-is), and keep the manual-version
call convention for `.roadnetwork` (embedding versions now would change the format).

---

## 9. Threading map (complete for the extract)

| Thread | Started | Sync mechanism | Notes |
|---|---|---|---|
| `_shadowEdges::solveThread` | `Earthworks_4::onLoad:152-153`, `std::thread(...).detach()` | Plain (non-atomic) bools `requestNewShadow` / `shadowReady` + `Sleep(10)` poll loop (`terrain.cpp:83-102`); render thread consumes `shadowReady` in onFrameUpdate, uploads `shadowH`, writes `global_sun_direction` | **Infinite loop, never joins, reads/writes 4096² arrays the render thread also reads.** Works because writes complete before the flag flip and x86 gives it away — but it is a formal data race. The current port already wrapped this in `launchSolveThread()` (`EarthworksFX/src/core/Earthworks_4.cpp:431`); the re-port should keep that shape and make the flags atomic. |
| `terrainManager::hashAndCache_Thread` | `std::async(std::launch::async, ...)` per elevation-tile miss (`terrain.cpp:2024`), future stored in `hashFuture` | int state machine `hashCount` 0→1 (decoding) →2 (done, set by worker `:1991`); main thread creates the Texture from `jphData` when it sees 2 (`:1999-2006`) and resets to 0. **Single decode in flight**; split retries (`return false`) until cached | JP2 elevation decode (OpenJPH) into `jphData` (uint16). Texture creation stays on the render thread — only decode is off-thread. |
| `terrainManager::hashAndCacheImages_Thread` | same pattern (`terrain.cpp:2170`), `hashCountImage`/`hashIFuture`/`jphImageData` | identical state machine (`:2121-2170`) | JP2 orthophoto decode. |
| `terrainGenerator::hgt_to_jpeg2000` / `img_to_jpeg2000` | detached `std::thread` from the generator GUI (`terrainGenerator.cpp:676-698`) | none visible (progress via member fields) | Pipeline-tool batch conversion; acceptable as-is for a tool. |
| `barrier.hpp` | — | thread barrier (mutex + condvar) | **Orphaned**: its only users (glider/CFD threads) were stripped; zero references left. Drop from the re-port; keep the file noted for the future editor/glider phase. |

No `std::mutex`/`std::atomic` anywhere in the extract outside `barrier.hpp` — the entire
cross-thread story is flag polling. Any re-port that changes scheduling (e.g. real job
queue for tile decodes) changes streaming behavior — treat as a deliberate, separately
verified step.

---

## 10. Port drift notes (extract vs current `EarthworksFX/src/core/Earthworks_4.cpp` + app shell)

The re-port keeps the Diligent host shell. What must be re-wired vs what the port added:

### Extract ↔ port app-shell mapping

| Original (extract) | Current port equivalent | Status/drift |
|---|---|---|
| `main()` + `Sample::run` | `EarthworksFXApplicationBase` (win32 loop) + `EarthworksFXSample` | Keep host. Engine-init musts collected in the host: `SetBreakOnError(false)` D3D12+Vk (F12), Vulkan descriptor pools 32768/16384 sampled images (F13), D3D12 `GPUDescriptorHeapDynamicSize[0]=786432`, sampler heap 128/1920 (F18), Vulkan `BufferCount=3` for vsync-off (F21), swap chain `BGRA8_UNORM_SRGB` (`EarthworksFXApplicationBase.cpp:119,128-129,343-372,423-444`). F13/F18 sizes exist ONLY because the shim binds everything DYNAMIC — a native port with static/mutable bindings can shrink them, but re-verify before deleting. |
| `onLoad(ctx)` | `InitializeScene()` (`:533-552`) → `onLoad` + initial `onResizeSwapChain` + FPC seeded from scene camera (pos/target, move speed 50) | Same order. |
| framework frame loop | `Update()` (`:638`) → `OnUpdate` (`:1118`) syncs FPC → `camera->setPosition/setTarget(pos + ahead*100)` (`:1148-1157`); `Render()` (`:1209`) → `OnRender` (`:1105`) → `onFrameRender(ctx, m_TargetFbo)`; `Present()` (`:1232`) | `m_TargetFbo = Fbo::createFromSwapChain` is a PROXY: `getColorTexture(0)` is null (F5) — a native port should give the swap-chain FBO a real texture view or keep the getRenderTargetView discipline. |
| `onResizeSwapChain` | `WindowResize` → `OnWindowResized` (`:1138`) | identical. |
| Falcor mouse (normalized) | `SyncInput` normalizes pixels → [0,1], synthesizes Move + Wheel events (`:1159-1197`) | keep. |
| number keys 1..7 (terrain mode) | host does not forward them; port added `ew::gDebug.toggles.requestTerrainMode` honored at frame start (port `Earthworks_4.cpp:537-542`) | re-port needs SOME mode-switch path. |
| `refresh.minimal` Sleep | port DISABLED it: `if (false && refresh.minimal)` (port `:606`) | decide: testflights want honest timing; the editor wants the power mode. |

### Port-side deltas inside Earthworks_4 (drift to be aware of when re-porting from the extract)

- **Shadow load path**: port uses `gis/_export/root4096.bil` (F23) and passes an optional
  `buildingsRenderer*` so buildings bake into the shadow heightfield (port `:414-434`) —
  an intentional IMPROVEMENT, extract has terrain-only shadows and the `elevation/` path.
- **camera.bin FrameHeight force-correct** after fread (port `:334-338`).
- **Tonemapper**: port calls `drawInstanced(6,1)` with a comment claiming the shared IB
  forces an indexed draw — that was the pre-F22 workaround world; original draws
  `drawInstanced(3, 1)` (extract `:278`, one oversized triangle) and post-F22 semantics
  make 3 correct again. Verify the port's compute_tonemapper.hlsl vsMain (it may have
  been rewritten for 6 verts). Also port adds `["gConstants"]["debugView"]` toggle.
- **bypassHdr / tonemapper / syncCamera / terrainUpdate / atmosphere toggles**
  (`ew::gDebug`) + debug globe/ground-grid draws + overlay pass + `aboutTex` — bring-up
  aids worth carrying over, not original behavior.
- **GUI**: the port's `onGuiMenubar/onGuiRender/initGui/guiStyle` (port `:44-216,815-859`)
  is ORIGINAL editor GUI that the extract stripped — for the sample-first milestone it is
  optional; fonts come from `Framework/Fonts/*`.
- The port kept glider/cfd thread startups commented out (port `:304-323`) — the extract
  has them fully removed.
- BRINGUP F-findings that are COMPAT bugs (not original semantics): F4, F5, F7-F18,
  F21-F22, F25-F26. F-findings encoding ORIGINAL semantics to preserve: F3 (fov/depth
  conventions), F6, F19/F20/F20.1 (RH/CCW/compass), F23 (data path is per-terrain), F9
  (layout rule), plus the atmosphere half-angle note.

---

## 11. Bad code / removable candidates (opinion — do not act without developer sign-off)

- `Earthworks_4.h`: unused `tonemappedFbo`, `scene` (earthworksScene stub), `layout`
  struct (feeds dead GUI in the extract), includes `RenderGraph.h`/`VideoEncoderUI.h`.
- Presets hardcoded `serialize(archive, 100)` → v101 layout persistence is dead; either
  fix the call or drop the versioned fields.
- `solveThread` infinite `while(1)` + `Sleep` + non-atomic flags: keep the algorithm,
  fix the lifetime (port's `launchSolveThread` already does) and make flags
  `std::atomic<bool>`.
- Hardcoded `1920` resolution in `setCamera` (`Earthworks_4.cpp:236`) — should be the
  real render width, but changing it CHANGES LOD BEHAVIOR — do not "fix" silently.
- `_shadowEdges` embeds 4·(4096² float) ≈ 300 MB of static arrays in the terrainManager
  object (stack/global size hazard); heap-allocate in the re-port (behavior-neutral).
- Port GUI bug (carried from original?): the three layout DragInts all edit
  `layout.left_width` (port `:121-123`).
- `lru_cache.h` `get()` does erase+reinsert on every hit — fine at csize 50, just don't
  grow it blindly.
- File-dialog based first-run directory recovery (`terrain.cpp:435-469`) is hostile to
  headless/testflight runs — port replaced dialogs with stubs; the re-port should make
  missing dirs a loud log + graceful fallback instead.

## 12. Open questions / uncertainties

1. **Which root4096.bil path per terrain?** The extract's onLoad uses `elevation/`, Steg
   ships `gis/_export/` — is this per-terrain data-layout drift the terrainSettings
   should encode, or is `gis/_export` simply current?
2. `numLights`/`sunColour` in `shaderLightBuffer` are never written by the app shell —
   written elsewhere or genuinely dormant? (Shaders may read uninitialized values —
   check when porting the consumers.)
3. `postProcess.colorCube` RGB32Float 3D + linear sampler: was original Falcor silently
   promoting/filtering? Diligent may reject filtering RGB32F — needs a format decision.
4. `refresh.vsync` is persisted and shown in GUI but the extract never applies it to the
   swap chain (Falcor's Sample handled it?) — confirm the host owns vsync (it does in the
   port via `m_Window.GetVSync()`).
5. `Camera::isObjectCulled` (`Sprites.cpp:160`) is the ONLY core-side use of Falcor's own
   frustum culling — semantics (AABB vs sphere, which frustum) must match when re-porting
   Sprites.
6. The `resolution` param 1920 vs actual window width: original always 1920 regardless of
   window size — deliberate LOD calibration or oversight? Affects fps comparisons.
