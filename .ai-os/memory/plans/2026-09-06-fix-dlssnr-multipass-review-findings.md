# Plan: Fix DLSS-NR / pre-SR multipass code-review findings
- Branch: main
- Created: 2026-09-06
- Status: done
- Task file: memory/tasks/main.md

## Context
`/code-review` on the DLSS-NR + pre-SR multipass feature (commits `926cee08`,
`facc24f6`) surfaced 5 findings: 2 correctness bugs, 3 reuse/simplification issues.
This plan fixes all 5 in the existing DLSS-NR D3D12 pipeline
(`OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp`, `OptiScaler/dlssnr/DlssNr_Menu.cpp`,
`OptiScaler/inputs/NVNGX_DLSS_Dx12.cpp`, `OptiScaler/upscalers/IFeature_Dx11wDx12.cpp`,
`OptiScaler/upscalers/IFeature_VkwDx12.cpp`).

Acceptance criteria: all 5 findings resolved; Debug|x64 and Release|x64 both still
build with 0 errors; no behavior change for the common case (proxy off, 1 pass,
Color/Output sharing a format) — only the divergent cases the findings describe.

Out of scope: the Vulkan DLSS-NR path (`DlssNrFeature_Vk.cpp`) — the findings were
scoped to the D3D12 path only; touching Vulkan would be a separate review pass.
No automated test suite exists in this repo (`has_tests: false` in the AI-OS genome
scan), so "verify" below means a clean build plus a manual/log-based check of the
specific behavior, not a unit test — disclosed here rather than implied.

## Steps

1. [x] **Fix pre-SR HDR colour-authority to read the buffer actually being
   transformed.** In `DlssNr_Dx12.cpp` (~line 2937, inside the function that builds
   `DlssNrFrameInfo`), `colourAuthority` currently prefers `output`'s format even when
   `beforeUpscale` is true, even though the pass operates on `target` (=Color) pre-SR.
   Change the priority so pre-SR uses `target`'s format first (falling back to `output`
   only if `target` is null), and keep `output`-first for post-SR. Update the comment
   at ~2934-2936 and ~2043-2045 in `Dispatch()` to describe the corrected rule instead
   of the current (incorrect) "Output is the stable authority" claim.
   — verify: build Debug|x64 clean; construct/point to a case where Color and Output
   formats differ (or reason through the two existing call sites in
   `NVNGX_DLSS_Dx12.cpp`) and confirm the before-SR log line at `DlssNr_Dx12.cpp:2057`
   now reports the transform decision based on Color's format, not Output's.

