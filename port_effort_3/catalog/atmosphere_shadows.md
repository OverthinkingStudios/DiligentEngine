# Concept Catalog — Atmosphere / Sky / Fog / Shadows / Lighting

Sources analyzed: `port_effort_3/source_extract_3/` — `earthworks_scene/atmosphere.{h,cpp}`,
`volumeFogPhaseFunctions.cpp`, `earthworks_scene/cascadeShadowMaps.{h,cpp}`, the `_shadowEdges`
system in `terrain.{h,cpp}`, `Earthworks_4.{h,cpp}`, `hlsl/atmosphere/*`, `hlsl/PBR.hlsli`,
`hlsl/render_Common.hlsli`, `hlsl/compute_tonemapper.hlsl`, `hlsl/terrain/gpuLights_*.hlsli`,
consumers `render_Tiles.hlsl` / `render_vegetation_ribbons.hlsl` / `render_triangles.hlsl`.
All file:line refs below are into `port_effort_3/source_extract_3/` unless prefixed.

---

## 0. Executive finding on shadows (mission-relevant, stated plainly)

**There are NO realtime geometry shadow maps in the original.** The entire realtime shadow story is:

1. **Terrain self-shadow**: an async **CPU** scanline ray-march over the 4096² root heightfield
   (`_shadowEdges`, terrain.h:76–92 / terrain.cpp:83–295), producing a 4096² RG32Float
   "shadow-line height + softness" texture sampled analytically by all surface shaders and by the
   fog compute. This shadows terrain, vegetation *placement*, buildings and the fog volume — one
   texture, no per-frame GPU cost.
2. **Vegetation self-shadow**: per-vertex light-cone/depth values **baked at asset build time**
   (ribbonBuilder), reconstructed in the VS and turned into soft dappled light in the PS via a
   noise texture threshold trick — no shadow map at all.
3. `cascadeShadowMaps.{h,cpp}` is **stub scaffolding**: `shadowMap::init` builds an `Fbo::Desc`
   but the `Fbo::create2D` line is commented out (cascadeShadowMaps.cpp:10); `startRender`/
   `stopRender`/`setToShaders` are empty (15–29); `cascadeShadows::init/update` bodies are almost
   fully commented out (35–102). `terrainManager::shadowSetup/shadowRenderFar/Near/Soft/
   shadowRender` are all **empty** (terrain.cpp:2797–2817). Nothing ever renders into a cascade.
   `earthworksScene` (earthworksScene.h) is an empty class that merely includes the header.
4. `_rootPlant::bakeShadowMap` (vegetationBuilder.cpp:4424–4468) is an 8192² depth FBO
   (created vegetationBuilder.cpp:2377) rendered from an ortho sun view — but it is **asset-bake
   only**: called from `SAMPLE_MODE` / `buildOneMap` (vegetationBuilder.cpp:4038–4040, 4472–4480)
   and sampled via `highResShadow.SampleCmp` only inside the `_RGB_SAMPLE` shader variant
   (render_vegetation_ribbons.hlsl:871–884). It bakes the per-plant sunlight response maps that
   the runtime vertex shadow term (item 2) later reads. Not a runtime pass.
5. `DrawArgs_Shadows_Quads/Plants` UAVs and empty `SHADOW_TILE`/`SHADOW_OUTER` entry points in
   compute_vegetation_lod.hlsl:20–56 are future-GPU-shadow scaffolding; never bound from C++.
6. Cloud shadow: `cloudShadow`/`CloudMap` sampling is commented out — hardcoded to 1
   (compute_volumeFog.hlsli:324, 328).

So the re-port must reproduce (1) and (2) faithfully, and should treat "cascade shadow maps" as a
name only. If the developer expects contact/dynamic-object shadows from the original: they don't
exist there.

---

## 1. Purpose & data flow

### 1.1 CPU terrain shadow (async, event-driven — not per-frame)

- **Load** (Earthworks_4.cpp:142–156): `shadowEdges.load("<dirRoot>/elevation/root4096.bil", …)`
  reads a raw 4096²×float32 heightfield (the whole 40 km terrain root, 9.765625 m/px =
  40000/4096). `load()` (terrain.cpp:157) precomputes X-slope `Nx[y][x] = (h[y][x]-h[y][x+1]) /
  9.765625` and fills `shadowH` with a **placeholder** `(height-5, 0)` (terrain.cpp:176 — author
  comment "remove this and pass terrain height separate").
- The GPU texture `terrainShadowTexture` (RG32Float, 4096², terrain.h:785) is created **from the
  placeholder** (Earthworks_4.cpp:150) and handed to the atmosphere (`setTerrainShadow`,
  Earthworks_4.cpp:155 → atmosphere.cpp:219).
- A detached `std::thread` runs `_shadowEdges::solveThread` forever (Earthworks_4.cpp:152,
  terrain.cpp:83–102): polls `requestNewShadow` every 10 ms; when set, advances
  `sunAngle += dAngle`, derives `sunAng = (-cos a, -sin a, 0)` and calls
  `solve(-sunAng.y, sunAng.x > 0)`; sets `shadowReady`.
- **Upload** (Earthworks_4.cpp:204–213, per-frame check): when `shadowReady`,
  `updateTextureData(terrainShadowTexture, shadowH)` AND `global_sun_direction = sunAng` in the
  same place — sun direction and its matching shadow field change **atomically together**.
