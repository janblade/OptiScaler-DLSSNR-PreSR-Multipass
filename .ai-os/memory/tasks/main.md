# Task Memory — main (rolling)

Protected-branch working notes. Drained by `MEMORY_CONSOLIDATE`, not `TASK_CLOSE`.

## State @ 2026-09-08 (post wilsjo2 sync)

`main` HEAD = `32563ad2` (Merge wilsjo2/main), pushed to origin. Lineage: `c3897a78` (PR #1) ->
`91603612` (AI-OS v2.8.0 + bookkeeping) -> `32563ad2` (wilsjo2 merge).

**wilsjo2/main sync — DONE.** merge (not rebase — main has merge commits + is shared). 46 files, 2
incoming commits (`79d9b11c` deferred-DLSS pre-SR NR; `3d083723` Vulkan pre-SR parity + optional NR
experiments + reviewed compat fixes). Brought in: `DlssNr_DeferredSr.inl`, `DlssNr_AsyncLatest.inl`,
`PassProfiles.h`, `ResidualFg.h`, `MfgUnlock.{cpp,h}`, 5 `docs/*.md`, 4 `tests/nr_*_smoke.cpp`. All
NR experiments **default-off**. wilsjo2 carries no `.ai-os/` / `CLAUDE.md` / `AGENTS.md` / `.claude/`
— merge kept ours.
- 1 conflict: `IFeature_Dx11.cpp` — both forks rewrote the D3D11 `Evaluate` state backup/restore from
  the same base. Convergent: kept our `ComPtr`/`pipelineFailed` structure + took wilsjo2's batched
  `CSSet*` + feature-level-aware `uavCount` (correct on FL11.0).
- 1 post-merge compile fix: 6 `g_nrMutex` lock sites the auto-merge kept from wilsjo2 used
  `std::lock_guard<std::mutex>` while our side had `g_nrMutex` as `std::recursive_mutex` — converted.
- Debug|x64 + Release|x64 both build clean, 0 errors. New NR experiment paths are auto-merge +
  compile-verified only — validate in-game before relying on them.

**jitter-cancel prototype** (`memory/plans/2026-09-07-dlssnr-presr-jitter-cancel-mv.md`): **PR
janblade#2 (experiment -> main) open**, `experiment/dlssnr-presr-jitter-mv` **rebased onto the
post-wilsjo2 `main` 2026-09-08** (was `639a58ec`+`1035a2d6` → now `d4fbe294`+`351ac35b`,
force-pushed). Rebase conflicts resolved: shader mode renumbered `DlssNrMode_JitterCancelMv` 5 -> 12
(wilsjo2 claimed 5-11 for its residual/motion modes); `dlssnr.hlsl` `gMode == 5` -> `gMode == 12`;
4 precompiled shader artifacts regenerated from the merged `dlssnr.hlsl` with the bundled `dxc`
(CSO 16848b — smaller than main's 29144b only because bundled dxc != wilsjo2's toolchain, same
source + our extra mode); `DlssNr_Menu.cpp` keeps both wilsjo2's deferred/async/residual block and
our jitter block; `decisions.jsonl` / `tasks/main.md` bookkeeping conflicts merged. Both x64 configs
build clean, 0 errors (63/64 total warnings = the wilsjo2-tree full-build baseline, none in the
resolution files). First in-game pass "barely noticeable"; landing default-off, step-9 sweep
continues. PR #2 is now a clean fast-forward descendant of `main`.

**AI-OS framework v2.8.0** committed `91603612`. `docs/dlssnr-binary-dump/` still untracked (1.5MB).

Prior state: **PR #1 MERGED into `main` (merge commit `c3897a78`)** — `main` now contains 2f9d2746 +
122eaca3. Merged branch `fix/overlay-input-stack-agnostic` still exists local + origin (safe to
delete). Two plans, both Status: done:
- memory/plans/2026-09-07-overlay-input-stack-agnostic.md — the input-stack-agnostic overlay fix (commit 2f9d2746).
- memory/plans/2026-09-07-overlay-input-review-fixes.md — remediation of the 9 independent Review Pass findings (commit 122eaca3). "Fix all": #8 hardened via a SetDataFormat vtable[11] hook, #3/#4 ownership-fit refactors included; re-review verdict "merge with nits", 2 nits folded in, F2 (hook heavier than the LOW finding) retained per user decision.
Both x64 configs build clean, 0 new warnings. In-game sign-off: Assetto Corsa (+CSP) OK, NBA 2K26 (WM_INPUT) OK. **PR into main NOT opened** — user said "Push" only; open on request.

Follow-up, NOT started: AC1 menu-window hold-and-drag still broken (never worked on any build). Separate pre-existing bug — AC1 is `mode:window`; CSP grabs the mouse on held-button for its own camera free-look, so ImGui gets no motion to drag windows. Needs its own task: Debug build + LogLevel=0 log while reproducing a title-bar drag, to confirm WM_MOUSEMOVE stops during the hold / a capture is active. (8 files, +277/-34; .ai-os bookkeeping uncommitted, not pushed). Steps 1-12: BlockGamepad (InputState + DebugState) + StateConsumed, ShouldBlockGamepadInputLocked helper, ReShade conditional block in ApplyMenuVisibilityChangeLocked, reset/snapshot mirror, press-edge FeedImGui, gamepad split (xinput + DI-other), Alt+F4 pass in WM_(SYS)KEYDOWN, TryConsumeRawInputStateLocked dedup + 3 raw feed sites, DirectInput FeedOverlayMouse helper + GetDeviceState/GetDeviceData rework (INFINITE drain preserved on every blocking path, per-device vtable[9]/[10] trampoline), shortcut arm+250ms-cooldown debounce. Debug + Release x64 build clean, 0 new warnings; verified no regression on WM_INPUT games (user). Step 13 (DI relative-motion virtual cursor, commit 3707092c) was **built then reverted at user request** — AC1's log is `mode:window`, so that path never engaged and fixed nothing; recoverable from git history if a genuinely parked-cursor DI title ever needs it. **AC1+CSP menu-mouse is a separate, pre-existing bug** (never worked on any build; CSP input hooking suspected) — out of scope for this plan; needs its own Debug-build-log investigation. Remaining: step 14 = manual matrix on a real DI/raw-only game + NBA 2K26 regression; then push + PR for what it fixes.

## 2026-09-07 — AI-OS framework updated v2.7.0 → v2.8.0

Ran `.ai-os-installer/UPDATE_PROMPT.md` from `D:/DEV/AI OS FRAMEWORK/.ai-os`. Key behavior
change: `PLAN_EXECUTE` now auto-runs `core.dev-loop.sk`'s **Review Pass** (independent
reviewer subagent where supported, else labelled cold self-review) once per flat plan and
once per epic story — non-blocking, findings surface to the user. Review checklist gained a
5th item, **ownership fit**. New `BOOT.md` §4 rule: a plan-less "implement this feature"
request routes to `DEV_IMPLEMENT_REVIEWED`, not a bare in-thread write. Local customizations
(`INFRA_SYNC_UPSTREAM`, empty PROJECT_RULES block) preserved; `RELEASE`/`EVOLVE_BENCHMARK`
not merged. Kernel edits (BOOT.md, kernel/bootstrap.md, manifest.json) done under an
explicit user `KERNEL OVERRIDE AUTHORIZED`. Full detail in `decisions.jsonl`.

