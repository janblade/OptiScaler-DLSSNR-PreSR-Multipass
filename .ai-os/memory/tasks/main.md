# Task Memory — main (rolling)

Protected-branch working notes. Drained by `MEMORY_CONSOLIDATE`, not `TASK_CLOSE`.

## State @ 2026-09-09 (wilsjo2 v0.7.2 + v0.7.3 sync; then v0.7.4 + v0.7.5)

`main` HEAD = `bbfd9547` (ai-os record) ← `66571fd3` (Merge wilsjo2/main v0.7.4/v0.7.5) ←
`eebbd2c5` (ai-os record) ← `21308d32` (Merge wilsjo2/main v0.7.2/v0.7.3). **Pushed to
`origin/main` 2026-09-09** (`b9a50ea4..bbfd9547`). Pre-sync HEAD was `b9a50ea4`.

**wilsjo2 v0.7.4 + v0.7.5 sync — DONE (local), 2026-09-09.** `git merge --no-ff`, 8 commits
off `cb1f7aa3`:
- `b7484ef9` v0.7.4-unified-nr: unify NR controls for SR and RR, simplify menu descriptions
  (`DlssNr_Menu.cpp` −274 lines); drop `DlssNrApplyAfterRR` / `DlssNrRRPasses` /
  `DlssNrRRWorkingScale` (RR now shares the SR pass/working-scale settings).
- `4a96e741` + PR #8: fix NR submission epoch on DXVK presentations; new
  `DlssNr_SeamClock.h` + `nr_seam_clock_smoke.cpp`.
- PR #11 + `fc87c566`: stabilize deferred NR frame pairing without bypassing submission
  guards (`DlssNr_DeferredSr.inl` +67).
- PR #13: NR control clarification (absorbed our tooltip commit `98e8d48f` upstream, then
  `b7484ef9` trimmed it).
- docs: `INSTALL-DLSSNR.md`, `docs/releases/v0.7.{4,5}.md`, `docs/PR-REVIEW-20260909.md`.
- **1 conflict:** `DlssNr_Menu.cpp` — took **wilsjo2's wholesale** (user decision). Their
  v0.7.4 deliberately simplified the NR tooltips after absorbing our DLSS-5-grounded
  version; staying lockstep stops this file re-conflicting every sync. Our verbose text is
  in history (`09e3cd4b`).
- `DlssNrNative.cpp` untouched by this range — no hybrid conflict.
- Debug|x64 + Release|x64 **Build succeeded, 0 errors**; Debug warnings 63 (baseline).
  Forced recompile of the merge-touched NR TUs → 0 warnings in any changed file.
- **PR #4 re-port — DONE (branch, not pushed).** `fix/hybrid-async-init-report` off
  `75ad586c`, 2 commits: `eb3f8c15` async device init (concern 1), `a769526a` recoverable
  `restartRequired` + bounded session map (concerns 2+3). All 3 confirmed still applicable
  against wilsjo2's v0.7.2 code (nothing upstream addressed them; v0.7.3-v0.7.5 didn't
  touch `DlssNrNative.cpp`). Adapted to the `(device,candidate)` keying, 4-tuple session
  key, `SetPrecision` entry point, and candidate `Prepare` branch. Debug + Release x64
  clean rebuild, 0 errors, 63/26 warnings (baseline), 0 in `DlssNrNative.cpp`. Compile-only
  — `Precision=4` needs Blackwell + `OptiScaler/nvfp4/hybrid` assets to run; stays draft
  until hardware-measured. **Draft PR janblade#5** open (fix/hybrid-async-init-report ->
  main), branch + main pushed to origin. `main` = `285f09e2` on origin.

---

### v0.7.2/v0.7.3 sync record (superseded HEAD, kept for context)

`main` HEAD was `21308d32` (Merge wilsjo2/main). Prior HEAD `b9a50ea4`.

**wilsjo2/main sync — DONE (local).** `git merge --no-ff` (established pattern: main is
shared + has merge commits, never rebase). 2 incoming commits off merge-base `ec96f42f`:
- `d2b65cda` v0.7.2-hybrid: promote combined NVFP4 hybrid, keep previous path for
  comparison; `DlssNrPrecision` hybrid value `2 -> 4`; new "candidate" split-half
  contraction path in `DlssNrNative.cpp`.
