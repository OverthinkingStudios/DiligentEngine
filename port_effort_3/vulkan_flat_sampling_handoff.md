# RESOLVED: Vulkan terrafector/road rendering — fixed; final fix is 2 lines

Updated 2026-08-30 (end of second session, post-revert verification). Status:
**FIXED AND VERIFIED on a cleaned tree** — `TF_DEBUG_FORCE_OUTPUT 0`, all
experiment residue reverted, Vulkan vs D3D12 on both testflights:
`albedo_testing_steg_closeup` road-deck ROI medians (80,71,58) identical on
both APIs, 0.19% px >10 (noise floor); `albedo_testing_steg` 0.017% px >10.

## THE FIX (complete — everything else in this doc is context/history)

**Swapped-argument clamp = undefined behaviour, 2 lines:**
- `EarthworksFX/hlsl/terrain/materials.hlsli`, `solveElevationColour`, inside
  `if (MAT.useColour)`:
  `clamp(0.04, 0.9, A * B * 4 * MAT.albedoScale)`
  → `clamp(A * B * 4 * MAT.albedoScale, 0.04, 0.9)`
- `EarthworksFX/hlsl/terrain/render_spline.hlsl`, `solveColor`: same change
  with `_mat.albedoScale`.

Why: HLSL `clamp(x, min, max)` — the original passed the bounds first, giving
contradictory bounds (min 0.9 > max value), which is UB. DXIL's lowering
returned the value (looked correct on D3D12 forever); SPIR-V FClamp on NVIDIA
returns the 0.9 bound → every terrafector/road albedo pegged at flat cream on
Vulkan. Latent original bug, detonated by the API switch. Proven by bisection:
raw base sample, raw detail sample and the blend parameters were each
pixel-identical across APIs (probe modes 10/11/12); only this line diverged.

**Same-class UB in `EarthworksFX/hlsl/PBR.hlsli` (cube_mip): FIXED 2026-08-31**
(dev approved after analysis: DXIL had lowered it to `min(6, v)` — differs from
the intended clamp only for v < 0, where hardware clamps LOD anyway, so D3D12
look unchanged; on Vulkan/NV it returned the bound 6, env cube stuck at
blurriest mip).

**NO sampler rename is needed.** An earlier conclusion in this hunt (a
"name-cursed gSmpLinear sampler") was tested again at the end of the session
and DID NOT reproduce — see §2. The rename workaround was removed from the
tree and everything still matches D3D12.

## 1. What actually happened (two layers, one red herring)

- The visible symptom (flat washed-out roads/terrafectors on Vulkan) was the
  clamp UB above.
- Underneath it, a REAL but TRANSIENT sampler failure existed during the hunt:
  for ~7 consecutive runs, sampling through `gSmpLinear` in the two
  terrafector bake PSOs behaved like an unwritten descriptor (LOD stuck at
  smallest mip), pixel-verified with the mode-8/9 probes, while identical
  sampler objects on other slots worked. Renaming the variable appeared to fix
  it and the fix "held" for the rest of the session.
- END-OF-SESSION REVERT TEST: removing the rename entirely (original names,
  original registers, original declaration order) — the failure did NOT come
  back. Same shader configuration that failed all morning now passes the
  verdict map and full V==D verification.
- A read-only audit of DiligentCore (see §3) independently found **no
  name-keyed mechanism** anywhere between `Set("gSmpLinear")` and the GPU, and
  proved the SPIR-V is byte-identical modulo the name string.
- Conclusion: the sampler failure was **state-dependent** (a stale/corrupt
  cached pipeline or descriptor-pool state in the NV driver or Diligent, not a
  deterministic function of the source). The rename "worked" by perturbing
  bytes and flushing that state — a coincidence, not a mechanism. Note the
  perturbation dependency was odd: register moves and declaration swaps (which
  also change SPIR-V bytes) did NOT flush it, only the rename build did — so
  "driver pipeline cache keyed on SPIR-V" does not fully explain it either.
  Root cause remains UNPINNED; it is currently not reproducible.