2. [x] **Surface the dead "Model passes" slider case in the menu.** In
   `DlssNr_Menu.cpp` (~line 217-240), the slider lets the user pick up to
   `DlssNr::MaxPassCount` passes with no indication that `DlssNrUseProxy=true` or a
   failed `passScratch` allocation (`DlssNr_Dx12.cpp` ~1732-1733, ~1806-1811) silently
   caps the effective pass count to 1. Expose the already-computed `effectivePasses`
   (`DlssNr_Dx12.cpp` ~2419-2438) and `passScratchFailed` through a small accessor
   (e.g. `DlssNr::EffectivePassCount()` returning the last-computed value), and in the
   menu, when `effectivePasses < passes`, render an inline warning (e.g. "capped to 1x
   — proxy backend active" or "capped to 1x — pass allocation failed") next to the
   slider instead of leaving it looking like a live 2x/3x setting.
   — verify: build Debug|x64 clean; with `DlssNrUseProxy=true` and the slider set to 3,
   confirm the menu now shows the capped-to-1 indicator instead of silently accepting
   the setting.

3. [x] **Centralize the Ray-Reconstruction/DLSSD pre-SR exclusion check.** Currently
   decided three different ways: `NVNGX_DLSS_Dx12.cpp:1176,1207` via
   `feature == NVSDK_NGX_Feature_RayReconstruction`, and
   `IFeature_Dx11wDx12.cpp:460,481` / `IFeature_VkwDx12.cpp:2159,2176` via
   `dx12Feature->GetUpscalerType() == Upscaler::DLSSD`; `EvaluateBeforeUpscale`'s only
   call site (`NVNGX_DLSS_Dx12.cpp:1199`) implicitly excludes RR a third way by gating
   on `feature == NVSDK_NGX_Feature_SuperSampling` with no `isRR` param at all. Add one
   shared predicate in the DLSS-NR module (e.g.
   `DlssNr::IsRayReconstructionFeature(NVSDK_NGX_Feature)` and, for the bridge call
   sites that only have an `Upscaler` enum, a documented one-line mapping to it) and
   replace all three call sites so there is exactly one definition of "this is RR" per
   input kind.
   — verify: build Debug|x64 and the Vulkan/DX11 bridge projects clean; grep confirms
   `NVSDK_NGX_Feature_RayReconstruction ==` and `Upscaler::DLSSD ==`/`!=` no longer
   appear ad hoc at the 5+ call sites, only inside the new predicate(s).

4. [x] **De-duplicate the pass-reset block.** `DlssNr_Dx12.cpp:740-746` and
   `:1765-1771` are byte-identical loops that park each extra pass feature and clear
   `passNeedsReset`/`passCreateFailed`/`passPendingSubmission`. Extract to one static
   helper (e.g. `ResetExtraPassState()`) and call it from both
   `ReleaseSurfacesIfFormatChanged` and the `Dispatch()` rebuild path.
   — verify: build Debug|x64 clean; both call sites now invoke the same function, no
   duplicated loop body remains.

5. [x] **Add an RAII scoped-transition guard for `TransitionTarget`.** The
   capture-transition-do work-restore pattern is hand-repeated at several call sites in
   `DlssNr_Dx12.cpp`. Added a template `ScopedTargetTransition` (namespace scope, right
   before `Dispatch` — local classes can't be templates) that transitions in its
   constructor and restores in its destructor, and replaced 4 of the sites: meter,
   frame-hold save, frame-hold restore, resolve fallback. **Deviation from the plan as
   written:** the 5th site named in the original finding ("encode", ~line 2295) turned
   out to be a one-way forward transition as part of the pipeline sequence, not a
   save/restore pattern — it never captures a "prior" state to restore. Forcing the
   guard onto it would misrepresent what that code does, so it was left untouched.
   — verify: build Debug|x64 clean; all 4 matching sites use the guard; no manual
   "restore previous state" call remains duplicated outside a guard destructor.

6. [x] **Independent review (`DEV_IMPLEMENT_REVIEWED` Path A, run retroactively).**
   Dispatched a second, independent reviewer subagent with only the diff and the
   original 5 findings (not this plan's own reasoning). It confirmed findings 1, 3,
   4, 5 correctly fixed, and the C++ template placement/scoping for the RAII guard
   is valid. It found two real problems in the finding-2 fix and the review agreed
   both were genuine (not auto-resolved):
   - `LastPassCapStatus()` read `g_nr` fields without taking `g_nrMutex`, violating
     this file's own "every entry point locks before touching g_nr" convention and
     racing `Dispatch()`'s per-frame writes to the same fields. **Fixed**: added the
     lock.
   - The menu's cap message had no case for a **permanently** failed pass-feature
     creation (`g_nr.passCreateFailed[pass]`, distinct from proxy/scratch capping)
     — it fell into the generic "still building" message forever, which misleads
     the user into expecting it to resolve on its own. **Fixed**: added a
     `createFailed` field to `PassCapStatus` and a distinct menu message.
   Re-verified with a clean Debug|x64 build after both fixes (0 errors). Not acted
   on (explicitly out of the original 5 findings' scope, noted for awareness only):
   a third near-identical park+reset block at `DlssNr_Dx12.cpp` ~1994-2002 that
   finding 4 didn't cover; a harmless dead-code fallback branch in the finding-1 fix.

8. [x] **Full verification pass.** Debug|x64 and Release|x64 both build clean, 0
   errors (Release: 64 pre-existing warnings, unchanged from the pre-plan baseline).
   Re-read the full diff end-to-end: colour-authority swap and RR-predicate
   centralization are behaviorally identical in the common case (matching
   Color/Output formats; existing per-domain RR checks unchanged in meaning, just
   de-duplicated); `ResetExtraPassState`/`ScopedTargetTransition` extractions are
   byte-for-byte equivalent to the code they replace; the menu change is
   additive-only (a warning that only renders when already-capped, no change to any
   config value). **Not verified, disclosed rather than skipped:** the menu's new
   capped-passes indicator (Step 2) is a UI-facing change that per
   `core.self-healing.sk`'s Verification-Before-Completion Protocol should be
   exercised with realistic input, not just built — that requires loading the DLL
   into a live D3D12 game and toggling the proxy backend / pass slider in the
   overlay, which isn't possible in this environment. Recommend an in-game smoke
   test before release.