- Triggering: in the extract only the initial `requestNewShadow = true` at load
  (Earthworks_4.cpp:148). In the original/previous port a GUI DragFloat("angle") + button re-sets
  it (EarthworksFX/src/core/Earthworks_4.cpp:95–98) — that GUI was stripped from the extract.
  The re-port needs *some* trigger to move the sun.

### 1.2 Atmosphere / volumetric fog (per-frame, 2 compute dispatches, cheap)

Per frame in `Earthworks_4::onFrameUpdate` (Earthworks_4.cpp:216–246):
1. Build `shaderLightBuffer` on the stack: sun basis (right = normalize(cross(up, sunDir)),
   up = normalize(cross(sunDir, right))), screenSize, far+near fog log-depth params from the
   `FogVolume`s (Earthworks_4.cpp:219–232).
2. `terrain.updateShaderConstants(hdrPreviousFrame, lightBuffer)` (terrain.cpp:1752–1779): binds
   `terrainShadow` + LightsCB scalars into terrainShader, terrainSpiteShader, and (via
   `plants_Root.updateShaderConstants`, vegetationBuilder.cpp:3998–4021) vegetation + billboard
   shaders.
3. `atmosphere.setSunDirection(global_sun_direction)`; `atmosphere.getFar().setCamera(camera)`
   (updates ray basis + slice constants); `computeSunInAtmosphere` (atmosphere.cpp:96–152,
   512×256 dispatch); `computeVolumetric` (atmosphere.cpp:155–212, 64×32 threads → 8×4 groups).
4. Consumers sample the resulting textures during `terrain.onFrameRender` into the HDR FBO
   (R11G11B10F + D24S8, Earthworks_4.cpp:347), then the tonemapper full-screen pass writes the
   swapchain (Earthworks_4.cpp:271–279), then `hdrFbo` color is blitted into the **half-res**
   `hdrPreviousFrame` (Earthworks_4.cpp:281, created at :349) for next frame's temporal tricks.

Texture flow:

| Producer | Texture | Format/size | Consumers |
|---|---|---|---|
| compute_sunlightInAtmosphere | `sunlightTexture` | RGBA32F 512×256 (atmosphere.cpp:79) | fog compute (`SunInAtmosphere` t7), all surface shaders (`SunInAtmosphere` t16 via `sunLight()`) |
| compute_volumeFogAtmosphericScatter | `mainFar.inscatter`/`outscatter` | R11G11B10F 3D **64×32×256** (atmosphere.cpp:73) | terrain/veg/billboard/buildings pixel+vertex fog (t12/t13) |
| same dispatch | `inscatter_sky`/`outscatter_sky`, `*_cloudbase` | RGBA16F 2D 64×32 | skydome (`gAtmosphereInscatter_Sky` t22, render_triangles.hlsl) |
| CPU table | `phaseFunction` | R32F 32×128 (`phaseData`, volumeFogPhaseFunctions.cpp:3–5) | fog compute (`hazePhaseFunction` t5) |
| CPU solver | `terrainShadowTexture` | RG32F 4096² | fog compute (t8), surface shaders (t21) |

- `mainNear` (RGBA16F 3D 256×128×256) and `parabolicFar` (128×128×32) FogVolumes are **allocated
  but never computed or sampled** in the surviving code path (atmosphere.cpp:75–76; only
  `mainFar` is wired to `compute_Atmosphere`, :87–92). Their compute shaders
  (compute_volumeFogLights.hlsl, compute_volumeFogSmokeAndDust.hlsl) `#include
  "CSVolumeFogCommon.hlsli"` — a filename that no longer exists (renamed
  `compute_volumeFog.hlsli`) → they cannot even compile as-is. Dormant subsystem.

### 1.3 Tonemap / post

`postProcess.tonemapper` (pixelShader) + `colorCube` 33³ RGB32Float 3D LUT parsed from a `.cube`
text file (Earthworks_4.cpp:162–193). Full-screen: ACES filmic with hardcoded `* 1.7943`
pre-exposure, then `lerp(aces, LUT(aces), 0.2)` (compute_tonemapper.hlsl:43–50). Drawn with
`drawInstanced(3,1)` — single fullscreen triangle from SV_VertexID (:33–39).

---

## 2. Core tricks & clever mechanisms (MUST NOT be lost)

1. **Edge-seeded scanline shadow solve** (`_shadowEdges::solve`, terrain.cpp:104–155): instead of
   ray-marching every texel, only texels where the X-slope crosses the sun angle
   (`Nx[y][x-1] < a+0.02 && Nx[y][x] > a-0.02`) *cast*; from each such silhouette edge the shadow
   line height `H -= angle * 9.765625` is propagated along the row **with early break** the
   moment it drops below an already-written shadow line (:130–136). This makes a 16.8M-texel
   solve fast enough for a background thread. Stored value is `float2(shadow-line height,
   softDepth = casterDistance/10)` (:132–133) — NOT a boolean mask; softDepth encodes penumbra
   width growing with distance from the caster.