- `cb1f7aa3` v0.7.3-kcd2 (co-authored cmh1448): wilsjo2's own adaptation of cmh1448's
  `6446cc8d` NR motion-vector metadata fix — pre-SR + multipass + native Vulkan; new
  `OptiScaler/shaders/dlssnr/DlssNr_Guides.h` (`ResolveGuideRegions`/`GuideSubrect`) +
  `tests/nr_guides_smoke.cpp`; `DlssNrFrameInfo` gains `Depth/MotionSubrectBaseX/Y`,
  `MotionVectorsLowResolution`, `OutputWidth/OutputHeight`; forwarder ABI versioned as
  `dlssnr_{call,vk}_evaluate_v2`. Plus KCD2 output-cap docs, DLSSG/NvApiHooks tweaks,
  Streamline 2.14.1 pin.
- New/changed: `DlssNr_Guides.h`, `nr_guides_smoke.cpp`, `docs/{HYBRID-V072-VALIDATION,
  NR-MOTION-METADATA,releases/v0.7.3}.md`, `Config.{h,cpp}`, `OptiScaler.ini`,
  `redist/streamline/manifest.json`, `package_release.ps1`.
- **2 conflicts:**
  - `DlssNrNative.cpp` — took **wilsjo2's wholesale** (user decision). Their promoted line
    adds the candidate split-half path + `(device,candidate)`-keyed maps + sync `Prepare`.
    **Our `c37c185d` (PR #4: async device init off the render thread, no-restart recovery
    from latched failure, LRU session-map bounding + graveyard) is DROPPED from the merge.**
    FOLLOW-UP OWED: re-port those 3 improvements onto the new `DlssNrNative.cpp` structure
    as a separate reviewed change.
  - `DlssNr_Menu.cpp` — took wilsjo2's precision-combo structure (`== 4`/`4u`,
    "NVIDIA (FP8)" / "Experimental (FP8+NVFP4 hybrid)" labels, `precisionChoice > 0` status
    check); kept our always-shown DLSS-5-grounded `HelpMarker`, folded in their
    "1440p rounding" caveat.
- `.ai-os/` (55 files) preserved — wilsjo2 carries none. `CLAUDE.md`/`AGENTS.md`/`.claude/`
  were already absent from `b9a50ea4` (not a merge effect).
- Debug|x64 + Release|x64 both **Build succeeded, 0 errors**; warnings at baseline
  (Release 26 / Debug 63 whole-solution). NR MV + hybrid candidate paths are
  auto-merge/compile-verified only — validate in-game (hybrid needs Blackwell + nvfp4
  assets).
- **NOT pushed.** `main` push (protected, shared) needs explicit go-ahead.

**`fix/dlssnr-mv-subrect-metadata` — ABANDONED + deleted.** It was our own port of the same
cmh1448 fix; v0.7.3's is the superset (pre-SR, shared guides header + smoke test, explicit
`OutputWidth/Height`, `_v2` ABI). Plan `memory/plans/2026-09-09-dlssnr-mv-subrect-metadata.md`
marked `abandoned` with the finding. No code from it lands.

Queued epic (approved, not started): memory/plans/2026-09-09-neural-manhwa-restyle.md
(epic approved, 0/5 stories) — "reverse-NR": train our own real-time model to restyle game
frames toward a Solo Leveling colored-manhwa look, run through OptiScaler in the NR slot.
NOT retraining nvngx_dlssnr.dll (closed engine). Method default CUT->distilled feed-forward
(diffusion-distill fallback); runtime default ONNX Runtime + DirectML behind `IStyleInference`.
Story 2 (offline stills quality) is the go/no-go gate. Training in a new `tools/style-transfer/`
(PyTorch, outside OptiScaler.sln); long-horizon, mostly offline.

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