## 2. If the flat-sampling gremlin ever returns — decision tree (do NOT chase names)

1. Confirm with the mode-9 verdict map (magenta-family = one sampler slot dead;
   see the switch docs in materials.hlsli, analysis via
   `port_effort_3/scripts/capture_stats.py --roi`).
2. Build DiligentCore with `DILIGENT_DEVELOPMENT=ON` (RelWithDebInfo ok) and
   rerun: `DvpValidateCommittedResource` logs "No resource is bound to
   variable ..." per draw — decides "descriptor never written" vs "written but
   consumed wrong".
3. Add a temporary log in DiligentCore
   `DescriptorPoolManager.cpp` `DynamicDescriptorSetAllocator::Allocate` for
   null returns (see §3 defect 1 — currently SILENT in release). One line per
   draw = descriptor-pool exhaustion is live.
4. RenderDoc one terrafector draw: descriptor set 0, the sampler binding
   (VK_DESCRIPTOR_TYPE_SAMPLER) — valid trilinear sampler with large maxLod?
   → fault is shader-side; empty/garbage → write/allocation path.
5. Driver-state hypothesis: delete the NVIDIA shader caches
   (%LOCALAPPDATA%\NVIDIA\DXCache, GLCache) and retest before deep-diving.

## 3. DiligentCore findings (read-only audit, 2026-08-30) + proposed upstream PR

The audit that cleared the name theory also verified: cross-stage merge keys
compare full strings (no hash-collision aliasing), string pools are
symmetric, combined-sampler suffix logic is dead when
`UseCombinedTextureSamplers=false`, `FindResource`/`GetVariable` walk the same
resource list, and `CommitDynamicResources`' 4096-element array batching is
correct. Signature layout for the terrafector PSOs (one DYNAMIC set): bindings
0..5, with `gmyTextures_T[4096]` = binding 5 occupying 4096 descriptors.

**Two REAL upstream defects found (PR-worthy regardless of our gremlin —
but we CANNOT claim they caused it; on NV they are likely latent):**

1. **Silent VK_NULL_HANDLE dynamic descriptor set on pool exhaustion.**
   Chain: `LogicalDevice.cpp:425-427` returns null with no log →
   `DescriptorPoolManager.cpp:290-306` (`DynamicDescriptorSetAllocator::
   Allocate`) passes it on unchecked (the non-dynamic allocator at :231 at
   least has a DEV_CHECK_ERR) → `DeviceContextVkImpl.cpp:663-666` feeds it to
   `CommitDynamicResources`, whose only guard is a debug-only VERIFY
   (`PipelineResourceSignatureVkImpl.cpp:784`) → release builds call
   `vkUpdateDescriptorSets` with a null dstSet. Fix: LOG_ERROR_MESSAGE on the
   failed allocation (naming layout + pool sizes) and a null check in
   `TransitionAndCommitShaderResources` that logs-and-skips. Pure error-path
   hardening; no behaviour change on success — that is the correctness
   argument.
2. **Dynamic descriptor pool sizing ignores actual set-layout requirements.**
   Default `DynamicDescriptorPoolSize` (`GraphicsTypes.h:4091`) allows 2048
   sampled images per pool; a single set with a 4096-element runtime array
   can NEVER be satisfied per spec — a fresh pool has the same sizes, so the
   retry loop cannot succeed; only driver over-allocation (NV) saves it.
   Fix: when allocation from a fresh pool fails, create a pool sized to
   max(configured, layout requirement); plus a doc note on
   `VulkanDescriptorPoolSize`. Correct because the layout's own requirement
   is by definition the lower bound for a pool that must hold one such set.

