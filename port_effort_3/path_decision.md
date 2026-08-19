# Phase 2 — Path assessment & decision

**Status: DECIDED 2026-08-02 — developer approved Path B+ (clean re-port from extract_3,
selective salvage). Execution plan: `execution/plan.md`.**
**Vegetation caveat (developer): the original's last-known-good terrain-vegetation integration
state is unknown ("no idea"), and the port never showed plants because the terrain dataset
contains NO plant data. Consequences: port vegetation per best-guess from the catalog (extract
code is authority); visual validation deferred until plant data exists; getting/making plant
data is a future developer task.**
Evidence base: the seven `catalog/*.md` docs (commits `9547be2`…HEAD), `extract3_inventory.md`,
`EarthworksFX/BRINGUP_NOTES.md`. Goal per task.md §1: shortest path to stable
terrain+vegetation+shadows+terrafectors+roads at comparable-or-better fps; first milestone:
anything rendering in `EarthworksFXSample`.

## 1. What the catalog changed about the picture

1. **The Falcor problem is small.** The whole engine touches ~30 Falcor types / ~120 methods,
   and almost all GPU traffic funnels through two ~140-line wrappers (`computeShader`,
   `pixelShader`) plus ~14 direct `RenderContext` calls (conventions doc §7). A native port
   needs real semantics for roughly five types (Texture, Buffer, Fbo, the two wrappers).
   The 5.9k-line shim reimplemented far more surface than the code ever used.
2. **The renderer is architecturally friendly to Diligent.** Bufferless (SV_VertexID +
   structured buffers), one shared 128-quad IB, few PSOs, 2 CPU draws + 2 dispatches per view
   for terrain (shader_interface, gpu_tile_pipeline docs).
3. **The current port's content is stale.** Port shaders/core are the June-21 snapshot; the
   original moved on (e.g. port `render_vegetation_ribbons` lacks `camRight/camUp/toneMap`;
   vegetationBuilder drift ~4k lines). Finishing Path A still requires a content re-import —
   i.e. a large chunk of "re-port" work is owed on every path.
4. **The two blocking bugs now have localized mechanisms** (roads_terrafectors §3,
   gpu_tile_pipeline §5): y=0 = elevation REPLACE-blend writing ~0 with alpha~1 during tile
   bake (zeroed TF_material / stub texture / RT0 per-target blend mistranslation on Vulkan);
   original cannot produce y=0 otherwise. This helps Path A somewhat — but the same knowledge
   transfers to any path.
5. **The perf gap (≥100–200 fps) is unattributed.** BRINGUP_NOTES point at shim overhead
   (per-draw PSO/SRB lookups, CPU-shadow buffer uploads, same-frame readbacks); no measurement
   isolates it. Paths that keep the shim keep the uncertainty.
6. **"Shadows" = the CPU shadowEdges solver + baked vegetation lighting**; there is no realtime
   shadow-map system to port (atmosphere_shadows §1). Scope is smaller than feared.

## 2. Paths

### A — Finish the current port (fix compat layer)
- **For:** most subsystems already render on D3D12; bug mechanisms now localized; least new code.
- **Against:** built on stale content (see §1.3) — catching up means re-importing the newer
  originals *through the freeze/1:1 discipline that just failed*; perf gap likely structural to
  the shim (ShaderVar maps, CPU shadows, PSO cache per draw); end state stays Falcor-flavored
  and needs the cleanup pass anyway; Vulkan path still broken.
- **Honest estimate:** possibly the fastest to "D3D12 mostly works again" (bug fixes + content
  re-import), but not credibly the fastest to the §1 goal (stable + both-bugs-dead + fps parity),
  and it produces the codebase we already decided we don't want.

### B — Clean re-port from extract_3, direct to Diligent
- **For:** extract is current, stripped (~37.8k lines), and now cataloged; Falcor surface tiny
  (§1.1); renderer Diligent-friendly (§1.2); every hyper-tuned invariant is written down with
  file:line; shaders re-ported from the authority copy; perf uncertainty removed at the source
  (native PSOs/SRBs, explicit barriers, latency-ring readbacks).