**jitter-cancel prototype** (`memory/plans/2026-09-07-dlssnr-presr-jitter-cancel-mv.md`): committed
`639a58ec` on `experiment/dlssnr-presr-jitter-mv`, pushed; **PR janblade#2 (experiment -> main)
open**. First in-game pass "barely noticeable"; landing default-off, sweep continues. **PR #2 now
conflicts with the wilsjo2 NR changes** (`dlssnr.hlsl`, `DlssNr_Dx12.cpp`, precompiled shaders,
`Config.*`, `DlssNr_Menu.cpp`) — needs `experiment` rebased onto the new `main` before it merges
cleanly. Not done (not requested).

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

### 2026-09-09 — reference doc: NR requirements matrix (API x GPU gen)

User asked for a requirements mapping table: what it takes for DLSS-NR to be usable across
DX9/DX11/DX12 x RTX 50/40/30, standalone and with DLSS RR. Wrote `docs/NR-REQUIREMENTS-MATRIX.md`
(not committed). Key findings from the code, not just docs:
- **DX9 = N/A** everywhere — OptiScaler has no D3D9 hooks and no DX9 game has a temporal upscaler.
- **DX11 = bridge only** — NR needs the D3D12 seam (`IFeature_Dx11wDx12.cpp` call site); native
  pure-D3D11 FSR2/XeSS backends don't carry the pass. Run via `with_dx12`.
- **DX12 = full** — native seam, all placements (post-SR / pre-SR multipass 1-3 / DeferredDLSS).
- **Vulkan native = yes** for post-SR + pre-SR (`DlssNrFeature_Vk.cpp`); **DeferredDLSS is
  D3D12-only** (Vk logs a diagnostic, leaves frame unchanged, `DlssNrFeature_Vk.cpp:527`).
  Vulkan-via-D3D12-bridge = full incl. DeferredDLSS.
- **GPU gen only changes the `nvngx_dlssnr.dll` you supply**: RTX 50 = original NVIDIA-signed
  310.8; RTX 20/30/40 = ShortFuse cross-gen 310.8 (auto Ada vs FP16 path; sig reports invalid,
  verify by SHA-256). Hardware gate = `architecture_id >= TU100`.
- **NR + RR**: NR runs on the RR+SR seam, supported placement = **after RR+SR**; `rayReconstruction`
  flag threads through `EvaluateInternal` / `EvaluateAtSeamVk`. **DeferredDLSS hard-disabled when
  RR is active** (`DlssNr_Dx12.cpp:2902`). Pre-RR ("before RR+SR") path exists but design doc
  flags post-RR as the contract -> experimental. Needs the game to actually integrate RR + a
  `nvngx_dlssd.dll` runtime. No DX11 game ships RR -> N/A in practice.
- RTX 30/20 fragility carried over from `docs/NR-COMPATIBILITY.md` (Onimusha/RE Engine, candidate
  fix only).

Extended 2026-09-09: added §3 (NR + Frame Generation) and §4 (NR + Multi-Frame Generation)
to `docs/NR-REQUIREMENTS-MATRIX.md`. FG/MFG facts: OptiFG output is DX12-only (DX11/Vk =
bridge/experimental); NVIDIA DLSS-G needs RTX 40/50 + Streamline 2.14.1 + `nvngx_dlssg.dll`;
RTX 30/20 get FG/MFG only via FSR replacement (`FGNvngxReplacement=Nukems/Arturs/FFX/Combo`).
Native MFG = RTX 50 (`[DLSSG] InterpolationCount` 2-5). RTX 40 MFG = `AdaMfgUnlock` in-memory
patch (from `MfgUnlock.h`, y4my4my4m-derived, ceiling 6x) or external Dashdogy ASI — neither
hardware-verified. NR runs alongside FG (separate subsystem); bring FG up first, then add one
NR pass. `[FrameGen] External=true` keeps NR but drops OptiScaler FG routing. ResidualFG
(`DeferredDLSS`+`ResidualFG`) is a distinct half-rate-NR experiment, D3D12/DX11-bridge only.
Doc still uncommitted.