**PR process** (dev's task; DiligentCore is the unforked submodule at
`DiligentCore/`): fork `DiligentGraphics/DiligentCore`, branch, apply the two
hardenings + a regression test in their API test suite (allocate a set whose
layout exceeds the configured dynamic pool size; expect a logged error, not a
silent null / crash), point our submodule at the fork to soak locally, then
PR upstream referencing: silent-failure chain above, and that a 4096-element
runtime descriptor array (bindless-style) is a realistic workload. Minor
extra: `ShaderResourceHashKey` ctor (`PipelineStateBase.hpp:146`) masks away
`StrOwnershipMask` — harmless today, latent leak if `bMakeCopy=true` is ever
used.

## 4. Final tree state (clean)

In: the 2-line clamp fix; TF_DEBUG probe modes 1-12 (inert at 0, documented in
materials.hlsli); `port_effort_3/scripts/capture_stats.py`; a per-texture
format log line in `ewResources.cpp` createFromFile (useful: revealed the
terrafector albedos are **BC6H_UF16** — HDR float, no sRGB variant, the
`isSRGB=true` request is silently unsatisfiable for them); session-1 items
(ewShader sampler logging, [[vk::image_format]] fixes, TF_material hardening
+ static_asserts, TF_TEX clamp, Diligent→spdlog routing).
Out (reverted): the gSmpTF rename, register s7, declaration-order swap, the
extra gSmpPoint binds. Verified AFTER the reverts (see header numbers).
Git: dev commits; local commits on `port_effort_3` only; never push.

## 5. Loop recipe + subagent rules (unchanged, keep for future hunts)

```bash
# (TF_DEBUG_FORCE_OUTPUT bisection switch was removed 2026-08-31 after the fix
#  landed; re-add solid-colour early-outs in the psMains if a hunt needs them)
# syntax check:
cd /c/dev/git/os/DiligentEngine/EarthworksFX/hlsl/terrain
/c/VulkanSDK/1.4.350.0/Bin/dxc.exe -spirv -T ps_6_5 -E psMain -I .. -I . render_splineTerrafector.hlsl -Fo /dev/null
# deploy - cwd MUST be repo root; verify with grep afterwards:
cd /c/dev/git/os/DiligentEngine
cmake --build build/Win64 --config RelWithDebInfo --target EarthworksFX_DeployHLSL   # C++: EarthworksFXSample
grep -c "<marker>" /c/dev/git/os/gameroot_dev/hlsl/terrain/materials.hlsli
# run (Vulkan default; "-m d3d12" for reference - NEVER with --validation 2,
# freezes the machine; --validation 2 on Vulkan is fine; NO --tf_lossless):
cd /c/dev/git/os/gameroot_dev
./../DiligentEngine/build/Win64/EarthworksFXSample/RelWithDebInfo/EarthworksFXSample.exe \
    --testflight albedo_testing_steg_closeup > /dev/null 2>&1
# analyse numerically (grid.jpg 1280x508 closeup / 640x384 standard):
python port_effort_3/scripts/capture_stats.py RUN/grid.jpg --roi 350,300,1000,500
python port_effort_3/scripts/capture_stats.py V/grid.jpg --compare D/grid.jpg
```

Subagent rules (user-mandated): NO image upscaling/resampling of any kind —
report "need better quality" instead; reuse capture_stats.py, no one-off
analysis scripts; judge colours by ROI medians, not eyes; black grid.jpg =
early capture (check metrics terrainTileDraws first).

## 6. Method lessons (why this hunt looped)

- Two defects stacked (transient sampler state + deterministic clamp UB) make
  every single-cause theory half-fail. Bisect with judgment-free colour-coded
  probes, change ONE variable per roundtrip, diff numerically.
- When every INPUT to an expression is proven identical cross-API and the
  OUTPUT differs, hunt undefined behaviour inside the expression.
- A fix that cannot be un-fixed (revert → bug stays gone) was never the fix.
  Always run the revert test before writing conclusions — the rename survived
  five elimination experiments and still turned out to be a coincidence.
- "Verbatim-original code" can carry UB that only detonates on a new API.
