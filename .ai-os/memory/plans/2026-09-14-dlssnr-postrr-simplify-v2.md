# Plan: force post-RR NR placement, retire ResidualAcrossRR, add RR Auto model resolution

- Off `main` `30093a4e`, branch `feat/dlssnr-postrr-simplify-v2`
- Created: 2026-09-14
- Status: done — both configs build clean, Review Pass complete, not yet committed/pushed
- Task file: `.ai-os/memory/tasks/main.md`
- Source of intent: `feat/dlssnr-postrr-simplify` (`b656d2d9`), branched off the much older
  `main` `e237f895` — 8 commits + PR #7 stale relative to current `main`. Not mergeable as-is
  (see below); this plan re-implements the same intent against current code.

## Context

User asked to "apply `feat/dlssnr-postrr-simplify` to main". That branch's own commit message
(`b656d2d9`) gives the rationale, confirmed independently against the code actually shipping on
`main` today:

> Ray Reconstruction already denoises the frame before either NR seam runs, so placing NR
> before it only exposes the edit to RR's own denoise pass... Running NR after RR sidesteps
> the problem entirely for free; in-game A/B testing confirmed post-SR looks more detailed
> than pre-SR for the same scene, with none of the grain/artifact risk traced through Carry's
> private-DLSS-SR jitter handling, reset-latch behavior, and blend-rate tuning.

**Why this isn't a merge/cherry-pick.** `b656d2d9` deletes ResidualAcrossRR (`DlssNrCarry`) as
it existed at `e237f895` — a small additive-v1 implementation (~239 lines, modes 5/6 reuse, no
private DLSS SR feature). Current `main` shipped a much larger v2/v3 rewrite via PR #7
(`f29d7abb` "adopt upstream's ResidualAcrossRR rewrite" onward): a private DLSS SR sub-feature
with its own creation epoch, jitter-offset forwarding, reset-latch, MV-reprojected temporal
accumulator (`DlssNrResidualAcrossRrBlend`, default 0.08), and its own carrier
encode/upscale/apply pipeline. Confirmed by grep: 134 lines across
`OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp` alone reference this machinery today, against ~7
files touched by `b656d2d9`. A cherry-pick would conflict on nearly every touched line and,
worse, could leave dangling pieces the old commit never knew about.

**Confirmed DX12-only.** `grep -c ResidualAcrossRr OptiScaler/dlssnr/DlssNrFeature_Vk.cpp` = 0.
The Vulkan-parity step in the original build plan (`.ai-os/memory/plans/2026-09-10-dlssnr-presr-residual-across-rr.md`,
step 8) was deferred and never done. Nothing to remove on the Vulkan side.

**User confirmed via AskUserQuestion (2026-09-14): re-implement against current `main`**, not
a raw merge, and not a pause to reconsider retiring the feature — go ahead.

### What "force post-RR unconditionally" changes today

`configuredBefore = cfg.DlssNrRunBeforeSr.value_or_default() && preSrCompatible;`
(`DlssNr_Dx12.cpp:3755`) has no RR gate today — a user can already reach pre-SR-with-RR-and-no-Carry
(the "washed-out" behaviour the original ResidualAcrossRR plan's context section describes as the
problem it was solving). This plan adds `&& !rayReconstruction` there, so RR active forces
`configuredBefore = false` regardless of the `RunBeforeSR` checkbox — matching `b656d2d9`'s intent
exactly. `RunBeforeSR` continues to work normally when RR is off.

### Acceptance criteria
- [x] RR active (any placement setting) → NR always runs post-RR+SR. `RunBeforeSR` has no effect
      while RR is active; still works normally when RR is off.