## 2026-09-07 — AC + CSP overlay-input fix is stranded, not in main

Commit `ae2a10af` ("Keep the overlay usable over the Assetto Corsa + CSP input stack") is
the only commit on branch `dlss-neural-rendering` and was **never merged to main**. main is
109 commits ahead of the merge-base (`8ac91e81`) and refactored the input-blocking layer
into `ShouldApplyBlockingPolicyLocked()` + `ShouldBlock{Keyboard,Mouse,Cursor}InputLocked()`
helpers, so a cherry-pick conflicts (xinput / directinput / messages). Plan file above is a
re-implementation against the current structure. Symptoms it fixes: overlay mouse clicks
lost, one tap toggles menu twice, car keeps driving with menu open, Alt+F4 can't close.

## 2026-09-07 — DLSS-NR drops camera jitter offset (pre-SR blur cause)

Investigating why pre-SR DLSS-NR looks blurry. Confirmed at code level that the
NR path never forwards `Jitter.Offset.X/Y`:
- Every other upscaler feature reads `NVSDK_NGX_Parameter_Jitter_Offset_X/Y` from
  the incoming NGX param block (`OptiScaler/upscalers/**`, `IFeature.cpp:241`) — the
  game supplies it, the codebase treats it as required.
- `DlssNr_Dx12.cpp` `EvaluateInternal` reads `DLSS_Render_Subrect_Dimensions` and
  `MV_Scale_X/Y` (~L3186-3193) but not jitter.
- Forwarder `dlssnr_call_evaluate` / `dlssnr_vk_evaluate` set ~30 `DLSSNR.*` params,
  none jitter-related. `grep -ri jitter OptiScaler/dlssnr OptiScaler/shaders/dlssnr`
  = 0 hits.
- NR = NGX feature id 18 (DLSS-D/RR); temporal reconstruction needs subpixel jitter
  to align history.

Residual unknown: retail `nvngx_dlssnr.dll`'s full 61-name `DLSSNR.*` vocabulary not
dumpable here (only the forwarder binary is in-repo) → exact model-side slot name
unconfirmed. Likely `DLSSNR.JitterOffsetX/Y` or similar.

Impact: post-SR feeds a resolved/de-jittered image so the gap is near-invisible;
pre-SR feeds raw jittered render-res colour → NR history reprojection misaligned by
the per-frame jitter delta → softening. Matches the design doc's reason for forcing
RR post-SR; plain-SR pre path has the same gap, unguarded.

### 2026-09-07 update — Fix Shape is dead. Retail DLL has no jitter param.

