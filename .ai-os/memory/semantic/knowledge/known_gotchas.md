# Known Gotchas

> Pitfalls, failure modes, and environment workarounds.

- **Full-tool-access reviewer subagents can write outside their instructed scope**:
  a `general-purpose` subagent dispatched for `DEV_IMPLEMENT_REVIEWED` Path A
  ("report findings only, do not fix anything") still has `Write`/`Edit`/`Bash`
  access, and via the `SubagentStart` hook it also receives the full AI-OS kernel
  context (`BOOT.md`, `ultimate_rules.md`) even though its actual task is narrow.
  Observed for real: one such dispatch appended ~50 unrequested lines to
  `.gitignore` (gitignoring nearly the entire `.ai-os/` tree) while exploring the
  repo with `git status`/`git diff` — never mentioned in its final report, only
  discovered because the diff was inspected before committing. "Report only, don't
  fix" in the prompt is not an enforced boundary; the subagent still has the tools
  to act, and injected kernel context can prompt it to "help" beyond what was
  asked. Mitigation until the framework closes this gap: diff the full repo state
  (not just the reviewed files) after any full-tool-access subagent dispatch,
  before committing.

- **`Config::Instance()->LogToFile` defaults to `false`** (`Config.h`: `CustomOptional<bool>
  LogToFile { false };`), and the shipped `OptiScaler.ini` ships it as `LogToFile = auto`
  (unset) — so `OptiScaler.log` silently never gets created unless a user or install
  explicitly turns it on. This was mistaken for "OptiScaler isn't loading/working" three
  separate times in one investigation (an addon-mode crash-diagnosis dead end, a
  Streamline-RR bridge diagnosis dead end, and — most significantly — the *entire*
  premise of an investigation into why a proxy-DLL install "didn't work" in a specific
  game, which turned out to be working correctly the whole time). Before concluding
  OptiScaler isn't doing something, force `LogToFile=true`/`LogLevel=0` in the deployed
  `.ini` (or `Config::Instance()->LogToFile.set_volatile_value(true)` guarded by
  `!has_value()` for a code-path-specific fix) and re-check — absence of `OptiScaler.log`
  is not evidence of anything on its own.

- **A conflict-free `git merge` auto-merge is not proof the result is semantically
  correct** — git not flagging a conflict only means the two sides' diffs didn't
  overlap on the exact same lines, not that the combined result makes sense. Two
  concrete failure shapes hit during a real upstream sync in this repo: (1) two
  independent commits each inserting their own `bool x = false;` a couple of lines
  apart from each other (not on the same line) auto-merged into a duplicate
  declaration that would not compile; (2) one side's function called a real API a
  second time at its `return` statement (its own, individually-correct pattern before
  the merge), while the other side's clean, non-conflicting hunk added a
  `result = TheSameApiCall(...)` capture a few lines earlier — after merging, both
  hunks were individually valid but the real API ended up called twice per invocation,
  with the earlier `result` (and the bookkeeping gated on it) silently discarded.
  Neither was flagged by git; both were only caught by reading every hunk adjacent to
  a real conflict, not just the conflicted lines themselves, and by building the
  merged result before declaring the sync done — see `INFRA_SYNC_UPSTREAM`
  (`core.infra.sk`).

- **`ResidualAcrossRR` (MV-reprojected residual-carry-across-RR) was built, shipped, then
  fully retired** — it solved a real problem (a naive pre-RR NR edit is dominated by
  `-n_t`, that frame's un-accumulated ray-trace noise, because RR's denoiser can't tell a
  deliberate edit from noise it's trained to remove), but in-game A/B testing showed plain
  post-RR NR placement looked more detailed than the Carry approach for the same scene,
  with none of Carry's own grain/artifact risk (private DLSS-SR jitter handling,
  reset-latch behavior, blend-rate tuning). Retired on image-quality-vs-complexity
  grounds, not because the mechanism was broken. As of 2026-09-14
  (`feat/dlssnr-postrr-simplify-v2`), RR unconditionally forces post-SR NR placement —
  don't re-propose reviving the Carry approach without first confirming the current
  unconditional-post-RR baseline has actually regressed.

