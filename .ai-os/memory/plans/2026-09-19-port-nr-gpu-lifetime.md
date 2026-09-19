# Plan: Port wilsjo2's NR GPU lifetime tracker (retirement and Starfield crash fixes)

- Branch: `feat/dlssnr-gpu-lifetime-port`
- Created: 2026-09-19
- Status: aborted 2026-09-19 (user decision: no one has reported this bug on the fork; probe found no evidence of it)
- Task file: memory/tasks/feat_dlssnr-gpu-lifetime-port.md
- Ledger: `wilsjo2-fork:nr-retirement-lifetime-fixes` and `wilsjo2-fork:starfield-nr-crash-serialize`
- Source: wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass, GPL-3.0, refs `85ac8481` `10e33859` `7d909d47` `f450b6ab` (= `ecc99165`) `b48637f7` (= `dc5611d6`). Final code read from `codex/release-v0.8.4` (`8802b2b4`).

## Study

**What it is.** A self-contained class, `DlssNr::GpuLifetime` (about 200 lines, `OptiScaler/dlssnr/DlssNr_GpuLifetime.{h,cpp}`). It records which command lists touched NR resources, watches for them being reset or released, signals a fence per queue when they are really submitted, and only runs a retirement callback once every recording is closed and every fence has completed. It has no NGX or NR dependency.