- [x] `DlssNrResidualAcrossRr`, `DlssNrResidualAcrossRrBlend` config keys removed from
      `Config.h`/`Config.cpp`/`OptiScaler.ini` (not just defaulted off — actually gone, per
      `b656d2d9`'s "delete rather than leave behind a dead flag").
- [x] All ResidualAcrossRR runtime code removed from `DlssNr_Dx12.cpp`: the `residualAcrossRr`
      predicate/branch in `EvaluateInternal`, `ApplyResidualAcrossRr`, `UpscaleResidualCarrier`,
      every `g_nr.residual*` field tied only to this feature (`residualCarrierIn/Out`,
      `residualSrExposure/Params/Feature/CreateEpoch/Failed/Reset`, `residualStore/StoreHi/Composed/Edited`,
      `residualPair`, `residualHistoryPrimed`, `residualWhitePoint`, `residualExposurePreMul`,
      `residualUp`), their entries in the park/release/shutdown lists, and the
      `DlssNrResidualMode_EncodeCarrier`/related HLSL mode dispatch **only if** confirmed unused by
      any other surviving path (DeferredSr/ResidualFG's modes 5/6/10 are a separate, surviving
      mechanism per the 2026-09-10 plan's own recon — verify this distinction again before deleting
      any HLSL mode, since main's mode numbering may have shifted since then).
- [x] `DlssNr_Menu.cpp`: "Carry the pre-SR edit across RR" checkbox + "Detail accumulation rate"
      slider removed.
- [x] `tests/nr_residual_rr_smoke.cpp` and `docs/RESIDUAL-ACROSS-RR.md` removed.
      `OptiScaler/dlssnr/design/pre-sr-multipass.md` updated to drop the Across-RR subsection if
      present (verify — the 2026-09-10 plan's step 11 for this was left unchecked, so it may never
      have been written).
- [x] RR-only "Auto" model resolution: derives `DlssNrWorkingScale`'s effective value from the
      render:output ratio DLSS itself reports (`frame.RenderSubrectWidth`/`Width` or equivalent),
      scoped to RR only per `b656d2d9`'s description; live applied percentage exposed for the menu
      slider to display instead of a stale manual value while Auto is active.
- [x] No other NR placement (plain post-SR, pre-SR non-RR multipass, DeferredDLSS, ResidualFG),
      white-point handling, or Highlight guard behaviour changes.
- [x] Debug|x64 + Release|x64 build clean, 0 new errors, dlssnr-TU warning count at or below
      current baseline. No unrelated file touched.
- [x] Independent Review Pass (`core.dev-loop.sk`) run on the full diff before calling this done.

### Out of scope
- Vulkan parity (nothing to remove; RR-forces-post-RR gate is DX12-only in scope here since
  ResidualAcrossRR itself never reached Vulkan — if the Vulkan `configuredBefore`-equivalent
  needs the same `!rayReconstruction` gate for consistency, that is a separate, explicitly
  called-out decision, not assumed here).
- Any change to white-point sources, Highlight guard, or HDR colour-space handling.
- Re-litigating whether ResidualAcrossRR was worth building in the first place — this plan
  executes the user's explicit decision to retire it, not re-evaluate it.

## Steps

1. [ ] **Full audit before any deletion.** Enumerate every `residual*` symbol in
   `DlssNr_Dx12.cpp`, `DlssNr_Common.h`, `DlssNr_Dx12.h`, `DlssNr_ResidualPair.h`, `Config.h/cpp`,
   `DlssNr_Menu.cpp`, `precompile/dlssnr.hlsl` (mode numbers), `tests/`, `docs/`, `OptiScaler.ini`
   — separate "ResidualAcrossRR-only, safe to delete" from "shared with DeferredSr/ResidualFG,
   must survive" (modes 5/6/10, `DlssNrResidualMode_EncodeCarrier` if distinct). Write findings
   inline in this plan before touching any file.
2. [ ] **Branch.** Create `feat/dlssnr-postrr-simplify-v2` off `main` `30093a4e`.
3. [ ] **Force post-RR gate.** `configuredBefore` gains `&& !rayReconstruction`
   (`DlssNr_Dx12.cpp:3755`). Verify: with RR on, `RunBeforeSR` checkbox has no effect (log or
   code-read confirmation — no in-game test available here).
4. [ ] **Delete ResidualAcrossRR runtime code** per the audit's safe-to-delete list: predicate/
   branch, `ApplyResidualAcrossRr`, `UpscaleResidualCarrier`, `NrState` fields, resource
   lifetime/park/release/shutdown list entries.
5. [ ] **Delete config surface.** `Config.h`/`Config.cpp` keys + `OptiScaler.ini` entries.
6. [ ] **Delete menu surface.** Checkbox + slider + their HelpMarkers in `DlssNr_Menu.cpp`.
7. [ ] **Delete tests/docs.** `tests/nr_residual_rr_smoke.cpp`, `docs/RESIDUAL-ACROSS-RR.md`,
   any now-stale subsection in `pre-sr-multipass.md` or `NR-REQUIREMENTS-MATRIX.md`.
8. [ ] **RR-only Auto model resolution.** Investigate current `DlssNrWorkingScale` menu code and
   what `frame.*` fields expose render/output resolution at the RR seam; add the Auto mode +
   live-percentage readout per `b656d2d9`'s description. Scope precisely once investigated —
   this is new code, not a port, since the surrounding code has changed since `b656d2d9`.
9. [ ] **Build.** Debug|x64 then Release|x64. Confirm 0 errors, warning delta vs baseline, no
   unrelated file touched, no shader blob changed (this plan makes no HLSL edits unless step 1's
   audit finds a mode genuinely orphaned by the deletion — flag before removing any `.hlsl`/
   precompiled blob).
10. [ ] **Independent Review Pass** (`core.dev-loop.sk`), findings surfaced non-blocking.

## Progress log
- 2026-09-14: plan drafted after user confirmed re-implementation approach via AskUserQuestion.
  Awaiting approval to start step 1.
- 2026-09-14: user said "ai-os execute plan". Full audit done: mapped every `residual*` symbol
  (~200 touchpoints across `DlssNr_Dx12.cpp`). Key finding not anticipated in the draft:
  `DispatchResidualPass`/`dlssnr_residual.hlsl`/`_residualPipelineState` are 100% exclusive to
  ResidualAcrossRR (verified via a `finishedColor` bool parameter distinguishing them from
  Finished Picture's separate `_finishedColorPipelineState`/`dlssnr_finished_color.hlsl`) — safe
  to delete the whole shader + 4 precompiled blobs, not just C++ glue. Mirrored `b656d2d9`'s own
  choice: kept `DispatchResidualPass` but dropped the now-always-true `finishedColor` parameter.
  Confirmed `ResidualBlend`/`ResidualHistoryValid`/`ResidualMotionBaseX/Y` sit at the very end of
  `alignas(256) DlssNrConstants` (no fields after them) — removing them cannot shift any other
  field's offset for the surviving shaders, so removed cleanly rather than left as dead padding.
  Branch created; all deletions + the `configuredBefore &= !rayReconstruction` gate + the new
  `DlssNrModelResolutionAuto` RR-only feature (`CurrentModelResolutionPercent()` accessor,
  width+height-averaged render:output ratio, menu checkbox) applied. Debug|x64 and Release|x64
  both built clean, 0 errors, 0 new warnings, on the first attempt.
- 2026-09-14: Independent Review Pass (code-review skill, high effort) on the staged diff (19
  files, +108/-3846). Verdict: removal is clean, no leftover references anywhere in the tree, no
  stale build-file entries, all `DispatchResidualPass` call sites match the simplified signature.
  3 findings: (1) the Auto ratio used width only, not height — fixed to average both, matching
  the manual slider's own single-uniform-scale design rather than introducing asymmetric X/Y
  scaling; (2) `preSrCompatible` was still computed every frame (GetResource/GetDesc + log
  formatting) even when RR forces it to be discarded — fixed by skipping that block when
  `rayReconstruction` is true; (3) `CurrentModelResolutionPercent()` reads `g_nr.appliedWorkScale`
  without `g_nrMutex` — left as-is, matches the existing accepted lock-free pattern
  `GameExposureStatus()`/`LastGpuTime()` already use for menu-display-only reads. Both fixes
  rebuilt clean (Debug + Release x64, incremental, 0 errors, 0 new warnings).
  Not committed/pushed -- awaiting the user's direction on next step.
- 2026-09-14 (follow-up): user pointed out Auto model resolution should also apply when NR runs
  post-SR without RR, not just with RR active -- the render:output ratio rationale (the upscaler's
  output already reconstructed detail at that ratio, so NR working at the same scale on it is free)
  holds for any upscaler, not RR specifically. Generalized the gate in `DlssNr_Dx12.cpp` from
  `frame.RayReconstruction &&` to `!frame.BeforeUpscale &&` -- `frame.BeforeUpscale` is already the
  fully-resolved runtime placement (accounts for RR's forced-post override and the
  `preSrCompatible` fallback), so this is exact, not an approximation. Updated the doc comments in
  `Config.h`, `OptiScaler.ini`, and `DlssNrFeature_Dx12.h` to match. In `DlssNr_Menu.cpp`, the
  checkbox label became "Auto (post-SR only)" and `autoActive` became `resolutionAuto && (!beforeSr
  || rayReconstruction)` -- an approximation for menu display only (it can't see
  `preSrCompatible`), while the live percentage shown always comes from the actual runtime
  `g_nr.appliedWorkScale` so the displayed number is correct even in that edge case. Debug+Release
  x64 rebuilt clean, 0 errors, 0 new warnings. Still not committed/pushed.