2. **Analytic soft shadow reconstruction in-shader** — two deliberately different variants:
   - Surface shaders (render_Common.hlsli:78–85): `h = (pos.y - r) / (g / 30);
     return pow(saturate(h), 1.5)` — sharp near casters, soft far away. Note the dead first line
     (`h = (pos.y + g*0.3 - r)/g`) is immediately overwritten — the `/30` variant is live.
   - Fog compute (compute_volumeFogAtmosphericScatter.hlsl:36–44):
     `h = (pos.y + g*0.05 - r) / g; return saturate(h)` — wider penumbra for volumetrics.
   Unshadowed texels are `(-5000, 0)` (terrain.cpp:111) → `h` huge → fully lit with a 5 km
   margin; this is what makes coarse-LOD geometry immune to heightfield/mesh mismatch (see F26).
   Terrain applies `S = pow(shadow(...), 0.25)` before multiplying sunlight
   (render_Tiles.hlsl:287).
   **World→UV mapping is hardcoded**: `uv = saturate(pos.xz / (4096 * 9.765625) + 0.5)` in both.
3. **Sun/shadow atomic swap** (Earthworks_4.cpp:204–213): `global_sun_direction` is only updated
   in the same block that uploads the freshly solved shadow texture. Decoupling these gives
   shadows that disagree with the lighting direction.
4. **Sun transmittance LUT with per-channel refraction** (compute_sunlightInAtmosphere.hlsl):
   each texel = transmittance from a point (x = ±800 km along the sun azimuth, y = 0–100 km
   altitude) to space, marched with a **geometrically growing step** (`step *= 1.3` from 0.5 km,
   :7–23) against a spherical-earth altitude (`altitude = sqrt(R² + x²) - EarthR`). RGB use three
   *different* refraction-bent directions (`refraction.xyz` added to the sun-direction Y,
   :64–66) — that is where red sunsets and the sun's color gradient come from. Alpha stores the
   summed Mie optical depth (`optSum`, :67, 76) — see trick 6. Texel (0,0) is a special "sky
   ambient above clouds" hack integrating the previous frame's column x=256 downward (:32–57).
5. **World-space sun lookup** `sunLight(posKm)` (render_Common.hlsli:204–211 = duplicate of
   compute_volumeFog.hlsli:175–181): `sunUV.x = saturate(0.5 - dot(pos.xz,
   normalize(sun.xz))/1600); sunUV.y = 1 - saturate(pos.y/100)` then `* 0.0795774715…` (= 1/4π).
   Every lit shader gets altitude- and horizontal-position-dependent sunlight for the price of
   one 2D sample. The 1600 (km) and 100 (km) constants must match the LUT build (:61–62 of the
   sunlight shader).