Extended again 2026-09-09: added §5 (file placement table: source -> destination -> what it
enables) and §6 (menu control -> INI key, by goal) to `docs/NR-REQUIREMENTS-MATRIX.md`.
Verified INI keys against Config.cpp: `[DlssNr]` Enabled/RunBeforeSR/DeferredDLSS/ResidualFG/
Precision(0|4)/Passes/WorkingScale; `[FrameGen]` Enabled/External/FGInput(upscaler)/FGOutput
(fsrfg|xefg|dlssg)/FGNvngxReplacement(None|Nukems|Arturs|FFX|Combo); `[DLSSG]` InterpolationCount
(1-6)/AdaMfgUnlock/AdaBlackwellKernels/ForceDMFG/FramerateTargetDMFG. Menu labels from
menu_common.cpp: "FG Input"/"FG Output"/"FG Nvngx Replacement" combos, "Override DLSSG Ratio",
"Built-in RTX 40 MFG unlock (experimental; restart)", "External frame generation / MFG unlocker".
DLL search: NR DLLs = g_dllDir then exe dir; Nukems/Arturs = MainDllPath (OptiDllPath);
streamline/ = <OptiDllPath>/streamline. Doc = 7 sections now, still uncommitted.

### 2026-09-09 — applied independent-reviewer findings to NR-REQUIREMENTS-MATRIX.md

Spawned general-purpose subagent (independent Review Pass, cohesiveness/flow/navigation lens).
8 findings, all applied via full rewrite:
1. Added "## How to read this document" (Contents TOC + Matrix legend + Key concepts glossary)
   between intro and §0. Legend now defines 4 tokens incl. **Experimental**; every §1-§4 cell
   prefixed with one bold token; one-line legend repeated under each matrix.
2. Fixed all "(see §3)" fragility pointers -> "(see [§7](#7-caveats))" anchor links.
3. TOC added; every matrix has a "-> File placement / Menu-INI / Caveats" anchor-link footer;
   §4 Path table has a "Menu / INI" column linking to §6 goal sub-sections.
4. Key concepts glossary: post-SR, pre-SR multipass, DeferredDLSS, ResidualFG, hybrid precision,
   forwarder vs model DLL, ShortFuse cross-gen runtime, bridge - each links to its deep section.
5. All 4 matrices now identical shape: same column headers "RTX 50 (Blackwell)|40 (Ada)|30
   (Ampere)", same 5 API row labels ("Vulkan (D3D12 bridge)" everywhere).
6. Repeated constraints (DeferredDLSS limits x5, RTX30-replacement-provider x3) collapsed:
   §7 is now authoritative, elsewhere = short tag + §7 link.
7. Intro source dump moved to "## Sources" at end.
8. Minor: "FG runs at frame-present time"; §1 runtime table moved before its matrix; RTX 20
   scope stated once in the legend ("behaves like RTX 30 everywhere").
Section headings renamed to punctuation-free forms for clean GitHub anchors. Doc still
uncommitted. Reviewer praised §0 device, per-section "extra requirements" scaffolding, §6
goal structure, cross-doc terminology - all preserved.

### 2026-09-09 — second reviewer pass applied to NR-REQUIREMENTS-MATRIX.md

Fresh general-purpose subagent, same lens. 10 findings + minors, all addressed:
1. "Builds on" chain unified: §1 labelled the NR baseline ("§0 + model runtime"); §2/§3/§4 all
   open "Extra requirements on top of §1"; §4 says "§3 (= §1 + FG)".
2. New "### Frame Generation provider reference" table in §6 (menu label <-> INI value <-> DLL
   <-> result); §3 bullets + §5 footer + §6 subsections now point to it; dropped ad-hoc name
   variants ("DLSS Enabler (Artur)" -> "DLSS Enabler").
3. §7 claim narrowed to "each constraint a matrix *cell* tags"; §3/§4 scenario-wide notes
   relabelled "Scenario-wide notes (not cell-specific, stay here)".
4. Default placement canonical = "post-SR": §6 heading renamed "NR post-SR (baseline
   placement)"; Key concepts + §1 footer links updated to #nr-post-sr-baseline-placement.
