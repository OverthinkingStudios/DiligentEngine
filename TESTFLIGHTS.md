# Testflights — automated visual + performance runs (agent HOWTO)

A *testflight* replays a fixed set of cameras against the currently loaded terrain,
captures a screenshot per camera, and writes one self-contained run folder with a
contact-sheet JPG plus machine-readable metrics. Use it to compare two builds
(visually and performance-wise) without flying through the world manually.

Works with any EarthworksFX app; `EarthworksFXSample` is the usual host.
Windows-first (the app exits via the Win32 message loop).

## Build & run EarthworksFXSample (Windows)

The CMake build dir is `build\Win64` (VS generator, multi-config). RelWithDebInfo
is the usual config for testflights (fast + usable stack traces). Build from the
repo root, then **run with `%ACSMP_GAMEROOT%` as the working directory** — that
content folder holds assets and other dependencies the app needs (and it's the
VS2022 debugger working dir):

```powershell
cmake --build build\Win64 --config RelWithDebInfo --target EarthworksFXSample
Set-Location $env:ACSMP_GAMEROOT
& "C:\dev\git\os\DiligentEngine\build\Win64\EarthworksFXSample\RelWithDebInfo\EarthworksFXSample.exe" --testflight smoke -m d3d12
Write-Host "exit code: $LASTEXITCODE"
```

Swap `RelWithDebInfo` for `Debug`/`Release` as needed; the exe always lands in
`build\Win64\EarthworksFXSample\<config>\`. A full smoke run takes only a few
seconds after load, so it's cheap to run repeatedly.

## Run one

```
EarthworksFXSample.exe --testflight <name> [--mode d3d12|vk]
```

Options:

| Flag | Meaning |
|---|---|
| `--testflight <name\|path>` | Flight name (resolved to `EarthworksFX/testflights/<name>.json` in the source tree) or explicit json path |
| `--tf_camera <index>` | Fly only this camera (e.g. to re-shoot one suspicious view) |
| `--tf_lossless` | Additionally write one lossless PNG per camera |
| `--tf_settle_ms <n>` | Override the flight's per-camera settle time |
| `--tf_jpg_quality <n>` | Grid JPEG quality (default 90) |

The flight enforces its window size (windowed) and forces vsync off. Input is
ignored while it runs. The **terrain is whatever `lastFile.xml` points to** —
make sure the right terrain is configured before comparing runs.

Exit code: `0` = all cameras captured and settled; otherwise the number of
settle timeouts + capture failures (details in metrics.json).

## Where things live

**Flight definitions are committed** in the repo at `EarthworksFX/testflights/*.json`
(dev builds bake this source path in at compile time; the in-app editor reads and
writes there directly, so authored flights show up in `git status`).

**Run outputs go to user data.** `<UserData>` is the app's data folder under
`Saved Games/Overthinking Studios/<app>-<stage>` — the exact run-folder path is
printed in the log at the end of every run.

```
EarthworksFX/testflights/<flight>.json      committed flight definitions

<UserData>/testflights/
  <flight>/<timestamp>/          one folder per run; latest = greatest timestamp
    grid.jpg                     ALL cameras tiled row-major - read this first
    metrics.json                 per-camera fps/settle/renderer metrics - read this second
    holes.txt                    terrain hole / quadtree churn trace (summary in the first 5 lines)
    NN.png                       only with --tf_lossless (NN = camera index)
    <flight>.json                verbatim copy of the flight that was flown
    run.log                      application log copy
```

Interpreting a run as an agent:

1. Look at `grid.jpg` (each tile carries a burned-in badge: flight, timestamp,
   camera index + name). Tile order is row-major by flight order; the mapping is
   also in `metrics.json` under `grid` and `scenes[]`.
2. Read `metrics.json`: per camera `avgFps`, `settleSec`/`settleReason`
   (`stable` = tile streaming converged, `timeout` = it did not — suspicious),
   and `debug` (draw counts, `tilesUsed`, `gpuTerrainBlocks`, `splitsPerformed`...).
   `gpuTerrainBlocks == 0` on a terrain camera means the terrain draw was empty.
3. To compare two builds: same flight, same terrain, one run each; diff the two
   `grid.jpg` by eye and the two `metrics.json` numerically. Minor streaming
   nondeterminism is expected; large `tilesUsed`/fps/draw-count deltas are not.
4. Need a closer look at camera N? Re-run with `--tf_camera N --tf_lossless`.

## Authoring flights

In any EarthworksFX app: ImGui window **Testflights** (part of the common UI) —
select or create a flight, fly somewhere, "add current" to append the live camera
(position/yaw/pitch/FOV/near/far), reorder with up/dn, preview with "view",
then "save". Files are plain json under `EarthworksFX/testflights/` and can be
edited by hand; per-camera `toggles` accept `ew::DebugToggles` field names
(e.g. `"terrainConstColor": true` for a diagnostic shot).

Flight-level fields: `terrain` (stamped from the loaded terrain on every in-app
save; informational — the app still loads via `lastFile.xml`, so verify it matches
before comparing runs), `window` [w,h] (enforced), `settleMs` (minimum hold per
camera), `settleTimeoutMs` (hard cap), optional `toggles` baseline.

Implementation: `EarthworksFX/src/core/TestFlightData.h` (data/json, engine-agnostic),
`EarthworksFX/src/app/TestFlightController.cpp` (runtime), wired in
`EarthworksFX/src/app/EarthworksFXApplicationBase.cpp`.