6. **Optical-depth-indexed Mie phase LUT** (the atmosphere's visual signature):
   `phaseFunction` is a 32×128 *measured/precomputed* table (volumeFogPhaseFunctions.cpp,
   4096 floats). Lookup (compute_volumeFog.hlsli:339–343 with phaseUV built in
   AtmosphericScatter:83–84): `phaseUV.y = sqrt(acos(VoL)/π)` — the sqrt warp concentrates table
   resolution at the forward-scatter peak; `phaseUV.x = opticalDepthToSun/10` — the **phase
   function widens with optical depth**, a cheap multiple-scattering approximation (this is what
   `sunlight.a` from trick 4 feeds); `phaseUV.x += 0.46875` switches to the haze row block
   (different particle size) for the second sample. Losing the sqrt warp, the /10 scale or the
   0.46875 offset silently changes the whole sky.
7. **Single uber-march producing three outputs** (compute_volumeFogAtmosphericScatter.hlsl:100–146):
   one loop fills (a) all 256 froxel slices up to the cloud base (or ground), (b) continues
   marching to the cloud base into a 2D `_cloudBase` layer, (c) continues to 100 km into the 2D
   `_sky` layer used by the skydome. Termination uses **planet-curvature radius**
   `R = length(pos*0.001 + (0, EarthR, 0))` (compute_volumeFog.hlsli:378) so downward rays keep
   marching below the cloud-base altitude (`|| direction.y < 0`, :105) and rays leaving the
   atmosphere stop. Remaining slices after early exit are back-filled (and, extract-state, get a
   leftover debug marker `gInscatter = (0,0,1)`, :124 — see §9).
8. **Exponential (log-depth) froxel slicing** (FogVolume::setCamera, atmosphere.cpp:27–61):
   `m_SliceStep = (far/near)^(1/(z-1))`, `m_SliceZero = near/step`; shader marches
   `step = depth * sliceStep; depth *= sliceMultiplier`. Inverse mapping in every consumer:
   `uvw.z = log(dist / fog_Start) * fog_log_F + fog_one_over_k` with
   `fog_log_F = (k-1)/k / log(far/near)` and `fog_one_over_k = 1/k` (atmosphere.cpp:55–56).
   CPU constant and shader lookup must match exactly or fog bands shift.
9. **The accumulate quirk** (`acumulateFog`, compute_volumeFog.hlsli:381–386 + caller):
   `newOut` **accumulates** across steps (`newOut += …` inside `calculateStep`, :357, never reset
   by the caller) while `newIn` is **overwritten** each step (:356). So `outscatter =
   exp(-totalOpticalDepth)` is recomputed fresh each slice, and
   `inscatter += outscatter_prevSlice * newIn` uses the *previous* slice's transmittance. This
   asymmetric in/out handling looks like a bug and is not — a rewriter "fixing" either side
   breaks energy behaviour.
10. **Bicubic upsample of the tiny fog volume** (render_Common.hlsli:121–200): inscatter is
    sampled with a 4-tap bicubic (implemented as 4 bilinear fetches with cubic-weight offsets);
    outscatter is plain trilinear. This is the only reason a **64×32×256** volume looks smooth on
    a 4K screen. Vegetation evaluates fog **per-vertex** (`vs_atmosphere`,
    render_Common.hlsli:237–244, called render_vegetation_ribbons.hlsl:384/471) while terrain
    does it per-pixel (render_Tiles.hlsl:407–420) — moving either to the other side changes both
    cost and look.
11. **Vegetation dappled-shadow trick** (no shadow map): at asset build, every ribbon vertex gets
    a light *cone* direction (egg-envelope normal, `ribbonBuilder::lightBasic`,
    ribbonBuilder.cpp:292–319) and a normalized *depth inside the canopy*; packed to 9+8+7+8 bits
    in `v.e` (ribbonBuilder.cpp:51–74). Runtime VS: `Shadow = saturate(a*sunDepth + sunDepth)`
    where `a = saturate(dot(normalize(lightCone - sunDir*PLANT.sunTilt), sunDir))`
    (render_vegetation_ribbons.hlsl:488–493). PS: project worldPos onto the **sun's right/up
    basis** (`sunRightVector/sunUpVector` from LightsCB — this is their entire purpose), sample
    the tiling noise `gDappledLight` (`dappled_noise_01.jpg`, terrain.cpp:738), and
    double-smoothstep it against `Shadow` with `PLANT.shadowSoftness`
    (render_vegetation_ribbons.hlsl:1082–1087) → sun flecks that slide over the foliage as the
    view moves, for one texture sample. `plant.shadowUVScale/shadowSoftness` live in the plant
    GPU struct (vegetation_defines.hlsli:76–77).
12. **JHFAA temporal alpha** (render_Common.hlsli:215–223): semi-transparent vegetation pixels
    lerp toward the previous half-res frame instead of alpha blending —
    consumes `hdrPreviousFrame` and `screenSize`. Cheap OIT-ish effect; needs the post-tonemap
    blit chain intact (Earthworks_4.cpp:281).
13. **Skydome = fog sky slice** (render_triangles.hlsl + terrain.cpp:2956–2967): a 36-vertex cube
    (`cube.fbx`) drawn with `pos.z = 1; pos.w = 1.000002` (:92–93 — glued to the far plane,
    depth-test friendly), sampling `gAtmosphereInscatter_Sky` with 2D bicubic. The sun disc/glow
    exists **only** here. `useSkyDome = 0` path is the live one.

---

## 3. Invariants & conventions

- **Shadow-solver sun path is locked to the X axis**: `sunAng = (-cos a, -sin a, 0)` —
  terrain.cpp:89–92 — and `solve()` marches along **rows (±X)** only. The system cannot represent
  an arbitrary sun azimuth; the whole terrain-shadow feature assumes east↔west sun. (Author
  comments at terrain.h:74–75: "MOVE TO TEMP SHADOW CLASS TO BE REPLACED WITH GPU DATA".)
- Heightfield metrics hardcoded everywhere: 4096 px, 9.765625 m/px (= 40000/4096), world XZ range
  ±20000 m, uv = pos.xz/(4096·9.765625)+0.5. Also hardcoded: sun LUT 512×256 spanning ±800 km ×
  0–100 km; phase LUT 32×128; fog volume dims in dispatch counts (atmosphere.cpp:151, 211).
- `global_sun_direction` points **from sun toward ground** (negative y when up;
  default `(0.966, -0.259, 0)` Earthworks_4.h:38). All `dot(N, -sunDirection)` conventions follow.
- Distance unit inside all scatter math is **km** (`pos * 0.001` at every call into
  `sunLight`/`opticalDepth`); Rayleigh/haze/fog scattering constants
  (compute_volumeFog.hlsli:148–159) are per-km. `sun_Luminance` in lux; `globalExposure`
  (default 1/20000, atmosphere.h:60) is baked into the sun LUT (sunlightInAtmosphere:76), NOT
  applied per-pixel.
- Log-depth slice constants: shader-side formulas must match `FogVolume::setCamera`
  (atmosphere.cpp:55–60). Note code uses `1.0/k` although the header comment says `1.5/k`
  (atmosphere.h:110) — **code wins**.
- Slice 0 of the fog march always gets `S = 0` ("No sun light in first slice ever",
  AtmosphericScatter:110) — prevents a bright first slice glued to the camera.
- `FogAtmosphericParams` cbuffer layout (compute_volumeFog.hlsli:56–138) is mirrored 1:1 by the
  CPU struct `fogAtmosphericParams` (atmosphere.h:17–84) **including every `padd`/`slicePADD2`/
  `parabolicPADD` member — padding is load-bearing** (a past porting agent deleted "unused"
  padding and broke rendering). Same for `shaderLightBuffer` (vegetationBuilder.h:91–114) vs
  `LightsCB` (render_Common.hlsli:35–60).
- The operative atmosphere parameter values are the **C++ member-initializer defaults** in
  `fogAtmosphericParams` (atmosphere.h:48–68): nothing in the extract writes `atmosphere.params`
  (the GUI that edited it was stripped). haze_Turbidity 1.5, fog_Turbidity 8.5, ozone 0.7 ×
  (0.65, 1.6, 0.085), refraction (0.022, 0.027, 0.03, 0), etc. Losing these defaults = a
  different sky.
- Camera basis extraction in `FogVolume::setCamera` uses `W = toGLM(view.getTranspose())`,
  `dir = W[2] * -1` — correct **only** for a right-handed glm view matrix (BRINGUP F19/F20).
  FOV derives from Falcor's film-back model: `fov = atan(0.5 * frameHeight / focalLength)`
  (atmosphere.cpp:36–37); `dy` is negated for texture-space Y (:42); eye_direction is moved to
  the **center of the top-left pixel** (:47).

---

## 4. Performance-critical details

- **The whole atmosphere is ~2 small dispatches/frame**: 512×256 LUT + 64×32 rays × ~256 log
  steps. It stays cheap because the volume is tiny (64×32×256 R11G11B10F ≈ 2 MB) and consumers
  upsample bicubically (trick 10). Raising volume resolution is the obvious "improvement" that
  costs real ms (an author comment at AtmosphericScatter:52 notes smaller seemed faster).
- **Early break in the shadow solve** (terrain.cpp:135, 150) turns worst-case O(4096) per edge
  into a short walk. Removing it (e.g. "simplify" to full march) multiplies solve time hugely.
- The CPU shadow costs **zero GPU time per frame** except one 4096² RG32F upload *when the sun
  moves*. Any replacement (GPU ray-march, cascades) must beat "free".
- `_shadowEdges` is ~**336 MB of static arrays** (`height` 64 MB + `Nx` 64 MB + `edge` 16 MB +
  `shadowH` 128 MB + 64 MB slack; terrain.h:76–92) inside `terrainManager` — this dictates the
  process footprint and stack/global placement; port must keep it heap-allocated or equivalent.
- Per-pixel terrain shading calls `shadow()` (1 sample) + `sunLight()` (1 sample) + trilinear
  outscatter + 4-tap bicubic inscatter — the shader author marked `sunLight` and the atmosphere
  block "expensive but hard to avoid" (render_Tiles.hlsl:289, 406). Vegetation deliberately moved
  fog to the VS (`vs_atmosphere`) — keep that split.
- Fog march still calls `sample_cfd` (up to 4 LOD probes × 6 samples) **per step** even though
  the cfd/smoke feature is stripped; bounds checks make it cheap-ish but not free
  (compute_volumeFog.hlsli:298–307). Also computes 3 noise evaluations (`_snoise` + 2×`gnoise`)
  per step for a **disabled** fake-cirrus feature (:310–316) — pure waste, candidates in §9.
- The sunlight LUT's growing step (`*= 1.3`) is what keeps a 300 km integration at ~2 dozen
  iterations; a fixed step would be dispatch-dominating.
- `refresh.minimal` mode sleeps 20 ms per frame (Earthworks_4.cpp:283–286) — editor babysitting,
  not a rendering mechanism.

---

## 5. GPU resources & shader interface

### 5.1 Textures/buffers (producer side)

| Resource | Format / size | Bind | Created |
|---|---|---|---|
| `sunlightTexture` | RGBA32F 512×256 | UAV+SRV | atmosphere.cpp:79 |
| `phaseFunction` | R32F 32×128, init from `phaseData` | UAV+SRV | atmosphere.cpp:78 |
| `mainFar.inscatter/outscatter` | R11G11B10F 3D 64×32×256 | UAV+SRV | atmosphere.cpp:10–16, 73 |
| `mainFar.*_cloudbase`, `*_sky` | RGBA16F 2D 64×32 | UAV+SRV | atmosphere.cpp:11–16 |
| `mainNear.*` | RGBA16F 3D 256×128×256 (**dormant**) | UAV+SRV | atmosphere.cpp:75 |
| `parabolicFar.*` | R11G11B10F 3D 128×128×32 (**dormant**) | UAV+SRV | atmosphere.cpp:76 |
| `terrainShadowTexture` | RG32F 4096² | UAV+SRV | Earthworks_4.cpp:150 |
| `postProcess.colorCube` | **RGB32F** (3-channel!) 3D 33³ | SRV | Earthworks_4.cpp:186 |
| `hdrFbo` | R11G11B10F + D24S8, screen size | RT | Earthworks_4.cpp:347 |
| `hdrPreviousFrame` | R11G11B10F, **half res**, AllColorViews | SRV/RTV | Earthworks_4.cpp:349 |
| `shadowFbo` (veg bake) | 8192² depth (+color per desc) | DSV/SRV | vegetationBuilder.cpp:2377 |

RGB32Float for the color cube is a portability trap: 3-component 32F is not filterable/renderable
on most APIs — the previous port had to repack RGB32→RGBA32 (BRINGUP `BuildTextureInitData`).

### 5.2 Compute: `compute_volumeFog.hlsli` common interface (both atmosphere shaders)

- Samplers s0 `linearSampler`, s1 `clampSampler` (set atmosphere.cpp:208–209 — **only on
  compute_Atmosphere; compute_sunSlice relies on them being unused**).
- SRVs t0 `CloudMap`, t1 `FogMap`, t2/t3 noise (all unused in live paths), t4 `gLightVolume`,
  t5 `hazePhaseFunction`, t6 `envMap` (TextureCube — **never bound in the extract**; sampled at
  AtmosphericScatter:89–91 but the result is overwritten by a constant at :94), t7
  `SunInAtmosphere`, t8 `terrainShadow`, t32+ `fogDensities[32]` (dormant near-fog).
- `ParameterBlock<cfdTextures> gCfd` = `Texture3D T[12]; float4 offset[12]; float4 scale[12]`
  (:40–46) — the **stripped cfd/smoke seam**; feeders `setSMOKE`/`setSmokeTime`
  (atmosphere.cpp:224–280) survive but are never called.
- cbuffers: b0 `FogCloudCommonParams` (sun_direction + pad, cloudBase 5000, cloudThickness;
  compute_fogCloudAtmosphereCommon.hlsli), b1 `FogAtmosphericParams` (full layout
  compute_volumeFog.hlsli:56–138 — keep byte-identical incl. padding), b2 `FogLights`
  (`volumeFogLight[64]`, dormant), b3 `FogVolumes` (`fogVolume[8]`, dormant).
- UAVs (AtmosphericScatter): u0–u2 inscatter volume/cloudbase/sky, u3–u5 outscatter — declared
  `RWTexture3D<float3>`/`RWTexture2D<float3>` in the original; the previous port had to make them
  float4 + `[[vk::image_format]]` for Vulkan (F17) — expect the same in the re-port.
- Dispatch: sunlight `[numthreads(32,32,1)]`, grid 16×8 (atmosphere.cpp:151); scatter
  `[numthreads(8,8,1)]`, grid (x/8, y/8, 1) (atmosphere.cpp:211).

### 5.3 Graphics: `render_Common.hlsli` (included via PBR.hlsli by every lit shader)

- Samplers s0–s4: point, linear, aniso, linearClamp, `gSamplerDepth` (comparison).
- SRVs: t7 `gEnv` cube, t8 `gPrevFrame`, t9–11 cube far/mid/close (mostly unused), t12
  `gAtmosphereInscatter` (3D), t13 `gAtmosphereOutscatter` (3D), t14/15 smokeAndDust (never
  bound — feeder commented Earthworks_4.cpp:121–122), t16 `SunInAtmosphere`, t20 `cloudShadow`
  (unused), t21 `terrainShadow`, t22 `gAtmosphereInscatter_Sky` (2D), t23 `gPreviousFrame`.
- b2 `LightsCB`: **exact layout** render_Common.hlsli:35–60 — 3 float3+pad blocks (sunDirection/
  numLights, sunColour/padd, sunRightVector/padd2, sunUpVector/padd3), then screenSize +
  6 fog scalars, then `gpu_LIGHT Lights[128]` (48 B each; tail never populated by the surviving
  CPU code — `numLights`/`sunColour` are never set either). CPU mirror `shaderLightBuffer`
  (vegetationBuilder.h:91–114) covers only the head — members are written **individually by
  name** through Falcor reflection (terrain.cpp:1761–1776, vegetationBuilder.cpp:4006–4020), so
  CPU-struct-vs-cbuffer size mismatch was harmless in Falcor; a re-port that block-copies the
  struct must reproduce the full layout.
- Helper functions to preserve verbatim: `shadow()` (:78), `cubic/textureBicubic/
  vs_textureBicubic` (:121–200), `sunLight()` (:204), `JHFAA_alpha` (:215), `atmosphere()` (:226),
  `vs_atmosphere()` (:237).

### 5.4 Tonemapper interface

compute_tonemapper.hlsl: s0 linearSampler, `hdr` declared `register(u0)` but is a plain
`Texture2D<float3>` **SRV** (Falcor reflection ignored the wrong register; DXC does not — the
previous port moved it to t1), t0 `cube` 3D LUT, cbuffer `gConstants { float avsLum; }` (unused).
`hdr[vIn.position.xy]` is an unfiltered integer-coordinate load; LUT sampled at `aces.rgb`
directly (no 32/33 half-texel rescale — slight LUT edge clamp, original behaviour).

---

## 6. Dependencies

Consumes:
- `Camera` (view/proj/frameHeight/focalLength/aspect/position) — for fog ray basis.
- `<dirRoot>/elevation/root4096.bil` **or** `<dirRoot>/gis/_export/root4096.bil`
  (Earthworks_4.cpp:144–145; see §8 F23 — which one exists is terrain-data-dependent).
- `<dirResource>/colorcubes/*.cube` (tonemap LUT), `<dirResource>/vegetation/dappled_noise_01.jpg`
  (terrain.cpp:738), `cube.fbx` skydome mesh + `vegetation.skyTexture`/`envTexture`
  (terrain.cpp:739–740, 1021–1031).
- `hdrPreviousFrame` from the frame loop (temporal tricks).

Provides:
- `terrainShadowTexture` + `global_sun_direction` → terrain, terrain sprites, vegetation,
  billboards, buildings, fog compute.
- `sunlightTexture`, `mainFar` in/outscatter (+ `_sky`) → every lit shader + skydome.
- `m_logEnd`/`m_oneOverK`/`_near` per FogVolume → `shaderLightBuffer` → LightsCB of all
  surface shaders (Earthworks_4.cpp:226–232).
- Sun basis vectors → vegetation dappled shadows.
- `atmosphere.getFar().inscatter/outscatter` + `sunlightTexture` are also handed to
  `plants_Root` at load (Earthworks_4.cpp:85–87).

Cross-subsystem couplings to respect: vegetation's per-vertex baked light cones (vegetation
catalog), the tile pipeline's `render_Tiles` shading (terrain catalog), buildings' use of
`render_Common` fog/shadow (render_Buildings_Far.hlsl:105).