- **Against:** biggest single-shot effort; bring-up ordering must be planned so something renders
  early; risk of re-losing subtleties (mitigated by catalog + STRIP-REVIEW + testflights + the
  old port as behavioral reference).
- **Honest estimate:** slower to *first* pixel than A, but the first milestone is deliberately
  small ("anything in EarthworksFXSample"), and progress is monotonic — no shim archaeology.

### C — Get A working, then strangle the shim
- Dominated: pays A's full cost first, then B's migration cost on top. Only sensible if A were
  nearly done — it isn't (Vulkan broken, content stale). **Drop.**

### D — B but salvage the port's `src/core` files as the C++ base
- **Against (new evidence):** the port's core files are the *stale* June content — salvaging them
  re-imports the staleness that already bit us once; their GPU-API surface is exactly what we're
  replacing, so the "already compiles here" advantage evaporates after surgery.
- **Keep from D anyway (selective salvage into B):** the app shell (`EarthworksFXApplicationBase`,
  TestFlightController, testflights, `EarthworksDebug.h` toggles), `buildings.*` (works, has
  culling the original lacks), the F-numbered fixes catalog, the port's DXC techniques and its
  deliberate `compute_tileVertices` median-of-5 tile-hole fix (keep knowingly), debugGrid/debug
  shaders. **As components, not as the base.**

## 3. Recommendation

**Path B with selective salvage (B+).** Base = `source_extract_3/` (current, stripped,
cataloged). Target = `EarthworksFX/` with old `src/core`+`hlsl`+compat moved to a reference
location. Replace the Falcor surface with a thin *purpose-built* native layer: keep the
`computeShader`/`pixelShader` wrapper shape (the code's own abstraction, ~300 lines) implemented
directly on Diligent — not a Falcor-semantics emulator. Salvage the proven port-side components
listed in §2-D. Shaders re-ported from the extract with the port as DXC-technique reference,
binding by name, layouts per shader_interface.md.

Suggested bring-up order (first milestone fast, each step visible in EarthworksFXSample):
1. **Shell + native layer**: app shell hosts new `terrainManager` skeleton; Texture/Buffer/Fbo/
   wrapper layer; debugGrid renders → *milestone: anything on screen*.
2. **Terrain minimal**: quadtree + JP2 streaming + tile bake (clear→generate-path only) +
   render_Tiles with flat shading → terrain silhouette on screen.
3. **Full tile bake** (ecotopes, floodfill, bc6h, normals, vertices) + atmosphere/fog + shadowEdges.
4. **Vegetation** (clip/lod/sortCombine + ribbons + billboards).
5. **Terrafectors + roads bake** (with the y=0 checklist from roads_terrafectors.md §3 applied
   from day one: per-RT blend translation test, TF_material byte-contract asserts, bake-camera
   transpose test).
6. Buildings (salvage), sprites, tonemapper polish, perf pass vs remembered numbers.

Known decision points inside B+ (flagged, decide during Phase 3, not now):
- `compute_tileBuildLookup`'s Slang `ParameterBlock<views>` (3×18 RW buffers): flatten like the
  port did vs restructure (shader_interface.md).
- Same-frame GPU readbacks (tile min-height, GC_feedback): keep-with-fence vs 1-frame latency ring
  (terrain_quadtree_streaming.md §7).
- Keep port's median-of-5 tile-hole fix (recommended: yes, with comment).

## 4. What would change the recommendation

- If the developer values "D3D12 demo soon" over the §1 goal → A first, B later (accepting ~2×
  total cost).
- If terrain-vegetation integration in the *original* is genuinely mid-construction
  (vegetation.md §10 suspicion), parts of vegetation may need the developer's last-known-good
  guidance regardless of path.

**Developer decides.**
