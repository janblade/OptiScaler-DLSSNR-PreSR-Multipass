# Plan: Prewarm DLSS-NR's scale-change passes to remove mid-frame PSO-compile stutter risk
- Branch: fix/dlssnr-sgsr1-prewarm
- Created: 2026-09-16
- Status: abandoned
- Task file: memory/tasks/fix_dlssnr-sgsr1-prewarm.md

## Abandoned

Never executed (0/5 steps). Based on a misread of the triggering question: the user's
"anything we added lately that may cause stuttering?" meant ordinary stuttering while
playing, not a one-time hitch specifically tied to crossing the 100% model-resolution
threshold. The PSO-lazy-construction hazard analyzed here may still be real, but it
doesn't match the reported symptom, so this plan doesn't address the actual problem.
Branch `fix/dlssnr-sgsr1-prewarm` deleted (no commits). Left as a dated record in case the
lazy-construction hitch is worth revisiting later on its own merits.

## Context

`OS_Dx12` (`superUp`/`superDown`, supersample leg, `workScale > 1`) and `SGSR1_Dx12`
(`sgsr1UpAnswer`/`sgsr1UpProxy`, reduced leg, `workScale < 1`) are all lazily constructed
the first frame their direction is actually hit (`DlssNr_Dx12.cpp`, `Dispatch()`). Each
constructor calls `Shader_Dx12::CreateComputePipeline` -- a synchronous, driver-side PSO
compile -- on the render thread, inside command-list recording. That is a real stutter
candidate, and worse for the reduced leg than it ever was for supersampling: most users
lower model resolution rather than raise it, and `DlssNrModelResolutionAuto` can cross the
100% boundary for the first time well into a session, mid-scene, under load.

Fix: build all four passes unconditionally, once, on NR's first-ever dispatch -- at the
same point the file already lazily allocates other session-lifetime resources (`g_nr.meter`,
`g_nr.colorSmall`, `g_nr.outputNative`, `g_nr.proxyNative`; `DlssNr_Dx12.cpp:1997-2016`).
This does not eliminate the render-thread PSO compile (no async/loading-time init hook
exists in this codebase to move it off-thread without a larger change), but it converts up
to 4 unpredictable future hitches into one, on the most predictable and least loaded
frame available (NR's startup), instead of an arbitrary later one chosen by whatever the
game/Auto happens to do.

**Acceptance criteria:**
- All four passes (`superUp`, `superDown`, `sgsr1UpAnswer`, `sgsr1UpProxy`) are non-null
  after NR's first dispatch, regardless of that frame's `workScale`.
- The existing per-use-site lazy `new` guards remain as a defensive fallback and do not
  fire in normal operation once prewarm has run (log-confirmed).
- No change to behavior when `DlssNrScalingDownscaler` changes later in a session (the
  existing `g_nr.nrScaler` change-detection at `DlssNr_Dx12.cpp:2526-2532` still rebuilds
  `superUp`/`superDown` correctly).
- Debug + Release x64 build clean, 0 new warnings.

**Out of scope:** moving PSO creation to a genuinely async/background thread (would need a
new init-time hook this codebase doesn't have yet); fixing anything about the passes'
runtime cost/quality, only their construction timing.

## Steps

1. [ ] In `DlssNr_Dx12.cpp` `Dispatch()`, add a prewarm block immediately after the
   existing `g_nr.proxyNative` allocation (~line 2012), guarded by per-object `== nullptr`
   checks: construct `g_nr.superUp`/`g_nr.superDown` with
   `cfg.DlssNrScalingDownscaler.value_or_default()` as `nrScaler` (also setting
   `g_nr.nrScaler` to that value so the existing change-detection at line 2526 sees a
   correct baseline), and `g_nr.sgsr1UpAnswer`/`g_nr.sgsr1UpProxy` -- regardless of the
   current frame's `workScale`. Add one `LOG_INFO`, logged once, reporting init ok/failed
   for all four. — verify: Debug build compiles; manual log inspection confirms the line
   fires on NR's very first dispatch.
2. [ ] Leave the existing lazy `new` guards at each pass's original use site
   (`DlssNr_Dx12.cpp` ~2533-2536 and ~2965-2969) in place, untouched, as a defensive
   fallback for the case prewarm itself failed (e.g. early OOM). Add a one-line comment at
   each site noting it's now normally a no-op after step 1's prewarm. — verify: code
   review only, confirm no behavior change to the fallback path itself.
3. [ ] Manual test A: set `DlssNrWorkingScale` below 1.0 in `OptiScaler.ini`, launch a
   test game, confirm the new prewarm log fires on frame 1 and the existing reduced-path
   "SGSR1 up-leg" log (from `feat/dlssnr-sgsr1-upscale`) reports `answer inst init ok`,
   `proxy inst init ok` immediately, with no separate later construction log. — verify:
   log inspection.
4. [ ] Manual test B: repeat with `DlssNrWorkingScale` above 1.0, confirming
   `superUp`/`superDown` are already built (no mid-session construction) the first time
   the supersample branch runs. — verify: log inspection / no new hitch-adjacent log
   lines appear later in the session.
5. [ ] Debug + Release x64 full build. — verify: 0 errors, 0 new warnings vs baseline.

## Notes

Design alternative considered and rejected: gate prewarm on current config
(`DlssNrWorkingScale != 1.0` or `DlssNrModelResolutionAuto` enabled) instead of building
unconditionally. Rejected per user's explicit choice — it would leave the exact worst-case
scenario (Auto starts at 100%, drops later mid-session) unprotected, which is the scenario
motivating this fix in the first place.