**What we do today.** `TickNrRetired()` ([DlssNr_Dx12.cpp:805](OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp#L805), called once per frame at :2296) frees a retired NR feature or resource after a fixed `framesLeft = 32` countdown ([:754](OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp#L754)). Nothing checks that the GPU has finished with it. `ParkNrFeature` has 6 call sites and `ParkNrResource` 18.

**What each source commit fixes, and whether it applies here:**

| Source | Their problem | Applies to us? |
|---|---|---|
| `85ac8481` | Introduces the tracker inside a 4,400-line restructure of `DlssNr_Dx12.cpp` | The tracker file is separable. The restructure is not portable. |
| `10e33859` | Cyberpunk reached 34 GB VRAM while NR time rose from about 4 to 23 ms. Logs showed unresolved recordings at teardown | Unknown. Our 32-frame countdown frees on time, so it is not obviously leaking. It could free too early instead. |
| `7d909d47` | Access violation: NGX destruction re-entered hooks while `std::erase_if` was compacting the retired vector | **The exact bug is not in our code.** `TickNrRetired` walks by index, not by iterator. Re-entrancy is still untested. |
| `f450b6ab` | Retired generations were not released after option changes | Partly. We park resources at option changes (:1935-1979) but never tie that to completion. |
| `b48637f7` | Starfield crash: a null recording entry in `ResetRecording`, from tracker state mutated outside the reset lock | **Does not apply on its own.** It fixes their tracker, which we do not have. Our `GpuTime::ResetRecording` walks a fixed array, so it has no null-entry case. It only matters if we adopt the tracker, and the final `GpuLifetime.cpp` already contains the fix. |

**Evidence they give.** A WARP test (`tests/nr_gpu_lifetime_smoke.cpp`, 329 lines): 64 destroyed unsubmitted lists, blocked submissions, replay, wrapped identities, several queues, re-entrant retirement of 64 callbacks, and a four-worker race of 8,000 callbacks. Their notes say the old collector fails the re-entrant case and the pre-fix tracker crashes the race. They also say it does not prove long-session VRAM stability and that Starfield was never tested locally. Its separate `0xBAD0000B` NR init error is not fixed.

**Evidence we lack.** No report here shows premature free, a leak, or a Starfield crash from our retirement path. Everything above is their evidence on their code.

**Licence.** GPL-3.0 both sides. The file needs `pch.h` and `Util::CheckForRealObject`, both of which we have (the latter is used in `DlssNr_GpuTime.h`).

**Recommendation: port the tracker, not the restructure.** Lower risk than it sounds because the tracker is additive and has its own game-free test. The real change is replacing the frame countdown, so measure first (step 1). Treat `starfield-nr-crash-serialize` as folded in: it is a property of the final file, not a separate change.

## Steps

1. [x] **Measure before changing anything (optional but recommended).** *Built and run once 2026-09-19 (Cyberpunk 2077, see Result below).* A `RetireProbe` block in [DlssNr_Dx12.cpp](OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp) plus three one-line call sites (`EvaluateInternal`, and the submit/reset hooks in `DlssNr_Late.inl`). When a retired item's countdown expires it logs a `DLSS-NR retire probe:` WARN if a command list that entered NR at or before the parking frame is still open, and an INFO summary every 600 evaluates. Log only, no behaviour change.
   - **How to read it.** No WARN lines and `0 with a still-open command list` in the summary across a session with option changes means the countdown has held in that session, and the port is hardening rather than a fix. WARN lines mean a free happened under a list that had not been reset or released yet. "Never submitted" is the stronger signal.
   - **What it cannot show.** GPU completion (no fences, no queue signals), and it over-reports slightly because `Record` fires before NR decides whether it will record. A clean run is evidence, not proof, for the game and settings tried.
   - **Isolated check:** the probe block compiled `/W4` clean and passed a WARP test of open, unsubmitted, parked-frame, reset and destroyed-list handling. The full project has not been built.
   - **Result, run 1:** Cyberpunk 2077, RR on, NR after RR+SR at 2560x1440, FSR frame generation active, NR passes changed 2 to 1 and the model resolution rebuilt from 2048x1152 to 1706x960 during the run. Cumulative counters at the last summary: 61 retired items released, **0 with a still-open command list**, 0 never submitted; retire list peak 12; 2 command lists tracked. NR time stayed flat at about 4-6 ms. The log kept only the last ~110 s (counters are cumulative, so they cover the whole run), and only 6 summaries survived. No evidence of a premature free. The session was short; wilsjo2's Cyberpunk report was a long session.
   - **Run 2:** fresh Cyberpunk session, 13 min, no NR option changes, so 0 items were ever retired and the probe checked nothing. NR time drifted from about 5.5 ms to 9 ms in the last two minutes on 3-4 samples a minute (not attributable to retirement, since nothing was parked).
   - **Removed 2026-09-19:** probe reverted with `git checkout`; the code is back to the committed state. Original removal note: delete the `RetireProbe` block, `g_retireProbe` call sites, `NrRetired::parkedFrame` and the five added `#include`s.
2. [ ] **Add the module.** Copy `DlssNr_GpuLifetime.h/.cpp` from `codex/release-v0.8.4` into `OptiScaler/dlssnr/`, add both to `OptiScaler.vcxproj` and `.filters`. Do not edit the logic; this file carries the reentrancy and lock-order fixes.
3. [ ] **Port the test first.** Copy `tests/nr_gpu_lifetime_smoke.cpp` and `tests/run_nr_gpu_lifetime.ps1` from the same ref and run it under WARP. This is the game-free gate. It must pass before any wiring. Match how `nr_interpass_clamp_smoke.cpp` is built.
4. [ ] **Own a tracker in NR state.** One `DlssNr::GpuLifetime` next to `g_nrRetired` ([:757](OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp#L757)).
5. [ ] **Record command lists.** Call `Record(cmdList)` where NR records: `EvaluateInternal` ([:3255](OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp#L3255)) and the two wrappers, the state envelope, and the late-slot arming in `DlssNr_Late.inl`. Enumerate the remaining sites by grep for command-list use in NR; do not guess. Check `DlssNr_DeferredSr.inl` separately, since it has its own generation handling.
6. [ ] **Forward the notifications we already receive.** In `DlssNr_Late.inl`, `FinishedPictureSubmitted` (:231) and `FinishedPictureResetCommandList` (:191) already call `g_gpuTime` and `g_ngxTime` under `g_nrMutex`. Add `g_lifetime.Submitted` and `g_lifetime.ResetRecording` beside them. Lock order is `g_nrMutex` then the tracker's own mutex, which matches the order their doc requires.
7. [ ] **Replace the countdown.** `ParkNrFeature` and `ParkNrResource` hand ownership to `g_lifetime.Retire(callback)` instead of pushing a `framesLeft` entry. `TickNrRetired` becomes `g_lifetime.Collect()`. Call `BeginGeneration()` where option changes rebuild resources (:1935-1979). Each item must have exactly one owner, the tracker callback, so the shutdown drain (:3816) cannot double-free.
8. [ ] **Shutdown.** Keep the existing forced release at teardown, but only for items the tracker has abandoned. Confirm by reading that abandoned callbacks capture raw ownership and are never invoked after destruction.
9. [ ] **Provenance.** Commit trailer `Ported-from: wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass@85ac8481, 10e33859, 7d909d47, f450b6ab, b48637f7`. Add a line to `docs/CREDITS.md` for the tracker and its test. Port `docs/NR-GPU-RETIREMENT.md` in our words, keeping their stated limits.

## Verification

- **No game needed:** the WARP lifetime test (step 3); Release x64 compile. The user builds when they say so.
- **Needs a game, not claimed until run:** a long session with repeated option changes for VRAM and NR time, ideally Cyberpunk since that is where they saw it; a Starfield launch. Both are unverified even upstream.
- Step 1's log is the only thing that can show the change fixes something real here.

## Risks

- We replace behaviour that works today. A tracker that never completes (a list never reset or released) would hold resources forever, so keep a hard upper bound or log loudly.
- Our monolith has one generation of state. Their model has per-owner trackers (enlargers, deferred SR). Only the first is needed if we do not adopt their structure.
- Lock order inside a 4,000-line file is easy to violate. Review step 6 against `g_nrMutex` users.

## Out of scope

The 4,400-line restructure of `DlssNr_Dx12.cpp`, the split into `DlssNr_Dx12_*.cpp`, the Vulkan split in `85ac8481`, per-owner trackers for their enlarger, and the Starfield NR init error `0xBAD0000B`.

## Rollback

Work on the branch above. Dropping the branch restores the frame countdown unchanged.
