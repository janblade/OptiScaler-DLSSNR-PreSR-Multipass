# Plan: Collapse the DLSS-NR multi-pass rebuild ramp after a forced teardown
- Branch: feat/dlssnr-presr-residual-across-rr
- Created: 2026-09-12
- Status: approved
- Priority: 2 (behind `2026-09-12-dlssnr-multipass-interpass-clamp.md`) — this mechanism only
  fires on a resolution/tuning/placement teardown (e.g. Dynamic Resolution Scaling), which the
  user has not confirmed is active in the game where the boiling was observed. The interpass-
  clamp plan matches the reported symptom (continuous, motion-correlated, no resolution change
  needed) more directly and runs first.
- Task file: memory/tasks/dlss-neural-rendering.md

## Context

Independent review (this session) traced a "boiling on objects in motion" report to
`DlssNr_Dx12.cpp`. Root cause, verified by reading the code (not simulated):

- `resolutionChanged` (`DlssNr_Dx12.cpp:1962-1963`) is a raw per-frame equality check
  against the *active* pre-SR render size — for `RunBeforeSR`, `width`/`height` come
  from `PreSrColorExtent(desc, frame.RenderSubrectWidth, frame.RenderSubrectHeight)`
  (`DlssNr_Dx12.cpp:1821-1823`), i.e. the game's own per-eval Dynamic Resolution
  Scaling (DRS) subrect, unquantized. `workWidth`/`workHeight` are `width`/`height`
  scaled by `DlssNrWorkingScale` (default `1.0`, so at the default setting
  `workWidth == width` exactly) — so at the default working scale, any DRS tick
  changes `resolutionChanged` 1:1.
- Any `resolutionChanged` parks the primary feature *and every extra-pass feature*,
  and (since it also implies a resource-size change) parks every scratch resource
  (`DlssNr_Dx12.cpp:1976-2018`).
- Extra-pass features are then rebuilt **one per `Dispatch()` invocation**
  (`DlssNr_Dx12.cpp:2256-2315`): the loop creates at most one missing
  `g_nr.passFeature[pass]` and immediately `device->Release(); return;`s — a
  deliberate throttle, because evaluating a feature on the same command list that
  created it is the documented "creation-frame GPU hang" (comment at
  `DlssNr_Dx12.cpp:2256-2259`).
- `effectivePasses` (`DlssNr_Dx12.cpp:2721-2733`) only counts a *contiguous* prefix
  of ready, non-pending pass features, so during that trickle the frame is actually
  processed at 1 pass, then 2, then ... up to the configured N, one visible strength
  step per frame, before settling.
- This resize is **not avoidable by ignoring small changes**: the ping-pong scratch
  resources (`g_nr.output`/`g_nr.passScratch`) are allocated at the exact
  `workWidth`x`workHeight` of the active frame (`CreateScratch`, `DlssNr_Dx12.cpp`
  around 2022/2083), not a max-size allocation with a subrect — so when the active
  DRS size genuinely changes, the resources genuinely must be resized every such
  frame. A resize is correct and unavoidable here; what is not necessary is
  *rebuilding the N extra-pass NGX features one frame apart from each other* every
  time it happens.
- DRS reacts to GPU load, which correlates with on-screen motion (camera movement,
  particle-heavy scenes, more visible geometry) far more than with static frames —
  which is why the resulting multi-frame strength ramp reads as boiling that is
  specifically worse on moving content, and specifically worse the higher `Passes`
  is set (a 1-pass config still loses its single history on the same event, but
  recovers as one clean transition, not a graduated multi-step ramp).

**What this plan fixes:** the *width* of that ramp (currently up to `Passes - 1`
extra frames of degraded pass count, once per teardown). **What it does not fix**
(explicitly out of scope, see below): the fact that a teardown happens at all on
every DRS tick, and the "more passes, more artifacts" tradeoff already documented in
`OptiScaler/dlssnr/design/pre-sr-multipass.md`'s Guardrails section.

### Acceptance criteria
- [ ] After a forced full teardown of the pass-feature set (DRS resolution change,
  tuning change, or placement change) while `Passes > 1`, every missing extra-pass
  feature is rebuilt together on the same "build-only" frame, instead of one frame
  per pass.
- [ ] `Passes = 1` behavior is byte-for-byte unchanged (no extra-pass features exist
  in that configuration, so this path is never entered).
- [ ] The primary feature's (`g_nr.feature`, pass 0) own create/evaluate timing is
  untouched by this change.