5. FSR-based MFG for RTX 40: new §6 heading "NR with FSR-based MFG (any GPU)"
   (#nr-with-fsr-based-mfg-any-gpu, was "RTX 30 and older"); §4 RTX 40/30 DX12 cells and the
   RTX 40-unlock subsection now link to it.
6. Sources: `INSTALL-DLSSNR.md` -> "`INSTALL-DLSSNR.md` (repo root)" (it's repo-root, not docs/).
7. "Extra requirements" block shape unified: bullets -> matrix -> supplementary table for
   §1/§4; §1 runtime table moved after the matrix.
8. Dropped the duplicate Key concepts "Bridge" row (kept legend's); §5 closing 2 paragraphs
   -> 1-line pointer to Key concepts.
9. TOC: added indented §6 sub-list (provider reference + 11 goal subsections, grouped 3 lines).
10. Key concepts "DeferredDLSS" More-link repointed §7 -> §6 subsection for parity.
Minor: "feature-18" -> "NGX feature 18" glossed in intro; §4 compound cells rewritten to lead
with one legend token; pre-sr-multipass.md path made consistent (full path in §7).
Validated: all 82 internal anchor refs resolve, no duplicate heading slugs (python check).
Doc still uncommitted.

### 2026-09-09 — NR-REQUIREMENTS-MATRIX.md review loop (user: cycle until no actionable comments, max 5)

Cycle 1 — applied review #3 (8 items):
1. Intro "builds on" fixed: §1 = baseline; §2/§3 add to §1; §4 also needs §3. (was "each builds
   on the one before it" — false)
2. Uniform scenario template: every §1-§4 now = 1 intro sentence + "**Builds on:** [§X]" +
   "Extra requirements on top of §X:" bullets + legend + matrix + table + footer. §4 got the
   bulleted skeleton it lacked; §2 got an intro sentence.
3. RTX 20 naming canonicalised to "RTX 30 / 20" in prose/cells; legend defines the bucket;
   "Ampere / Turing" kept only in the FP16-path note. Matrix "fragile on Ampere" -> "Fragile
   ([§7])" (column already says Ampere).
4. §7 named in the intro's "links down to" sentence (kept §7 last rather than renumber — the
   reviewer's lightweight alternative).
5. Contents: added "How to read this document" row w/ Matrix legend + Key concepts sub-links.
6. §6 Contents quick-links now one labelled "*§6 quick links*" dot-separated line.
7. §6 DeferredDLSS constraint pre-summary trimmed to bare "Constraints: [§7]".
8. Opening sentence split into two.
Also: dropped GPU parentheticals from 4 §6 headings for cleaner anchors
(#nr-with-fsr-frame-generation, #nr-with-real-nvidia-dlss-g, #nr-with-fsr-replacement-fg,
#nr-with-fsr-based-mfg); all inbound links updated; §6 cross-refs between subsections now
hyperlinked. Validated: 93 anchor refs resolve, no dup slugs.
Review #4 dispatched (general-purpose, cold).

Cycle 2 — applied review #4 (verdict "it succeeds"; 1 Should-fix + 5 polish, all done):
1. (Should fix) §7 now backs every cell that links to it: added "Ray Reconstruction adds
   further GPU cost" to the fragility bullet, and a new bullet "RTX 40 MFG unlock is
   experimental / unverified on Ada" (folded in the don't-run-on-RTX-30/20 line).
2. Trimmed §6 "FSR replacement FG" / "FSR-based MFG" subsections to point at the provider
   reference instead of restating label<->INI mappings.
3. Pre-matrix legend link text "How to read" -> "Matrix legend" (x4) so text matches target.
4. §6 Contents quick-links: one 12-link middot line -> 3 grouped indented lines
   (placement / frame gen / multi-frame).
5. DX9 rows: added "([§7])" pointer to the first cell of all 4 DX9 matrix rows.
6. "ShortFuse cross-gen" unified to "ShortFuse cross-generation runtime" everywhere
   (KC term + §5 + runtime table "..., 310.8"); KC row notes the canonical name.
Validated: 98 anchor refs resolve, no dup slugs; greps confirm no leftover "cross-gen ",
"How to read](#", "fragile on Ampere", "RTX 30 and older".
Review #5 dispatched (general-purpose, cold; also asked to verify §7 pointers back their claims).

Cycle 3 — applied review #5 (verdict "it succeeds"; 1 low Should-fix + 7 polish, all done):
1. KC "Hybrid precision" More-link §7 -> "§6 · §7" (was routing to constraints not the toggle).
2. Legend: added sentence that a bridge path shows as **Experimental** in §3-§4 (FG-unvalidated).
3. Legend: RTX 30/20 bucket note now says the column header abbreviates it.
4. Intro reading-order: "How to read" listed before §0 (it physically precedes it).
5. §5 "Put it" column normalised to one label ("beside OptiScaler"); intro para rewritten to
   define it first and note OptiDllPath is the only case where locations differ.
6. Forwarder DLL: one term "forwarder shim" (KC + §0 + §5); "caller-gate" kept once as alias.
7. §6 order: "NR with DeferredDLSS" moved above "NR with Ray Reconstruction" (group §1-placement
   items); Contents quick-link order matched.
8. Intro scenario links "[2]/[3]/[4]" -> "[§2]/[§3]/[§4]".
Validated: 99 anchor refs resolve, no dup slugs, greps clean.
Review #6 dispatched (cold; told to report "no should-fix" if only cosmetic nits remain, cap 3).
This is cycle 3 of 5; reviews have trended to "it succeeds" for three passes running.

Cycle 4 — applied review #6 (verdict "strong"; 2 Should-fix contradictions + 2 polish, all done):
1. InterpolationCount ceiling reconciled: mapping stated once in §4 intro ("generated frames;
   multiplier = count+1; 2->3x, 3->4x, 5->6x; config accepts 1-6, runtime clamps, treat 5=6x as
   ceiling"). §4 matrix "2-5 runtime-clamped", §4 Ada row "ceiling InterpolationCount 5 = 6x",
   §6 RTX50 "up to 5 (6x)", §6 RTX40 "2...5 (3x-6x)" — all agree now (was 2-5 / up to 6 / 2...6).
2. §6 "FSR replacement FG" INI line: added missing `FFX` -> `Nukems | Arturs | FFX | Combo`.
3. §3 + §4 "Menu / INI" footer links -> section head #6-menu-settings-and-their-ini-keys
   (was landing on one sub-case; §1/§2 keep their single-subsection targets).
4. §0 row label "The seam must reach D3D12" -> "...reach a supported backend" (its own detail
   names the native-Vulkan path, which doesn't reach D3D12).
Validated: 99 anchor refs resolve, no dup slugs.
Review #7 dispatched — cycle 5 (final). If clean / polish-only, loop stops.

Cycle 5 (final, cap reached) — applied review #7 (verdict "in good shape"; 1 Should-fix + 2 polish):
1. Streamline path self-contradiction: §5 row now says `OptiScaler/streamline/` (matches §6 +
   docs/DLSS-FRAME-GENERATION.md); §5 intro rewritten — "beside OptiScaler" = folder the renamed
   proxy loads from; dropped the false "OptiDllPath defaults to the exe folder" claim; noted the
   `OptiScaler/` backend folder + streamline nesting inside it.
2. "Menu" vs "Overlay" vs "panel" -> standardised on "Overlay". §6 retitled "Overlay settings
   and their INI keys" (anchor #6-overlay-settings-and-their-ini-keys); Contents + intro + all
   4 footers + MFG-paths table header + "panel reports" -> "overlay".
3. §1 "Overlay / INI" footer repointed from #nr-post-sr-baseline-placement to the §6 section
   head (§1 matrix spans 3 placements); §2 keeps its single-subsection target.
Validated: 99 anchor refs resolve, no dup slugs, grep-clean.

REVIEW LOOP COMPLETE — 5 cycles run (reviews #3-#7). Trend: structural findings in #3-#4,
narrowing to 1-2 localized self-contradictions per pass by #6-#7; every pass verdict "succeeds
/ good shape". Doc = 8 sections (How-to-read + §0-§7 + Sources), all internal anchors verified
each cycle. STILL UNCOMMITTED — user has not asked to commit; `docs/NR-REQUIREMENTS-MATRIX.md`
is the only tracked change beyond the ai-os task memory.
