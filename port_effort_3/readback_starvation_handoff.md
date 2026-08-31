# OPEN: tileCenters readback starvation in interactive sessions

Written 2026-08-31. Status: **mitigated, root cause unknown.** The visible bug
(tile-shaped terrain holes) is FIXED by a conservative culling fallback; this
handoff is the follow-up investigation into WHY the GPU->CPU readback that
feeds tile heights completes on only a fraction of interactive frames. Read
`CLAUDE.md` (repo root) first — especially: DiligentCore is READ-ONLY (audits
fine, edits forbidden), never commit, the developer runs interactive tests and
reports back.

## 1. The measured symptom

`ew::ReadbackBuffer::mapCompleted()` (the fence-checked, non-blocking map of
the tileCenters staging ring) returns data on:

- **testflight runs: ~89–99% of frames** (healthy; lag 1–2 frames)
- **interactive sessions: 4% and 50%** in the two sessions measured
  (`holes_interactive_2026-08-31_14-32-28.txt`: mapped 31/803;
  the 15:26 session: mapped 3731/7517)

When a map succeeds, lag is always 1–2 frames — the failure is binary
(nothing maps), not a growing queue. A GPU drain reliably un-sticks it:
clicking the window title bar (modal message-loop stall) or a testflight
screenshot capture made the next map succeed, which is why the hole bug healed
on click and never showed in flights.

## 2. Why it matters (even after the fix)

Tile bounding-sphere heights and `origin.y` are patched from this readback
(terrain.cpp, readback block, search `buffer_tileCenter_readback`). Under
starvation, freshly split tiles keep parent-inherited heights for seconds.
The culling side is now safe: `quadtree_tile::heightPatched` +
`tileInFrustum()` (terrain.cpp) test unpatched tiles as a column over
`kTerrainYMin..kTerrainYMax`, so wrong heights can no longer cull on-screen
tiles (that was the hole bug — see §5). But stale heights still degrade
`testForSplit`'s distance/LOD quality, and the conservative column test keeps
a few extra tiles alive per frame while starved. Fixing the starvation
restores 2–3-frame freshness everywhere.

## 3. Code anchors

- `EarthworksFX/src/gpu/ewResources.h` ~158–205: `ReadbackBuffer` — ring of
  `kSlots = 3` `USAGE_STAGING`/`CPU_ACCESS_READ` buffers plus one fence.
- `EarthworksFX/src/gpu/ewResources.cpp` ~450–545:
  - `enqueueCopy()`: `CopyBuffer` into the current slot, then
    `EnqueueSignal(m_Fence, value)` (comment claims this flushes), slot/value
    bookkeeping. Called once per `terrainManager::update()`.
  - `mapCompleted()`: `m_Fence->GetCompletedValue()`, picks the NEWEST slot
    with `slotValue <= completed`, then
    `MapBuffer(slot, MAP_READ, MAP_FLAG_DO_NOT_WAIT)`. Two distinct null
    paths: (a) no slot fence-complete (`best < 0`), (b) `MapBuffer` returns
    null despite the fence saying complete. **Which one fires is currently
    unmeasured — instrument this first** (two counters in `HoleStats`, see §4,
    is the cheapest way).
- Caller: `EarthworksFX/src/core/terrain.cpp`, search
  `buffer_tileCenter_readback->enqueueCopy` — enqueue with
  `tag = m_frameCounter`, map, patch loop with `bornFrame < dataFrame` guard.
- A second instance exists for `GC_feedback` (picking/metrics) — check whether
  it starves too; that tells you if the problem is per-buffer or systemic.

## 4. Measurement tooling already in place

`ew::gDebug.holeStats` (EarthworksFX/interface/EarthworksDebug.h) records per
terrain-update frame: `readbackMapped`, `readbackLag`, splits/merges,
born-guard skips. Output:

- every testflight run writes `<rundir>/holes.txt` — line 4 is the readback
  health summary; VERDICT on line 2 must stay 0 (regression guard for the
  hole fix).
- interactive sessions auto-dump
  `<UserData>/testflights/holes_interactive_<ts>.txt` at app exit — but ONLY
  when pathological (y_mismatch > 0 or mapped < 75%), see
  `TestFlightController::DumpInteractiveHoleStats()`. While starvation is
  unfixed, every interactive session leaves one; when you fix it, the dumps
  stop appearing — that IS the success signal.

Build/run recipe: `port_effort_3/vulkan_flat_sampling_handoff.md` §5
(cmake target `EarthworksFXSample`, run from `C:\dev\git\os\gameroot_dev`).
Interactive repros must come from the developer — ask, don't drive the GUI.

## 5. Established facts and constraints

- The visible hole bug (rectangular missing terrain, healed by clicking) is
  fully explained and fixed: starved readback -> stale inherited sphere
  heights -> `calculateSurfaceFlags` frustum test culled on-screen tiles.
  Verified: pre-fix interactive repro showed sustained y_mismatch up to
  37/frame; post-fix repro with identical churn: 0 over 7517 frames.
- Identical visible behaviour on D3D12 and Vulkan (developer observation).
  The starvation ratio itself has only been measured on Vulkan — measure
  D3D12 (`-m d3d12`; NEVER with `--validation 2`, machine-freezer) to learn
  whether it is backend-specific.
- Testflights differ from interactive in: camera driven programmatically,
  per-frame `ProcessCapture()`/PostPresent work, possibly uncapped fps
  (interactive ran 60fps vsync). One of these keeps the fence progressing —
  identifying which is half the diagnosis.
- Mapping the live buffer directly instead of the ring = full GPU sync per
  frame; that is why the ring exists. Any fix must not reintroduce a stall.
- DiligentCore may be read (e.g. `DeviceContextVkImpl::MapBuffer`,
  `FenceVkImpl::GetCompletedValue`, EnqueueSignal/flush semantics,
  `FinishFrame()` requirements for staging reads) but never modified. If the
  root cause is a Diligent usage contract (e.g. staging reads require
  FinishFrame-tracked fences that only advance on Present/Flush), fix the
  usage on our side (ewResources/ewGpuContext).

## 6. Hypotheses, ranked

1. `MapBuffer(MAP_READ, DO_NOT_WAIT)` fails although the app fence completed:
   Diligent tracks staging-buffer availability with its own internal frame
   fences, which may lag the app's `EnqueueSignal` fence (they advance on
   Flush/FinishFrame/Present cadence). Discriminating counter from §3 settles
   this immediately.
2. `GetCompletedValue()` not advancing: EnqueueSignal's flush behaviour in the
   interactive frame loop differs (batching, vsync-blocked present, queue
   submission cadence).
3. Something in the testflight path (ScreenCapture fence queries, PostPresent)
   pumps fence progress that the interactive path lacks — if so, the fix is
   to pump it explicitly once per frame.

## 7. Unrelated loose end (watch, don't chase)

Two flaky segfaults during testflight startup (load storm) on 2026-08-31,
exit codes 139 and 127, 2 of ~8 runs, never reproduced since, not attributed
(appeared in builds carrying only instrumentation + a preprocessor-neutral
shader cleanup). If one recurs, capture the run.log tail and conditions.
