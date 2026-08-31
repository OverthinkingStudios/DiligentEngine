# Port Effort 3 — Analysis & Decision

**Status: Phase 3 executing (plan: `execution/plan.md` — its step table is the single source of
progress truth). Steps 1-4 DONE and developer-verified (2026-08-04): full terrain + bake +
atmosphere + shadows + HDR render on Vulkan AND D3D12 (~900-1.2k fps D3D12), and the complete
vegetation system is ported and runs DORMANT (the Steg dataset ships no plant data — visual
validation deferred until it does). Step 5 (terrafectors + roads bake, faithful descriptor
arrays — bindless deferred by developer choice) is DONE, developer-verified 2026-08-04 on
D3D12 (~850 fps). The "VK-only solid-white terrafectors" follow-up is RESOLVED (2026-08-31:
clamp argument-order UB — see plan.md step log / bringup_notes P16,
`vulkan_flat_sampling_handoff.md`); the rare both-API hang remains open. New follow-up: the
visible terrain-hole culling bug is fixed (bringup_notes P17), but the underlying tileCenters
readback starvation in interactive sessions stays OPEN — handoff:
`readback_starvation_handoff.md`. Root-cause log in `execution/bringup_notes.md` (P1-P17).
Next: step 6.**

## 0. Ground rules (for every agent working on this)

- **Git policy: NEVER commit — with one exception: local commits on the `port_effort_3`
  branch of the main repo (DiligentEngine), which are allowed and wanted. Never push, never
  touch other branches.** Subagents never commit at all; the main agent commits after review.
- Submodules **`Earthworks/` and `Earthworks/extern/OTSCommon/` stay on master, unchanged** —
  keep all changes in the main repo.