Dumped `nvngx_dlssnr.dll` v310.8.0 (165 MB; copies in game folders, ~/Downloads,
~/AppData/Local/RHI — all identical 61-name vocabulary). **Zero jitter parameters.**
`"jitter"` appears 0× in the whole binary; no `Jitter.Offset`, no halton/phase, no
camera/view/clip matrix params. Full input surface = the 9 resources + subrects,
Enabled/Width/Height, DepthInverted, Reset, MVecScaleX/Y, Hint.Render.Preset,
Intensity, LocalStructureStrength, LocalToneStrength, SkinStructureStrength, Style,
UICorrection, UseAutoMask, ScalingRatio.

**Root cause confirmed (High):** DLSS-NR = feature 18, designed SR-fused; the SR half
resolves jitter, so the NR core has no jitter input and assumes display-res
post-resolve colour. That IS the post-SR placement. Pre-SR hands it jittered
render-res colour with no channel to inform it → softening is intrinsic.

**No fix via a jitter float** — there is no slot. `setFloat("DLSSNR.Jitter…")` would be
a dead write (cf. the documented `DLSSNR.GlobalToneStrength` dead write).

**Only lever left:** the MVec texture is the sole frame-to-frame correspondence
channel. Could bake the per-frame jitter delta into a synthetic MVec passed to NR
(jitter-cancellation compute pass — pattern already exists in repo:
`FFX_FSR2_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION`, XeFG `JITTERED_MV`). Bigger
change (new compute pass over MVs each frame) and speculative — unknown whether the
model does jitter-aware reprojection at all, or is design case (c): accepts the error,
built for post-SR only.

**Decision point for user:** (A) build the jittered-MVec pass and test, (B) gate/label
`RunBeforeSR` as experimental-softer like RR is force-gated, (C) leave as-is. Awaiting
choice — did not implement.

### 2026-09-07 — no missing NGX call / unset param is degrading the NR image

Audited the forwarder (`dlssnr_forwarder.cpp` `dlssnr_call_create`/`evaluate`/`set_extras`)
and the DX12 integration (`DlssNr_Dx12.cpp`) against the 61-name `DLSSNR.*` vocabulary and
NGX conventions. The full surface is driven: Color/Depth/MVec/Output (+UI/UIAlpha/Backbuffer)
resources with subrects; `MVecScaleX/Y` from the game's own encoding; `DepthInverted`,
`Reset` per frame; `Hint.Render.Preset`, `Intensity`, `Style`, `LocalStructureStrength`,
`LocalToneStrength`, `SkinStructureStrength` at **create** (correct — the model reads them
once at build; setting them only at evaluate was a past no-op bug); `Enabled/Width/Height`.
Deliberately not set, each with a reason: `GlobalToneStrength` (not a real DLL string —
Streamline-only, dead write); `ScalingRatio` at eval (it's a query *output* like
DLSSOptimalSettings, not an evaluate input; pre-SR and post-SR both run ratio 1.0);
`BidirectionalDistortionField` (RR frame-warp guide games don't produce; optional for
denoise); `PopulateParameters_Impl` before the real create (NR reuses the game's
already-populated DLSS capability block); alloc/telemetry/override callbacks (NGX
self-allocates). **Exposure:** the NR DLL has NO exposure parameter at all (only internal
CUDA `*_exposure_scale_kernel`); OptiScaler substitutes by pre-scaling colour to a resolved
white point (`ResolveWhitePoint` + exposure-scan anchoring, `DlssNrWhitePointSource` default
1). So exposure is handled via image pre-transform, not a param. Config defaults sane:
`DlssNrPreset` 0 = shipping-default network; `DlssNrWorkingScale` 1.0 = native.
**Conclusion: the softness is architectural (SR-fused model, no jitter/matrix input, run
before SR on a jittered render-res frame) + the tunable `DlssNrWorkingScale`, not a
forgotten call.**

### 2026-09-07 — clarified: the NR→SR jitter handoff is NOT the problem

User asked whether pre-SR mode fails to pass the jitter offset from the NR pass to the
SR pass. Traced it: it does pass, cleanly. NR and SR share the game's single
`InParameters` block. `NVNGX_DLSS_Dx12.cpp:1163/1199` calls `EvaluateBeforeUpscale` with
that pointer; `EvaluateInternal` (`DlssNr_Dx12.cpp:3328`) reads Color/Depth/MVec,
denoises Color in place, and makes **zero `params->Set` calls** (grep = 0 in the file) —
it never touches jitter/MVecScale/subrects. The unmodified block is handed to SR at
`:1166`/`:1202`; SR reads `Jitter_Offset_X/Y` normally (`IFeature.cpp:241`, plus the
per-backend dispatch-desc copies). So SR gets the game's jitter regardless of pre-SR.
The earlier "NR drops jitter" finding is about NR's *own* model input (retail
nvngx_dlssnr.dll has no jitter slot), not a broken NR→SR forward. No plumbing bug.
