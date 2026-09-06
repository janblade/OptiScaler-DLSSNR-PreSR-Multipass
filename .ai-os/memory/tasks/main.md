# Task Memory — main (rolling)

Protected-branch working notes. Drained by `MEMORY_CONSOLIDATE`, not `TASK_CLOSE`.

## 2026-09-06 — repo switch + DLSS-NR/pre-SR-multipass review
- Repointed this checkout's `origin` to `wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass.git`,
  checked out `main` (tracks `origin/main`). Old `dlss-neural-rendering` branch/task file
  preserved locally, no longer active.
- Built Debug|x64 and Release|x64 — both succeed (0 errors). Release post-build packages
  to `x64/Release/a/`.
- Ran `/code-review` on the DLSS-NR + pre-SR multipass feature (commits `926cee08`,
  `facc24f6`). 5 findings: 2 correctness (pre-SR HDR transform reads Output's format
  instead of Color's in `DlssNr_Dx12.cpp`; "Model passes" slider has no dead-control
  indication when proxy backend/scratch-alloc silently caps effective passes to 1), 3
  reuse/simplification (duplicated RR/DLSSD exclusion check across 3 call sites;
  duplicated pass-reset block in 2 places; hand-repeated transition/restore pattern
  instead of RAII, 5 call sites).
- Active plan: memory/plans/2026-09-06-fix-dlssnr-multipass-review-findings.md (done, 6/6)
- All 6 steps executed and verified by build (Debug|x64 + Release|x64, 0 errors each).
  Step 5 shipped 4 of the 5 originally-named RAII sites (the 5th, "encode", doesn't
  match the save/restore shape -- disclosed in the plan file, not forced). Step 2's
  new menu indicator is build-verified only, not exercised in a live game -- flagged
  for an in-game smoke test before release.
- Ran `DEV_IMPLEMENT_REVIEWED` retroactively (`core.dev-loop.sk`), Path A: this host
  has genuine subagent-dispatch, so a second independent reviewer got only the diff +
  original findings, not this session's own reasoning. It caught 2 real bugs the
  initial pass missed, both fixed and re-verified by build: `LastPassCapStatus()` was
  reading `g_nr` without the `g_nrMutex` lock every other entry point takes (race vs.
  `Dispatch()`'s per-frame writes); the menu's pass-cap message had no case for a
  *permanent* pass-creation failure (`passCreateFailed`), so it misleadingly said
  "still building" forever in that case. See plan step 6 for full detail.
- Pushed to `origin` (repoint: `origin` is now `janblade/OptiScaler-DLSSNR-PreSR-Multipass`,
  a fork of `wilsjo2`'s repo, forked via `gh repo fork` since `janblade` had no push
  access to `wilsjo2`'s repo directly). Rebased our 3 local commits onto
  `origin/main`, which had 2 unrelated commits already on it ("native RR controls",
  "per-pass strengths") touching the same DLSS-NR files -- 2 real conflicts in
  `DlssNr_Dx12.cpp` (both were "our SetExtras signature change" vs "their new
  PassTuning line" -- resolved by keeping both), re-verified by a clean Debug|x64
  build after resolving, then pushed. `1366c6fa..1b773ae1 main -> main`.
- Applied EP-1 (`registry/core.planning.sk/SKILL.md`): `PLAN_EXECUTE` now has a
  mandatory independent-review gate (step 6, `core.dev-loop.sk`'s review half) that
  must run once per plan before `Status: done` is set. Closes the gap that let the
  DLSS-NR fix plan above complete without independent review the first time. Not yet
  committed to git — this is an installed project, so commit/push follows normal git
  workflow, not the framework's own `RELEASE` command (project-only, MaiKS's dev repo).
- Ran `/code-review` on DLSS-NR's forwarder-DLL usage (`EnsureForwarder`, the
  function-pointer table, every call site). 5 findings, all verified against the code
  (one also cross-checked against `dlssnr_forwarder.cpp`'s actual exported signature):
  2 real `g_nrMutex` data races (`RetryAfterFailure`; `IsRunning`/`FailureReason`/
  `Calibration`/`GameExposureStatus`), 1 latent UI/UIAlpha aliasing bug in `SetExtras`,
  1 stale-success bug in `EnsureForwarder`'s already-loaded fast path, 1 lock-ordering
  inefficiency in `ProbeD3D11`.
- Active plan: memory/plans/2026-09-06-fix-dlssnr-forwarder-dll-usage-findings.md
  (done, 7/7). Fixed: EnsureForwarder fast-path now checks both create+evaluate;
  RetryAfterFailure + IsRunning/FailureReason/Calibration/GameExposureStatus all
  locked; SetExtras takes a real uiAlpha param (3 call sites updated); ProbeD3D11
  checks the config flag before locking. Step 6's independent-review gate (first
  real run of EP-1's new mandatory step) confirmed all 5 fixes correct, no
  self-deadlocks/lock-ordering issues; its one "bundled changes" note was a false
  alarm caused by nothing being committed to git between this plan and the prior
  one (git diff naturally shows both plans' uncommitted work together). Debug+Release
  x64 both build clean, 0 errors.