- Original sources live in **`port_effort_3/source_extract_3/`** (under git on this branch;
  fresh extract of the up-to-date original). This is our working copy — it may be stripped/
  annotated. The pristine reference is `C:\dev\git\os\Earthworks_4-F5_2\` (READ-ONLY, never
  modify). `source_extract_2/` (stale earlier extract) was removed after comparison — it lives
  in git history; `docs/source_extract_2/` is read-denied — don't touch.
- CLAUDE.md files (root, EarthworksFX) were rewritten 2026-08-02 to match this mission — keep
  them consistent with task.md. Do not modify the legacy findings docs (`BRINGUP_NOTES.md`,
  `PROJECT_OVERVIEW.md`, `MIGRATION.md`, TESTFLIGHTS.md) — they are historical reference.
  New work products go under `port_effort_3/`.
- Never compile/run/test on your own initiative — the developer does that and reports back.

## 1. Goal (the decision criterion)

> **The shortest path to a stable rendering environment that renders terrain, vegetation,
> shadows, terrafectors and roads at comparable-or-better fps than the original.**

Everything in this effort is judged against that sentence. "Cleaner code" matters only where it
serves stability or speed of getting there.

**First milestone (developer, 2026-08-02): get the new port rendering inside
`EarthworksFXSample` as early as possible — early insights beat completeness.** There is no
runnable original build; "comparable fps" is judged against remembered numbers, so an early
running state is also our only way to calibrate performance.

## 2. Problem statement

The current port (`EarthworksFX/`) kept the original Falcor-era Earthworks 4 sources ~byte-identical
and faked the Falcor API on Diligent (compat layer: `interface/Falcor.h` + `src/compat/*`, ~5.9k
lines of new code). Rationale was minimal port effort + easy re-application of upstream changes.
Assessment after many weeks:

- Upstream churn on the originals is minimal — the re-apply goal solved a near-nonexistent problem.
- Most bugs were introduced in the compat layer (see `EarthworksFX/BRINGUP_NOTES.md`); still not
  in a working state.
- Even at success, the codebase stays saturated with Falcor idioms needing a second cleanup pass.

**Question to answer:** finish the current port, or restart from the originals and port directly
to Diligent idioms — dropping the "never touch original code" dogma, carrying over the good,
discarding the bad. All options are genuinely open, including "Path A is fine after all".

## 3. Known facts

### Code sizes (2026-08-02)

| Body of code | Size |
|---|---|
| `EarthworksFX/src/core/` (ported originals) | ~33.4k lines C++ (vegetationBuilder 9k, terrain 6.6k, roadNetwork 2.6k, terrafector 2.3k) |
| `EarthworksFX/hlsl/` (ported shaders) | 59 files, ~11.1k lines |
| Compat layer (`interface/Falcor.h` + `src/compat/`) | ~5.9k lines, all new, exists only to fake Falcor |
| `EarthworksFX/src/app/` (Diligent host shell — keep regardless of path) | ~2.5k lines |
| `port_effort_3/source_extract_3/` (fresh original sources incl. hlsl, **stripped**) | 101 files, ~62.6k source lines (24.8k of that vendored json.hpp → ~37.8k real Earthworks code) — see `extract3_inventory.md` (documents the PRE-strip state) |

### Current port state (developer assessment, 2026-08-02)

- **Works well:** terrain, atmosphere, quadtree splitting, buildings; decent debug shaders.
- **Broken:** terrafectors draw "in principle" on D3D12, broken on Vulkan; tiles being sent to
  y=0 where some terrafectors are placed — unresolved for weeks.
- **Performance:** not shabby, but ≥100–200 fps below the original.

### Shaders

- The original shaders went through **significant porting** (DXC switch forced changes), done by
  a less capable model; quality is questionable (e.g. an agent once removed cbuffer byte padding).
  **Decision: shaders get re-ported fresh in this effort**, using the originals as source of truth
  and the current port only as a DXC-issues reference.
- **Zero-warning policy** applies, including shaders unless that proves impractical there.

### ⚠ The extract is STALE (found 2026-08-02)

Commit `b360417` (2026-06-21, "imported up-to-date Earthworks files") pulled a **newer**
Earthworks snapshot into `EarthworksFX/` than what `source_extract_2/` contains:

- Files in the port with **no counterpart in the extract**: `cascadeShadowMaps.*` (the
  shadows!), `earthworksScene.*`, `vegetationBuilder_Trees.cpp`, `terrainGenerator.*`,
  `lru_cache.h`; shaders `compute_vegetation_sortCombine.hlsl`, `extractTextures.hlsl`,
  `compute_sampleRGBtoPixel.hlsl` (`viewRenderData_lookupBuffers.hlsli` turned out to be a
  port-side creation, not import content — extract3_inventory.md surprise 2).
- Content drift: extract `vegetationBuilder.cpp` is 4.3k lines vs 7.1k in the port;
  `render_vegetation_ribbons.hlsl` had a 1260-line change in that import.
- (`buildings.*`, `EarthworksDebug.h`, `TestFlightData.h`, `debugGrid.hlsl` are genuinely
  new port-side code — `buildings` replaced a glider.h-entangled feature; keep.)

**Resolved:** the developer fetched the original project to
`C:\dev\git\os\Earthworks_4-F5_2\Source\Samples\Earthworks_4\` (read-only reference; sandbox
read access granted in settings). It is newer than everything (contains e.g.
`terrain_Builder_GIS.*`, absent even from the June import) and restructured
(`earthworks_scene/` subdir). A fresh extract lives in `port_effort_3/source_extract_3/`
(see `extract3_inventory.md`); it supersedes `source_extract_2/` as source of truth.
`cascadeShadowMaps.*` exists in the original → shadows question resolved.

### Strip decisions (developer, 2026-08-02)

- **Editor functionality is DEFERRED, not dead** — this port phase does not carry
  EarthworksEditor's roads editing / vegetation paint; the editor GUI was stripped from the
  extract for token efficiency. It WILL be ported at some later point (quick results matter
  then too) — the pristine tree (`Earthworks_4-F5_2`) is the source for that, plus the
  `// STRIP-REVIEW:` tags marking where GUI coupled into build logic.
- **terrain_Builder_GIS.*: stripped immediately** (dead code; no delta-mining).
- **roads_AI: strip; roads_physics: keep** (surface queries may serve future consumers).
- **terrainGenerator.*: full first-class port** like any other subsystem.
- Deleted outright (13 files, ~26.5k lines): cfd.*, glider.*, glider_runtime.cpp, latexgen.h,
  terrain_Builder_GIS.*, roads_AI.*, render_GliderWing/cfdSlice/thermalRibbons.hlsl.
- **In-file surgery DONE** (commits `cd27007`…`2ced903`, each diff reviewed): editor GUI
  stripped from ecotope, terrafector, roadNetwork, roads_materials, vegetationBuilder(+PDF/
  LaTeX), terrain (+glider/cfd/GDAL-tooling), Earthworks_4 (+thumbnail overlay); roads_AI
  integration code removed from roadNetwork/terrain; `imGuiHelper.h` deleted. Deliberate keeps
  are tagged `// STRIP-REVIEW:` in-code (11 tags — grep for them). Every strip verified: only
  original lines survive (byte-identical), zero non-justified ImGui/glider/cfd/hpdf refs remain.
- Editor-leaning shaders all turned out to be referenced by surviving code → all four kept
  (`render_spline`, `compute_terrain_under_mouse`, `extractTextures`, `compute_sampleRGBtoPixel`).
- **terrainGenerator GUI stays in the extract** (developer decision): it is the pipeline
  tool's own frontend and should survive the port with minimal edits — don't over-invest,
  but don't strip it.

### Calibration hints from the developer (hints, not decisions — let facts decide)

- Where ported code is verified good, shortcut it (don't re-port for purity). Known examples:
  `buildings.*` works very well and has culling the original lacks; tiling is probably fine.
- Terrafectors definitely have issues.
- Avoid bias in both directions; per-subsystem facts (catalog + diff evidence) decide.

### Knowledge to mine (do NOT re-derive)

- `EarthworksFX/BRINGUP_NOTES.md` — F-numbered root causes; several encode real engine semantics
  (RH camera, CCW front faces, non-indexed 16-byte indirect args, depth [0,1], film-back FOV).
- `EarthworksFX/PROJECT_OVERVIEW.md` — renderer internals, camera/matrix conventions.
- `EarthworksFX/MIGRATION.md` — port state, remaining compat gaps.

## 4. Candidate paths (open — decided in Phase 2)

- **A — Finish current port:** keep fixing the compat layer. For: core code known-good by
  construction; much already renders. Against: the shim keeps generating bugs (it reimplements
  Falcor semantics from memory); Falcor-flavored end state + cleanup pass anyway.
- **B — Clean re-port from originals, direct to Diligent:** subsystem-by-subsystem, native
  Diligent idioms (PSOs, SRBs, real reflection), guided by the Concept Catalog (§5). Original
  code may be changed where *understood* to be bad. Against: re-losing subtle behavior; bring-up
  order/testability needs a plan.
- **C — Get current port working, then strangle the shim:** only attractive if we're close to
  working; every dissolution step is unverifiable while broken.
- **D — B, but salvage:** port unit = already-C++-ported `EarthworksFX/src/core` file; replace
  only its GPU-API surface with direct Diligent calls. Keeps the C++-side conversion work.

Decision criteria: distance-to-goal (§1) per path; risk of losing hyper-tuned behavior;
reviewability by cold-context agents; end-state code quality (secondary).

### Decisions already made that shape the paths

- Shaders are re-ported fresh regardless of path (§3).
- If B/D: target tree is **`EarthworksFX/` itself** — move `src/core/` aside, start fresh there;
  carry over the app shell (`EarthworksFXApplicationBase`, `TestFlightController`, debug UI,
  testflights). Old code stays available as reference, not as a template.

## 5. Concept Catalog (Phase 1 — needed regardless of path)

The original code mixes brilliant and bad; past agents repeatedly "improved" what they didn't
understand and lost 200 fps or broke visuals. Before any rewrite executes, produce one doc per
subsystem in `port_effort_3/catalog/<subsystem>.md`: what it does, core tricks, invariants,
perf-critical details that MUST survive, bad code that can go, dependencies, Falcor API surface
actually used. Cold-context agents write; warm-context main agent reviews against the code.

Subsystems (refine during Phase 0.5):

1. Terrain quadtree + tile streaming (split/merge, JP2 streaming, tile-bake compute chain, LOD)
2. GPU tile pipeline (tileBuildLookup, frustum flags, indirect draw generation — non-indexed 16-byte args)
3. Vegetation (vegetationBuilder, clip/LOD compute, billboards, ecotope)
4. Roads / terrafectors (bezier network, ribbon builder, terrain-affecting stamps)
5. Atmosphere / sky / fog / **shadows** (locate the shadow tech in the originals — it's a §1 requirement)
6. Shaders (per-pass: entry points, bindings, defines, CPU-side feeders)
7. Cross-cutting conventions (coordinates, handedness, units, compass, depth range, file formats)

## 6. Phases

- **Phase 0 — Unblock & frame** ✅ (this doc; access solved via local copy + settings;
  branch created; decisions in §0–§4 manifested)
- **Phase 0.5 — Fresh extract + strip** ✅ (commits `7bae949`…`4872073`): up-to-date extract →
  `source_extract_3/` (inventory: `extract3_inventory.md`), then reviewed per-file surgery
  removing glider/cfd/editor-GUI/PDF/AI code (~38k lines stripped total; details §3).
- **Phase 1 — Concept Catalog** ✅ (§5 — all 7 catalog docs exist in `catalog/`).
- **Phase 2 — Path decision**: with the catalog, estimate each path honestly
  (per-subsystem effort, bring-up order, testability); developer decides.
- **Phase 3 — Execution plan**: per-subsystem task specs for cold-context agents: inputs
  (catalog doc, sources), rules (what must survive; which CLAUDE.md rules are suspended),
  deliverable, review checklist, verification (developer compiles/runs; testflights).

## 7. Notes / risks

- CLAUDE.md files (root, `EarthworksFX/`) were rewritten 2026-08-02 to match this mission —
  the old "algorithm code is sacred / sync freeze" premise is retired. They are git-excluded
  (local only).
- No runnable original build exists — perf comparisons are against remembered numbers until
  the first milestone (§1) renders.
- `docs/source_extract_2/` remains read-denied — agents must use `port_effort_3/source_extract_3/`.

## 8. Open questions for the developer

*(none currently)*
