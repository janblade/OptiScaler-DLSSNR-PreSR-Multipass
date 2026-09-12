# Plan: Clamp each DLSS-NR pass's raw output before it becomes the next pass's input
- Branch: feat/dlssnr-presr-residual-across-rr
- Created: 2026-09-12
- Status: in-progress (4/5 steps done, independently reviewed with one finding fixed; step 4
  still needs an in-game check, see below)

## Review Pass (core.dev-loop.sk)
Independent reviewer subagent dispatched (Path A — this host supports subagent dispatch) with
the diff and this plan's Context/Acceptance Criteria/Out-of-scope as the spec, not my own
implementation reasoning. One finding, fixed immediately (verified independently before fixing,
not taken on the reviewer's word alone):

- **Worth fixing (fixed):** the new `DlssNrMode_ClampProxy` shader branch
  (`dlssnr.hlsl:539-547`) read the model's raw output and `saturate()`d it directly, without the
  `SanitizeFinite3` NaN/Inf guard every other consumer of raw model output in this same file
  applies (lines 506, 522, and the `saturate(SanitizeFinite(...))` precedent at line 136) —
  exactly the "don't trust the model's raw answer" principle this whole fix exists to enforce.
  `saturate(NaN)` is not guaranteed safe across all D3D12 driver/compiler combinations. Fixed to
  `saturate(SanitizeFinite3(raw.rgb, 0.5))`, matching the line-136 pattern; both shader blobs
  recompiled, Debug|x64 and Release|x64 both rebuilt clean.
- No other findings — correctness against spec, `passScratch`-lifetime convention adherence,
  resource-state transitions, and the failed-allocation/stale-feature interaction were all
  checked and confirmed sound. Ownership fit (new resource + lambda generalization) judged
  proportionate, not over-engineered.
- Priority: 1 (ahead of `2026-09-12-dlssnr-multipass-resolution-rebuild-ramp.md` — see that
  plan's header note; this one matches the reported symptom without needing DRS/a resolution
  event, so it runs first)
- Task file: memory/tasks/dlss-neural-rendering.md

## Context

Independent review (this session) of the DLSS-NR multi-pass hand-off (`Passes > 1`) found a
concrete, verified gap distinct from the resolution-ramp finding in the sibling plan:

- The one-time encode step (`OptiScaler/shaders/dlssnr/precompile/dlssnr.hlsl:728-775`)
  guarantees the model's input is gamma-encoded and inside `[0,1]` per channel — a soft knee
  rolls off anything above 0.75, and the final write always goes through `LinearToSrgb`, which
  hard-`saturate`s (`dlssnr.hlsl:66`). The encode comment states why: "unstable input is
  unstable output... this is where a bright scene would produce it."
- The multi-pass loop (`OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp:2751-2813`) feeds pass N's
  raw `g_nr.evaluate()` output directly to pass N+1 as `passInput`, with only a
  `NON_PIXEL_SHADER_RESOURCE` barrier (`MakeModelReadable`, ~2757-2766) — no re-clamp, no
  re-knee.
- This is not a hypothetical gap. The codebase's own resolve-stage comment
  (`dlssnr.hlsl:1024-1033`) documents a **real, previously-shipped bug** from exactly this
  failure class: "whatever the model returned was handed back unbounded" caused visible
  flicker in Nioh 3, fixed there by adding an explicit two-sided ratio guard (`gMaxRatio`,
  `dlssnr.hlsl:1042-1043`). A second, independent admission of the same underlying fact sits
  in the residual-carrier code: "DLSS can ring outside the carrier's `[0,1]` range"
  (`dlssnr.hlsl:523`, `dlssnr_residual.hlsl:215`).
- That guard exists **only** in the single, once-per-frame final resolve (comparing the last
  pass's output against the *original* encoded proxy, `resolveProxy = modelInput` at
  `DlssNr_Dx12.cpp:2995`). Nothing bounds an intermediate pass's raw answer before it becomes
  the next pass's "encoded proxy."

**Why this matches the reported symptom better than the resolution-ramp finding:** it requires
no DRS, no settings change, no discrete event — it is live on every frame, at any fixed
resolution, whenever `Passes > 1`. The model's raw output is least constrained exactly where
content is hardest to predict (motion, disocclusion, fast-changing detail), so an out-of-range
value there, fed forward as if it were a normal well-formed proxy, reads as instability
("boiling") concentrated on moving content — worse with more passes, because there are more
unguarded hand-offs.

### Why a plain `saturate()` is the right fix, not a re-run of the encode curve
All three encode variants (`SoftKnee`/`HybridEncode`/`NeutwoEncode`) are followed
unconditionally by `LinearToSrgb(display)`, which itself hard-saturates
(`dlssnr.hlsl:66`, confirmed applying regardless of `gReversibleMode`) — so "channel values in
`[0,1]`" is the one invariant every encode path guarantees, independent of which curve was
used. Pass N's output already lives in that same encoded domain (its input did), so restoring
the invariant only needs a channel clamp, not a second pass through the knee/Neutwo/Hybrid
curves (which assume a *linear HDR* input, not an already-encoded one — re-running them on an
encoded value would double-transform it).

### Why this needs a new scratch resource, not an in-place clamp
`DispatchPass` (`DlssNr_Dx12.cpp:1631-1686`) always builds an SRV over `InSource` and a UAV over
`OutTarget`; a resource can only be in one D3D12 state at a time, so `InSource` and `OutTarget`
must be physically different resources when one is being read while the other is written. The
existing 2-buffer ping-pong (`g_nr.output`/`g_nr.passScratch`) has no spare slot for this, so a
third, small scratch resource is needed purely as the clamp step's landing spot.

## Out of scope
- The DRS/resolution-rebuild-ramp finding — separate plan
  (`2026-09-12-dlssnr-multipass-resolution-rebuild-ramp.md`), separate mechanism, deprioritized
  behind this one per the user's request.
- Re-deriving or second-guessing the model's internal behavior — this plan only restores an
  invariant this codebase's own encode step already establishes and already relies on
  elsewhere (the resolve guard); it does not attempt to characterize *why* the model rings.
- The single-pass (`Passes=1`) path — no pass-to-pass hand-off exists there, so nothing to
  clamp; this plan must not change its behavior at all.
- The Vulkan/DX11 bridges — D3D12 path only, matching how the multi-pass feature itself is
  currently scoped.

## Acceptance criteria
- [x] Every intermediate pass boundary (i.e. whenever `pass + 1 < effectivePasses`) clamps
  that pass's raw output into `[0,1]` per channel before it is used as the next pass's input.
- [x] `Passes = 1` is byte-for-byte unchanged (no intermediate boundary exists).
- [x] The final pass's output is untouched by this change — the existing once-per-frame
  resolve guard (`gMaxRatio`) still runs exactly as before, comparing the last pass's *raw,
  unclamped* answer against `modelInput`, so this plan does not alter the final resolve's
  own inputs.
- [x] Debug|x64 and Release|x64 both build clean, no new errors or warnings.
- [ ] Manual/log-based visual check disclosed rather than skipped (no automated shader-output
  test harness exists for `dlssnr.hlsl`, unlike the residual shader's
  `tests/nr_residual_rr_smoke.cpp`) — see Step 4. **Needs the user to check in-game; not
  something this session can exercise without a running title.**

## Steps

1. [x] **Add a clamp mode to `dlssnr.hlsl`.** Add a new `DlssNrMode` entry (next free value
   after `DlssNrMode_ZeroMotion = 11`, i.e. `DlssNrMode_ClampProxy = 12`) to
   `OptiScaler/shaders/dlssnr/DlssNr_Common.h`'s enum, with a one-line comment: "a pass's raw
   answer -> the same value saturated back into the proxy's valid range, before it becomes the
   next pass's input." In `dlssnr.hlsl`, add the corresponding branch:
   `gTarget[id.xy] = float4(saturate(gSource.Load(int3(id.xy,0)).rgb), gSource.Load(int3(id.xy,0)).a);`
   (alpha passed through unclamped, matching how the encode step treats alpha elsewhere).
   Recompile the DX12 blob with `fxc.exe -T cs_5_0 -E CSMain -O3` (legacy DXBC/SHEX — matching
   this session's established requirement so the WARP D3D11 smoke test can still load it) and
   the Vulkan blob with `dxc.exe -spirv -T cs_6_0 -E CSMain -O3 -Qstrip_debug -D VK_MODE -Cc -Vi`.
   — verify: both blobs regenerate without error; `git diff --stat` shows only the two
   `precompile/*.cso`/SPIR-V blobs and the two source files touched.
   — done: regenerated with the repo's own `fxc.exe`/`dxc.exe` in
   `OptiScaler/shaders/shader_tools/` using this step's exact flags, headers rebuilt with
   `create_header.py` using the array names the source actually expects (`DlssNr_cso`,
   `dlssnr_spv` — grepped from `DlssNr_Dx12.cpp`/`DlssNr_Vk.cpp` first, since the two blobs don't
   share one casing convention). `git diff --stat` touches exactly `DlssNr_Common.h`,
   `dlssnr.hlsl`, `DlssNr_Shader.cso`, `DlssNr_Shader.h`, `DlssNr_Shader_Vk.spv`,
   `DlssNr_Shader_Vk.h` (the last four are checked-in generated files, not build outputs — no
   build step regenerates them, confirmed via the `.vcxproj`).

2. [x] **Add the clamp scratch resource.** In the `NrState` struct
   (`DlssNr_Dx12.cpp` ~230-238, next to `passScratch`), add
   `ID3D12Resource* passClampScratch = nullptr; bool passClampScratchFailed = false;`. Create it
   alongside `g_nr.passScratch` (same `CreateScratch(device, desc.Format, workWidth, workHeight)`
   call shape, same conditional — only needed when `requestedPasses > 1`), and add it to every
   site that parks/nulls `g_nr.passScratch` today (the `resolutionChanged`/`placementChanged`
   teardown at ~1998-2016, the `requestedPasses == 1` release at ~2074-2079, `Shutdown()`/
   `ReleaseSurfacesIfFormatChanged`'s resource-parking list at ~909-914) so its lifetime exactly
   tracks `passScratch`'s.
   — verify: grep confirms every `ParkNrResource(g_nr.passScratch)` /
   `g_nr.passScratch->Release()` site has a matching line for `g_nr.passClampScratch`; build
   Debug|x64 clean.
   — done: also gated the two places that decide whether extra passes may run at all
   (the feature-build loop and `effectivePasses`'s count) on `g_nr.passClampScratch != nullptr`
   in addition to `g_nr.passScratch != nullptr` — an allocation failure for the clamp target
   must disable extra passes exactly like a `passScratch` failure already does, since without it
   there is nowhere to land an intermediate pass's clamped answer.

3. [x] **Wire the clamp into the pass loop.** In the loop at `DlssNr_Dx12.cpp:2779-2813`,
   immediately after `MakeModelReadable(finalAnswer);` and only inside the existing
   `if (pass + 1 < effectivePasses)` branch (i.e. only when there is a next pass to feed):
   transition `g_nr.passClampScratch` to `UNORDERED_ACCESS`, dispatch
   `DispatchPass(cmdList, clampParams, finalAnswer, nullptr, nullptr, nullptr, nullptr, g_nr.passClampScratch, nullptr)`
   with `clampParams.Mode = DlssNrMode_ClampProxy`, `clampParams.Width = workWidth`,
   `clampParams.Height = workHeight`, then transition `g_nr.passClampScratch` to
   `NON_PIXEL_SHADER_RESOURCE` and set `passInput = g_nr.passClampScratch;` (replacing the
   current `passInput = finalAnswer;`). Leave the existing `passOutput` ping-pong selection
   (`passOutput = passOutput == g_nr.output ? g_nr.passScratch : g_nr.output;`) untouched — it
   still alternates between the two original buffers; the clamp buffer is a pass-through
   landing spot only, never a ping-pong destination.
   — verify: build Debug|x64 clean; read through the changed loop and confirm `passOutput`'s
   alternation sequence for `Passes=3` is still exactly `g_nr.output -> g_nr.passScratch ->
   g_nr.output`, unaffected by the new clamp step; confirm the final pass
   (`pass + 1 == effectivePasses`) never touches `passClampScratch` and `finalAnswer` for the
   resolve is still the raw, unclamped last-pass output.
   — done: one deliberate deviation from the literal step text, in the plan's own spirit —
   instead of hand-written `Barrier` calls, `passClampScratch` was folded into the existing
   `MakeModelReadable`/`MakeModelWritable` idempotent-barrier lambdas (generalized from a
   two-way `output`/`passScratch` ternary to a three-way `ReadableFlag` helper). This matters
   because with `Passes=3` the clamp target is written and read twice in one frame (once per
   intermediate boundary), the same write-then-read-then-write-again shape `output`/`passScratch`
   already have — reusing the proven idempotent pattern avoids a hand-rolled state machine that
   could double-transition. The added `LOG_WARN` on a failed clamp dispatch (called for by Step 4)
   was implemented here since it's part of the same call site. `finalAnswer` — the value fed into
   the clamp dispatch as `InSource` — is already `NON_PIXEL_SHADER_RESOURCE` at this point (the
   preceding `MakeModelReadable(finalAnswer)` guarantees it), matching `DispatchPass`'s SRV
   expectation.

4. [ ] **Manual/log verification.** No automated pixel-level test harness exists for
   `dlssnr.hlsl` (unlike `dlssnr_residual.hlsl`'s `tests/nr_residual_rr_smoke.cpp`) — disclosed
   here rather than skipped, per this repo's existing practice for this file
   (see `2026-09-06-fix-dlssnr-multipass-review-findings.md`'s own disclosure for a UI-facing
   change in the same area). With `Passes=2` or `3` in a real game, on a scene with moving
   content, compare before/after this change: does the frame-to-frame instability on moving
   objects visibly reduce? Also confirm in `OptiScaler.log` that no new errors appear from the
   added `DispatchPass` call (e.g. a failed CBV/descriptor allocation would return `false`
   silently per `DispatchPass`'s existing contract — add a one-time `LOG_WARN` if it returns
   `false`, matching this file's existing pattern for other optional dispatches, so a failure
   is visible rather than silently leaving `passInput` stale).
   — verify: `OptiScaler.log` reviewed; visual comparison result reported back, since this is
   exactly the kind of quality claim `core.self-healing.sk`'s Verification-Before-Completion
   Protocol requires exercising with real input rather than just building.
   — status: NOT DONE. This needs a running game with `Passes=2` or `3` and moving content,
   which this session cannot exercise. Everything else in this plan is complete and both
   configurations build clean; this step is the one open item before calling the plan `done`.

5. [x] **Full verification pass.** Debug|x64 and Release|x64 both build clean. Re-read the diff
   end-to-end confirming: `Passes=1` never allocates `passClampScratch` and never enters the
   changed branch; the resolve stage's inputs (`resolveProxy`, `resolveAnswer` at
   `DlssNr_Dx12.cpp:3052-3053` — shifted from this plan's original estimate by the lines this
   plan itself added) are unchanged — still the original `modelInput` and the final pass's raw
   `finalAnswer`; no other call site was affected.
   — done: both configurations built via MSBuild directly (Debug|x64 and Release|x64), no new
   warnings or errors from `DlssNr_Dx12.cpp`, `DlssNr_Common.h`, or the shader. Full diff
   re-read end-to-end; confirmed `requestedPasses == 1` parks/never (re)allocates
   `passClampScratch`, and `effectivePasses` cannot exceed 1 without it, so the pass loop's
   `pass + 1 < effectivePasses` branch is unreachable in that configuration. `grep` confirmed
   `passScratch`/`passClampScratch` references are confined to `DlssNr_Dx12.cpp` (no Vulkan/DX11
   bridge touched, matching this plan's scope).