---

## 7. Falcor API surface actually used

| Area | Calls |
|---|---|
| Resources | `Texture::create2D/create3D` (with init data ptr), `Texture::createFromFile`, formats RG32F/RGBA32F/RGBA16F/R11G11B10F/RGB32F/R32F, `BindFlags::UnorderedAccess\|ShaderResource\|AllColorViews`, `Fbo::create2D` (+Desc with depth), `Sampler::create` (clamp-linear aniso1; wrap-trilinear aniso8) |
| Binding | `computeShader`/`pixelShader` wrappers (project-local): `.load(path[, vs, ps, topology])`, `.Vars()->setTexture/setSampler/setBuffer`, `Vars()["CBuf"]["member"] = v` (per-member reflection writes — the dominant pattern, ~100 call sites in atmosphere.cpp alone), `getParameterBlock("gCfd")->findMember("T")` + indexed ShaderVar assignment |
| Execution | `.dispatch(ctx, x, y[, z])`, `.drawInstanced(ctx, vtxCount, instCount)`, `renderIndirect` (bake path), `RenderContext::updateTextureData`, `clearFbo`, `blit(srv, rtv)` |
| Camera/math | `Camera::getViewMatrix().getTranspose()` + `toGLM`, glm vector math, `glm::orthoLH` (veg shadow bake) |
| Misc | `FALCOR_PROFILE`, `Fbo::getColorTexture/getDepthStencilTexture`, detached `std::thread` |