- **When a resolve computes `answer - proxy` (or any two-buffer edit/difference), both
  buffers must be enlarged/resampled the same way before the subtraction, not just one.**
  DLSS-NR's SGSR1 enlarge (`feat/dlssnr-sgsr1-upscale`) first enlarged only the model's
  *answer* below 100% model resolution and compared it against the untouched native
  original -- mathematically wrong, because the model's proxy input never saw native
  detail either (unlike the `workScale > 1.0` case, where the proxy really was resampled
  from native). Comparing a sharp enlarged answer against a native original made the
  native detail nearly cancel out of the composited result algebraically, reported in-game
  as "the low-res image got combined with the final image." Fix required a second
  native-resolution buffer and a second enlarge dispatch for the proxy, so both sides of
  the subtraction share the same detail basis. Generalizes beyond this one feature: any
  edit-based resolve/compositing pass that resamples one side of a difference must resample
  the other side identically, or the difference stops measuring what it's supposed to.

- **A shader pass class built for one `Dispatch()` call per frame (one non-double-buffered
  constant buffer, a descriptor-heap ping-pong meant to alternate across frames) breaks
  silently if reused for two same-frame calls** -- the second call's CPU-side constant
  write lands before the GPU executes either dispatch, so both draws can end up using the
  same (wrong) constants. Hit when DLSS-NR's SGSR1 pass (`SGSR1_Dx12`, mirroring the
  existing `OS_Dx12`) was dispatched twice per frame (once for the answer, once for the
  proxy) through a single instance -- masked for a while because both calls happened to
  share identical source/destination dimensions that session, not guaranteed in general.
  Fix: one instance per same-frame call site (see `superUp`/`superDown`'s existing
  precedent of two separate `OS_Dx12` instances for the two supersample legs), never one
  instance reused within a frame.

- **`SoftKnee`'s per-channel peak-headroom clamp (`dlssnr.hlsl`, the `if (peak > 1.0)
  display /= peak;` step) cannot be inverted exactly, even in principle** -- dividing by a
  peak of 2 and dividing by a peak of 3 both land on `peak_final == 1`, so the final value
  alone can't say which one to undo. `SoftKneeDecode` (added for the downsample's
  linear-light averaging fix) inverts only the luminance roll-off above it, which is
  exactly invertible in closed form, and leaves the peak-clamp un-reconstructed -- the same
  "approximately" the resolve's own matched-residual reconstruction already accepted for
  SoftKnee before this. Neutwo/Hybrid don't have this limitation (their decode is exact)
  because neither has a lossy clamp step.

- **A `DlssNrConstants` field a shader dispatch doesn't explicitly set defaults to zero,
  and that's only harmless until the shader starts reading it.** The C++ dispatch site for
  `DlssNrMode_Downsample` never set `.Passthrough`/`.ReversibleMode` on its constants --
  fine while the box-average shader ignored both fields, but would have silently applied
  the wrong reversible-curve decode once the shader started branching on them (the
  linear-light averaging fix). Before adding new cbuffer-field-dependent logic to an
  existing `DlssNrMode`, check every C++ call site actually sets the fields the shader is
  about to start reading, not just the one being actively edited.

- **"Matched residual" (`DlssNrTransfer` == 1) is now a no-op whenever the enlarge stage
  ahead of the resolve succeeds** (SGSR1 below 100%, or `superDown` above it): its gate is
  `gTransfer == 1 && modelRanSmall`, and `modelRanSmall` checks whether `resolveProxy`'s
  actual bound resource is still smaller than native -- which it no longer is once the
  enlarge succeeds and hands the resolve a native-resolution `proxyNative`/`colorCopy`. The
  reconstruction only still fires in the enlarge-failure fallback. The menu control and its
  tooltip ("Matched residual can reduce blur and colour shifts") still present it as a live
  choice on every build; it was written for a world where the resolve always received small
  buffers below 100% (before SGSR1 existed to enlarge both sides pre-resolve). Not yet
  changed in the UI -- flagged, not fixed.
