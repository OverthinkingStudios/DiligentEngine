# Port Effort 3 — Bring-up notes (P-numbered root causes)

Pattern copied from the previous port's BRINGUP_NOTES.md (F-numbered): every non-obvious root
cause found during B+ bring-up gets an entry. **Check here before re-investigating a symptom.**
All entries developer-verified unless marked otherwise.

- **P1 — D3D12 GenerateMips crash, VK fine**: manually created SRVs do not inherit mip-generation
  support from `MISC_TEXTURE_FLAG_GENERATE_MIPS`; the view needs
  `TEXTURE_VIEW_FLAG_ALLOW_MIP_MAP_GENERATION` or D3D12 crashes on null
  `m_MipGenerationDescriptors`. Fixed centrally in `ewResources.cpp createTextureView`.
- **P2 — terrain data resolution**: settings file is `<name>.terrainSettings.json` (scan the dir,
  don't hardcode); its dir fields are gameroot-relative with leading `/` and the absolute bases
  historically came from lastFile.xml — the bootstrap derives gameroot = terrain dir's
  grandparent. `-terrain <dir-or-json>` command line bypasses lastFile.xml entirely.
  `requireFile()` (terrain.cpp) logs + `__debugbreak`s (debugger-attached only) on missing
  required files — use it for every new required file.
- **P3 — OpenJPH signature drift**: current `codestream::pull()` takes `ojph::ui32&`, the
  original passed `int` — adapt, don't pin old OpenJPH.
- **P4 — setCamera takes STANDARD glm matrices**: real Falcor's `rmcv::toGLM` is a
  math-preserving converter; the frustum-plane extraction in `terrainManager::setCamera` is
  textbook Gribb-Hartmann on a standard glm projection, and `view * boundingSphere` wants the
  plain view matrix. Feeding transposed matrices INVERTS the culling (only rim/far tiles
  visible). Do not "fix" the `viewProj = view * proj` line — it is dead code (render path
  uploads `transpose(P*V)` straight from the camera).
- **P5 — async readback + pool-slot reuse = bottom-tile flicker**: the original read
  `tileCenters` back with a same-frame stall; our fence ring is 1-2 frames late, and quadtree
  pool indices are recycled on split/merge — stale data patched fresh tiles' bounding spheres
  with the slot's previous occupant's height, so the frustum test culled visible near tiles
  (screen bottom, motion-correlated, timing-sensitive). Fix: `ReadbackBuffer` slot tags +
  `quadtree_tile::bornFrame`; patch only when data is newer than the tile's allocation.
  **Lesson for every future async readback (GC_feedback, vegetation feedback): age-gate any
  per-slot data that can be recycled.**
- **P6 — red-herring warning**: the flicker initially correlated perfectly with unrelated
  gameroot shader-file shuffling (worked with stale legacy files present, broke without) —
  pure timing coincidence. The gameroot is rclone-synced (`.conflict*` files); treat
  "impossible" file-correlated behavior with suspicion and reproduce twice before believing it.
- **P7 — name collisions with original enums**: `_terrainMode`'s plain-enum values leak
  (`textureTool` collided with TextureSplitTool's class) — made `enum class` (values unchanged;
  cereal-serialized as int, order must never change).
- **P8 — deploy pipeline**: POST_BUILD DeployHLSL copies `EarthworksFX/hlsl` →
  `%ACSMP_GAMEROOT%/hlsl` (copy_directory, no clean). Stale legacy files in the gameroot are
  inert for correctness (all loads + include closure resolve to our deployed set) but mask
  missing-file errors — clean the gameroot hlsl dir when a step adds new shaders.

- **P9 — uninitialized-by-luck cbuffer feeders (found during step 3 by inspection; preventive — never seen as a live bug)**:
  the original left most `fogAtmosphericParams` members and `FogVolume::m_SliceStep/m_SliceZero`
  UNINITIALIZED; it worked because `std::make_unique<Earthworks_4>()` value-initializes the whole
  renderer object, zeroing them by accident. Any port that constructs the renderer differently
  inherits garbage — e.g. `parabolicProjection != 0` silently flips the fog compute into its
  parabolic-projection path (sky garbage with no error anywhere). Step 3 gives every member an
  explicit initializer; when adding future cbuffer-feeder structs from the extract, check their
  member initializers before trusting them.

- **P10 — fullscreen passes are back-culled unless cull is explicitly NONE**: the ew layer
  forces front=CCW at PSO build (F20) and defaults to cull-back; the standard fullscreen
  triangle from `(vId<<1)&2` winds CLOCKWISE in screen space, so the tonemapper drew nothing
  (blue clear colour on both APIs, even in its solid-colour debug view — the give-away that the
  PASS was missing, not its inputs). The extract masked this by borrowing `graphicsState`'s
  rasterizer, which was created CullMode::None. Rule: every fullscreen/post pass ported from the
  extract needs an explicit cull-NONE rasterizer state; check which state object the original
  *borrowed*, not just what the pass itself set.

- **P11 — headers added to terrain.h's include closure reach EVERY app TU (found during step 4 by inspection; preventive — never seen as a live bug)**:
  `EarthworksFXApplicationBase.hpp` includes `Earthworks_4.h` → `terrain.h`, so anything terrain.h
  includes lands in TextureSplitTool/Editor/TerrainGenerator translation units. Step 4's
  `vegetationBuilder.h`/`terrafector.h` immediately collided twice with TextureSplitTool:
  the `archive_float2/3/4` macros (redefinition warning C4005 — now `#ifndef`-guarded on both
  sides) and `class oneTexture` (outright class redefinition — the tool's field-identical local
  copy was removed; it now uses the engine's, same CEREAL version 101). P7's enum-leak lesson
  generalized: when a step grows terrain.h's closure, sweep the tool apps for duplicate names
  (macros, plain enums, helper classes the tools copied out of the original code).

- **P12 — Vulkan independent blend is OFF unless requested at device creation (found during step 5 by inspection of DiligentCore; the catalog's H6 hypothesis CONFIRMED — not yet developer-verified)**:
  `DeviceFeatures::IndependentBlend` defaults to `DEVICE_FEATURE_STATE_DISABLED`
  (GraphicsTypes.h:1723) and `EngineFactoryVk` enables the VK `independentBlend`
  device feature only when the state is ENABLED (EngineFactoryVk.cpp:876). The
  8-MRT terrafector bake NEEDS per-RT blend (RT0 elevation One/InvSrcAlpha vs
  SrcAlpha/InvSrcAlpha on RT1-7) — without the feature the PSO is invalid
  Vulkan while D3D12 doesn't care. Very plausibly the previous port's
  "terrafectors broken on Vulkan" root cause. Fixed in the app shell
  (`EngineCI.Features.IndependentBlend = ENABLED` on VK and D3D12); the ew
  layer additionally logs the effective per-RT blend at every
  independent-blend PSO build. Rule: any Diligent *device feature* a pass
  depends on must be requested at engine init — PSO-time is too late.

- **P13 — extract quirk: road LOD-bin GPU upload lives inside the EVO file-write blocks (found during step 5 by inspection; preventive)**:
  `bezierRoadstoLOD` does `splines.indexData_LODn->setBlob(...)` INSIDE
  `if (file && datafile)` where the FILEs are `<dirRoot>/bake/roadbeziers_*.gpu`
  side-exports. If `<dirRoot>/bake/` doesn't exist, fopen fails and the road
  layers silently never reach the LOD-binned bake buffers → roads bake empty
  with zero errors anywhere. The port creates the directory at terrain load
  and errors loudly if the files still can't be opened (structure kept —
  splitting the upload out of the file-write block would be the real fix, but
  it changes a hyper-tuned code path for no behavioural gain).

- **P14 — pre-v101 .terrafectorMaterial files crash the load (found during step-5 bring-up, first launch)**:
  `terrafectorEditorMaterial::import()` deserializes with a HARDCODED version
  (`TFMATERIAL_VERSION_LOAD` = 101) and the file embeds none, so a material
  written before the v101 `isStamp` fields (Steg's
  `terrafectors/20_base/LOD2/forests.terrafectorMaterial`) makes cereal throw
  "provided NVP not found" — never caught in the extract → crash during
  `terrafectorSystem::loadPath` right after "add mesh - ...001_forest.fbx"
  (the material import happens inside the mesh's cache-load path). Port 2's
  band-aid was a hardcoded early-return on that exact absolute path (legacy
  terrafector.cpp:1568). The first fixed run then showed the gameroot is FULL
  of pre-v101 materials (asphalt_17, all road markings, cobblestones, ...) —
  no-op-ing them guts the road look. Fixed generally: the cereal read is
  exception-guarded and RETRIES AS v100 (lossless — the version gate only
  guards isStamp/stampWidth/stampHeight); only a file failing both reads
  stays a loud no-op (useElevation=0, checklist-d semantics). Additionally
  001_forest.fbx is skipped in loadPath (developer decision — stale Steg
  forest data, replaced by the coming plant-data update; STEP5-STUB tagged).
  Rule: every manual-version cereal read (`serialize(archive, N)` — road
  networks, terrafector materials) is a crash risk on older files; guard it.

- **P15 — rootFolder backslashes made EVERY road material "outside rootFolder" (step-5 bring-up, first working run)**:
  `roadMaterialCache::find_insert_material` forward-slashes the INPUT path but
  prefix-compares against `terrafectorEditorMaterial::rootFolder` VERBATIM.
  The original workflow fed forward-slash paths from lastFile.xml; the port's
  bootstrap builds dirResource via std::filesystem (backslashes) → the prefix
  test failed for every .roadMaterial → all road layers fell back to material
  0. Fixed: rootFolder is cleanPath()-normalized once at assignment
  (terrain.cpp onLoad). Rule: any extract code that string-compares paths
  assumes forward slashes — normalize at the source, not per call site.

Known open items (not bugs): VK vsync-off doesn't uncap fps (pre-existing; step 6 perf pass);
`compute PSO unavailable`-style shader failures are non-fatal by design — check the log.
Resolved in step 3: `gHDRBackbuffer` now gets the real hdrFbo colour; the "texture bound as
render target will be unset" Info is silenced (GpuContext::unbindRenderTargets before the
terrain-under-mouse dispatch + inside copySubresource).