Port-path note: everything here maps to plain Diligent (PSO + SRB + mapped cbuffers); the only
structurally Falcor-ish things are per-member reflection writes (replace with a CPU-side struct
mirroring the full cbuffer, uploaded whole) and the `gCfd` ParameterBlock (previous port
flattened it to 12 named textures + one explicit cbuffer at b4 — see §8).

---

## 8. Port drift notes (EarthworksFX vs extract) & mined F-findings

Code drift (diffed):
- `EarthworksFX/src/core/atmosphere.cpp` ≈ extract, plus: binds a dummy 1×1 cube to `envMap`
  (line 87) so the unused declaration is satisfied; `setSMOKE`/`setSmokeTime` rewritten against
  flattened `gCfd_*` names; `FALCOR_PROFILE` lines dropped.
- `EarthworksFX/hlsl/atmosphere/compute_volumeFog.hlsli`: `ParameterBlock<cfdTextures>` →
  12 loose `Texture3D gCfd_T_n` + explicit `cbuffer gCfdParams : register(b4)` for offsets/
  scales — DXC puts loose globals in `$Globals`, which Diligent-D3D12 rejects (annotated in the
  port file). Expect the same constraint in the re-port.
- `compute_volumeFogAtmosphericScatter.hlsl` (port): UAVs became `float4` +
  `[[vk::image_format("r11f_g11f_b10f"/"rgba16f")]]`; all stores padded to float4 — **F17**:
  SPIR-V storage-image format must match the view format or all loads/stores are undefined;
  DXC defaults to Rgba32f. `compute_sunlightInAtmosphere.hlsl` and `render_Common.hlsli`
  are unchanged (diff-clean).