- [ ] Debug|x64 and Release|x64 both build clean, no new errors or warnings.
- [ ] Log-based (or in-game) confirmation: the
  `DLSS-NR model passes: configured {}, effective {}` line steps directly from 1 to
  the full configured count on the next `Dispatch()` after a forced teardown, rather
  than incrementing by one across several frames — with no new GPU fence-timeout
  warnings appearing in the log as a result of creating multiple NGX features on one
  command list (the one residual risk this plan cannot rule out without hardware —
  see Step 3).

### Out of scope
- The exact-size (vs. max-allocation + subrect) design of the ping-pong scratch
  resources. Switching to a max-size allocation would remove the resize entirely,
  which is the more thorough fix, but is a materially larger change than this report
  warrants on its own.
- Any change to the primary (pass 0) feature's create/pending-submission handling —
  same one-submission-delay pattern, different code, not covered by this finding.
- The generic "later layers converge while cost and artifacts continue to grow"
  tradeoff already acknowledged in `pre-sr-multipass.md`'s Guardrails section.
- The Vulkan/DX11 bridges — this finding and fix are D3D12-path only, matching how
  the multi-pass feature itself is currently scoped.

## Steps

1. [ ] **Batch-build every missing extra-pass feature in one `Dispatch()` frame
   instead of one per frame.** In `DlssNr_Dx12.cpp` (~2260-2315), change the
   creation loop so that when it enters the "build a missing extra pass" branch, it
   keeps creating each remaining missing `g_nr.passFeature[pass]` (for
   `pass = 1 .. requestedPasses - 1`) on the *same* command list before returning —
   rather than creating exactly one and returning immediately. All newly-created
   features naturally still share the current `frame.SubmissionEpoch` and are still
   evaluated for the first time only on a later submission (the existing
   `passPendingSubmission`/`passCreateEpoch` gate at ~2240-2254 already enforces
   that per-pass, unchanged). Keep the single early return once the loop has created
   at least one feature this frame (still skip evaluate this frame — nothing new is
   ready yet regardless of how many were built). A pass whose creation fails
   (`passCreateFailed`) must still stop the loop from continuing past it, matching
   the existing "the ready contiguous prefix remains active" contract — do not build
   pass 2 if pass 1 just failed.
   — verify: build Debug|x64 clean; read through the changed loop and confirm by
   inspection that a full teardown with `Passes=3` now logs three
   "feature for pass N built ... waiting for submission" lines with the same
   `SubmissionEpoch`, on the frame the teardown resolves, instead of one such line
   per `Dispatch()` call across three separate frames.

2. [ ] **Document the pass-0 boundary explicitly.** Add a short comment at the
   batched-build site (or immediately above it) stating that this only applies to
   extra-pass features (`passFeature[1..]`); the primary feature's own
   create/evaluate/one-submission-delay handling is separate code, elsewhere in this
   function, and is intentionally not touched here.
   — verify: comment present, accurately describes the boundary, does not claim
   pass 0 behavior it doesn't implement.

3. [ ] **In-game or log-based settle-time check.** With `Passes=2` or `Passes=3` in
   a title that supports Dynamic Resolution Scaling (or by forcing a resolution
   change via the working-scale slider while playing), trigger a teardown and
   confirm via `OptiScaler.log`:
   - the `DLSS-NR model passes: configured {}, effective {}` line jumps from 1
     straight to the configured count on the very next `Dispatch()`, not
     incrementally;
   - no new `Wait on gpu fence timed out` / `CPU pacer is skipping the frame`
     warnings appear around the teardown that weren't already there before this
     change (the risk this plan can't verify statically: whether creating multiple
     NGX features on one command list is as safe as creating one).
   If new GPU-timeout warnings do appear, revert Step 1 (restore the one-per-frame
   throttle) and treat "batch NGX creation on one list is unsafe" as a new,
   confirmed constraint rather than shipping a hang.
   — verify: `OptiScaler.log` reviewed for both conditions above.

4. [ ] **Full verification pass.** Debug|x64 and Release|x64 both build clean.
   Re-read the diff end-to-end confirming: `Passes=1` never enters the changed loop
   (`requestedPasses - 1 == 0`, loop body never runs); the per-pass
   `passPendingSubmission`/`passCreateEpoch`/one-submission-delay gate is unchanged
   in meaning for every individual pass, only the *build* loop's cadence changed;
   no change to `g_nr.feature` (pass 0) creation.
