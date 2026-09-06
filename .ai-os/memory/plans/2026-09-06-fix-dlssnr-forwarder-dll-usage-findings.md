# Plan: Fix DLSS-NR forwarder DLL usage findings
- Branch: main
- Created: 2026-09-06
- Status: done
- Task file: memory/tasks/main.md

## Context
`/code-review` on DLSS-NR's usage of its external forwarder DLL (`nvngx.dll_dlssnr.dll`,
loaded via `EnsureForwarder()` in `OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp`) surfaced
5 findings: 2 real data races on `g_nr` fields (unsynchronized against `g_nrMutex`,
which `Dispatch()` holds every frame), 1 latent API-design bug in the forwarder call
wrapper, 1 stale-success bug in the forwarder load fast-path, and 1 lock-contention
inefficiency. All verified directly against the code and against
`OptiScaler/dlssnr/forwarder/dlssnr_forwarder.cpp`'s actual exported signature before
this plan was written.

Acceptance criteria: all 5 findings resolved; Debug|x64 and Release|x64 both still
build with 0 errors; no behavior change for the common case (forwarder loads cleanly,
create+evaluate both resolve, UI/UIAlpha both null) — only the divergent/failure cases
the findings describe.

Out of scope: the Vulkan DLSS-NR path (`DlssNrFeature_Vk.cpp`) and the D3D11 probe's
own D3D11-side logic (`dlssnr_d3d11_*` exports) beyond the one locking-order fix named
below. No automated test suite exists in this repo, so "verify" means a clean build
plus a manual/log-based reasoning check, not a unit test.

## Steps

1. [x] **Fix `EnsureForwarder()`'s stale-success fast path.** `DlssNr_Dx12.cpp:470`
   (`if (g_nr.forwarder != nullptr) return g_nr.create != nullptr;`) only re-checks
   `g_nr.create`, not `g_nr.evaluate`, even though the first-load failure path
   (line 516) requires both to be non-null. Change the fast-path condition to
   `return g_nr.create != nullptr && g_nr.evaluate != nullptr;` so a forwarder that
   loaded but resolved only one of the two symbols consistently reports not-ready.
   — verify: build Debug|x64 clean; re-read the two call sites (`Dispatch()` line
   ~1760, `ProbeD3D11()` line ~3209) to confirm neither assumed the old
   create-only semantics anywhere else.

2. [x] **Lock `RetryAfterFailure()`.** `DlssNr_Dx12.cpp:2862-2868` writes
   `g_nr.failed`/`g_nr.reason`/`g_nr.reset` with no `g_nrMutex` lock, racing
   `Dispatch()`'s per-frame reads/writes of the same fields. Add
   `std::lock_guard<std::mutex> nrLock(g_nrMutex);` as the function's first
   statement, matching the pattern used elsewhere in this file (e.g.
   `LastPassCapStatus()`, `ProbeD3D11()`).
   — verify: build Debug|x64 clean; confirm the lock is acquired before any `g_nr.*`
   write in the function body.

3. [x] **Lock the four remaining unsynchronized `g_nr` accessors.** `IsRunning()`,
   `FailureReason()`, `Calibration()`, and `GameExposureStatus()` (all in the
   `namespace DlssNr` block, `DlssNr_Dx12.cpp` ~3315-3340) read `g_nr` fields every
   frame from the menu with no lock, the same race class as step 2. Add the same
   `std::lock_guard<std::mutex> nrLock(g_nrMutex);` to each. `IsRunning()` and
   `FailureReason()` are currently one-line functions (`{ return ...; }`) — give
   each a proper body so the lock guard has a scope to live in.
   — verify: build Debug|x64 clean; confirm all 4 functions take the lock before
   reading any `g_nr.*` field, and that this doesn't introduce a self-deadlock (none
   of the 4 call anything that itself re-acquires `g_nrMutex`).

4. [x] **Fix `SetExtras()`'s UI/UIAlpha aliasing.** `DlssNr_Dx12.cpp:1314-1324`
   only accepts one `ui` resource and passes it as both the `ui` and `uiAlpha`
   arguments to `g_nr.setExtras(...)`, even though the forwarder's real exported
   signature (`dlssnr_forwarder.cpp:851-867`) treats them as distinct model inputs
   (`DLSSNR.UI` vs `DLSSNR.UIAlpha`). Add a separate `uiAlpha` parameter to
   `SetExtras()`'s signature, pass it through instead of re-using `ui`, and update
   all 3 call sites (`DlssNr_Dx12.cpp` ~1921, ~2050, ~2434 — currently
   `SetExtras(cfg, nullptr, nullptr, 0, 0, 0, 0)`) to pass an explicit `nullptr` for
   the new parameter, preserving today's behavior exactly.
   — verify: build Debug|x64 clean; confirm all 3 call sites updated consistently
   and the forwarder call now forwards two independent resource pointers.

5. [x] **Check the config flag before locking in `ProbeD3D11()`.** `DlssNr_Dx12.cpp`
   ~3191-3205 acquires `g_nrMutex` unconditionally before checking
   `Config::Instance()->DlssNrProbeD3D11`, and since the off-path never sets the
   function's `done` latch, this happens on every frame the D3D11 bridge is active
   whenever the probe is disabled (the default) — needless contention with
   `Dispatch()`'s per-frame lock. Reorder so the config check happens before the
   lock is acquired: only take `g_nrMutex` once the probe is confirmed enabled.
   — verify: build Debug|x64 clean; confirm the disabled (default) path returns
   without touching `g_nrMutex`, and the enabled path's behavior (probe once, set
   `done`) is unchanged.

6. [x] **Independent review gate (`core.dev-loop.sk`, per EP-1).** Dispatched an
   independent reviewer with only the diff and the 5 findings (not this plan's
   reasoning). Result: all 5 fixes confirmed correct — no self-deadlocks, no
   lock-ordering issues against other `g_nrMutex` sites, no lock newly held across a
   blocking call, `SetExtras` call sites all updated consistently, `EnsureForwarder`'s
   fast path still returns `true` for an actually-ready forwarder. One false-alarm
   "scope" note: the reviewer flagged the previous plan's already-reviewed changes
   (RAII guard, `ResetExtraPassState`, `PassCapStatus`, colour-authority fix) as if
   newly bundled into this diff — they're not; nothing has been committed to git
   between the two plans, so a plain `git diff` shows both plans' cumulative
   uncommitted work together. No fix needed; nothing from this review was actionable
   beyond confirming the 5 fixes.

7. [x] **Full verification pass.** Debug|x64 and Release|x64 both build clean, 0
   errors (Debug: 0 warnings; Release: 28 warnings, all pre-existing C4250/LNK4098
   noise from an incremental build recompiling fewer translation units, not new).
   Common-case behavior confirmed unchanged: forwarder load/create/evaluate path
   identical when both symbols resolve; `SetExtras` call sites all still pass
   `nullptr`/`nullptr` for ui/uiAlpha; `ProbeD3D11`'s enabled path (opt-in, off by
   default) behaves identically, only the disabled path skips the mutex now.