- Tonemapper (port): `hdr` u0→t1 (DXC rejects SRV at a `u` register); vsMain rewritten to a
  6-vertex quad (a workaround for compat-layer draw bugs, F22 — **the original 3-vertex
  fullscreen triangle is fine for a native port**); added `debugView` diagnostics member to
  `gConstants` (changes that cbuffer's layout vs original).

F-findings that encode real semantics (EarthworksFX/BRINGUP_NOTES.md):
- **F19/F20**: `FogVolume::setCamera` is the one place core code *decomposes* the view matrix;
  `dir = W[2] * -1` assumes a right-handed glm view. Camera is RH, front faces CCW, depth [0,1]
  (`GLM_FORCE_DEPTH_ZERO_TO_ONE` must be a public define), FOV from 24 mm film-back focal length.
  When fog marched backwards the symptom was: blue wash (the :124 debug marker) + sun glow
  mirrored into terrain.
- **F23**: the heightfield path is data-set-dependent. Extract has
  `elevation/root4096.bil` **active** and `gis/_export/root4096.bil` commented "BAD - for STEG"
  (Earthworks_4.cpp:144–145); for the switserland_Steg data only `gis/_export/` exists.
  `_shadowEdges::load` fails **silently** on a missing file — add a loud error.
- **F26**: with the load-time placeholder (`(height-5, 0)`, g = 0) the shader shadow test
  degenerates to "black iff geometry > 5 m below the heightfield" → black patches on coarse LODs
  that pop away when tiles split. Symptom signature to remember: it means *the solved data never
  reached the GPU* (upload path broken), not a solver bug.
- **F16** (BRINGUP:279–283): the 2D `gInscatter_sky`/`gInscatter_cloudBase` were once
  misclassified as 3D by a substring match and crashed the device — bind by exact name.
- **F24**: `dirResource` resolution failures silently kill the colorcube (`loadColorCube() -
  failed` log) and the skydome mesh (black sky — the sun only reaches the screen via the skydome
  draw).
- Terrain-debug note (BRINGUP:109–124): final terrain pixel =
  `S * sunColor * (…) * albedo * outscatter + inscatter` — if any of shadow/sunlight/fog stages
  outputs zeros, terrain goes black; that composition is the first thing to instrument.

Developer-assessed state of the previous port: terrain + atmosphere "work well"; shadows were
confirmed working after F23+F26. So for the atmosphere subsystem the old port is a *behavioural*
reference (with the caveat that all its shaders carry the questionable DXC-era edits).

---

## 9. Bad code / removable candidates (OPINION — do not act without developer sign-off)

- `cascadeShadowMaps.{h,cpp}`, `terrainManager::shadowSetup/shadowRender*` and the `shadowMap`
  member (terrain.h:419–425): dead scaffolding; carrying it over only creates the illusion of a
  shadow-map system.
- `compute_volumeFogLights.hlsl` + `compute_volumeFogSmokeAndDust.hlsl`: reference a nonexistent
  include, never dispatched; `FogLights`/`FogVolumes` cbuffers, `fogDensities[32]`,
  `gLightVolume`, `mainNear`/`parabolicFar` allocations (atmosphere.cpp:75–76) are the dormant
  near-fog/light-volume system. Decision needed: port as dormant or drop (near-fog LightsCB
  scalars are already plumbed through — keep those for layout stability).
- Fake-cirrus noise block (compute_volumeFog.hlsli:309–316): 3 noise calls per froxel step,
  result unused. The `cfd.zw *= 0` line (:370) also disables the two colored-smoke inscatter
  terms below it. Cheap perf win if stripped, but it is inside `calculateStep` — verify against
  the pristine tree before touching.
- `sample_cfd` per-step probing with no cfd data (the stripped-smoke seam): either keep as the
  documented re-entry point for smoke or strip together with `gCfd`/`setSMOKE`/`setSmokeTime`.
- Duplicated `sunLight()` and `shadow()` (compute side vs render_Common side) — intentional
  (different samplers/softness), but the duplication invites divergence; document, don't merge
  blindly (the softness constants **differ on purpose**).
- `_shadowEdges::edge[4096][4096]` (16 MB) is only used by commented-out code; `Nx` could be
  computed on the fly. ~80–144 MB savings if trimmed — but only after shadows are proven working.
- `acosFast` (AtmosphericScatter:18–34) unused; `tmp_B/C/D` debug sliders; huge commented blocks
  (atmosphere.cpp:241–279, AtmosphericScatter:148–168, terrain.cpp:181–293).
- The `gInscatter[coord] = float3(0,0,1)` debug marker in the slice back-fill loop
  (AtmosphericScatter:124) overwrites the just-written correct value — in the original it is
  masked by the loop structure only for rays that terminated at the cloud base; it is the "blue
  wash" source whenever termination misbehaves. Recommend removing in the re-port *after* first
  light (it is also a useful diagnostic).
- Tonemapper `gConstants.avsLum` unused; the hardcoded 1.7943 exposure deserves a named constant.

---

## 10. Open questions / uncertainties

1. **Sun azimuth lock**: the CPU shadow solver only supports a sun traveling in the X/Y plane
   (§3). Is that an accepted product constraint for the port, or is a GPU replacement (the
   "REPLACED WITH GPU DATA" comment) expected within this effort? Affects whether to port
   `_shadowEdges` verbatim (recommended for milestone 1) or redesign.
2. **Sun animation trigger**: with the editor GUI stripped, nothing re-sets `requestNewShadow`
   after load. Where should the port expose it (debug UI toggle exists in the app shell)?
3. **mainNear / parabolicFar / FogLights**: port as dormant allocations for future smoke/near
   fog, or drop? (parabolic path also has plumbed cbuffer params `parabolicProjection` etc.)
4. **`FogVolume::setCamera` ordering quirk** (atmosphere.cpp:50–60): `compute_Params.sliceZero/
   sliceMultiplier/sliceStep` are assigned from `m_SliceZero/m_SliceStep` *before* those members
   are recomputed at the end of the function → the shader always sees last call's values, and
   the **first frame sees uninitialized floats** (members have no initializers, atmosphere.h:112–113).
   Harmless steady-state (camera params rarely change slice constants), but a rewriter must
   decide: reproduce or fix. Flagging, not guessing, per instructions.
5. **`updateFogparameters` partial copy** (atmosphere.cpp:19–25): only 4 of ~30 params are copied
   from `params` into each volume's `compute_Params`; `computeVolumetric` then uploads
   `mainFar.compute_Params` for most fields but `common`/`params` directly for others. The
   split looks accidental but works because defaults match — another reproduce-or-fix decision.
6. **`sunColour`/`numLights`/`Lights[128]`** in LightsCB are never written by surviving code —
   is the gpu_LIGHT path (gpuLights_functions.hlsli) consumed anywhere at runtime in the pristine
   tree (e.g. glider night flights)? Extract says no; pristine not exhaustively checked.
7. Whether the pristine original still calls `setSMOKE`/`setSmokeTime` from the cfd code path is
   assumed (cfd.* was deleted from the extract); the seam is noted but the exact original call
   sites were not re-verified against `C:\dev\git\os\Earthworks_4-F5_2`.
8. The skydome shader hardcodes screen sizes (2550×1440 / 4096×2160) for its bicubic UV
   (render_triangles.hlsl:145–149, 166–167) — presumably tuned for the author's monitors; the
   port should feed real screenSize but must verify the visual match.
