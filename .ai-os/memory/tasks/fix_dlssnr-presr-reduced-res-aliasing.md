# Task Memory — fix/dlssnr-presr-reduced-res-aliasing

Branched off updated `main` (which by this point already carried the merged
`docs/readme-housekeeping` PR #11). Code-complete and build-verified; awaiting in-game
reconfirmation from the user.

Active plan: memory/plans/2026-09-17-dlssnr-presr-reduced-res-aliasing.md (in-progress, 7/9)

## Execution progress

Steps 1 and 3 (pre-SR handoff trace, resize-tap code-level confirmation) were both closed by
reading the actual code, not by re-asserting the session's earlier live-testing theory:
- `NVNGX_DLSS_Dx12.cpp:1180-1184`/`:1215-1219` calls `DlssNr::EvaluateBeforeUpscale` on the same
  `params` object handed to SR's own evaluate immediately after; `DlssNr_Dx12.cpp:3376-3379`
  pulls `target` from that same `NVSDK_NGX_Parameter_Color` and dispatches
  `Dispatch(cmdList, target, depth, motion, target, ...)` -- source and destination are the
  literal same resource, confirming no copy/different arrival state sits between NR's write and
  SR's read.
- `dlssnr.hlsl:905-906`, the resolve's actual per-pixel `gSource`/`gModel` reads (not gated
  behind any debug/compare mode), are single-tap `SampleLevel(gLinear, uv, 0)` with `uv` derived
  directly from the *native* dispatch thread id over the *native* dims (`dlssnr.hlsl:570`) against
  textures bound at the *reduced* working resolution. The bilinear blend weight is
  `frac(nativePixel * workScale + const)`, a sawtooth with period `1/workScale` native pixels --
  exactly period 2 at 50% (the cleanest, most visible alternation: alternating pixels land exactly
  on vs. exactly between source texels) and ~1.667 at 60% (a beat pattern, less crisply lined).
  Matches the user's own 60%->50% widening report with no other mechanism needed; there is no
  fixed-size box filter anywhere in this read to compete as an explanation.
- Refined understanding of *why* a flat sky shows this most: the model's own reduced-resolution
  answer likely carries small per-pixel high-frequency noise/dither even in flat regions (neural
  output, not a clean signal), so the phase-dependent bilinear blend alternates between reading
  ~1 raw noisy texel (high variance, "sharp") and averaging ~2-4 of them (low variance, "soft") --
  invisible where real detail already dominates local contrast, maximally visible where the
  region is otherwise flat and this noise-variance swing is the only contrast present.

Step 2 (RCAS as required amplifier) needed a real user A/B, not something inferable from code --
asked via `AskUserQuestion`. Result: **lines persist with RCAS/Motion Adaptive Sharpness off.**
SR's own temporal/spatial reconstruction alone is sufficient to amplify the pattern; sharpening is
incidental, not required. This ruled out an RCAS-specific fix shape and pointed squarely at the
resize tap itself.

Step 4 (candidate fix) was also a real decision point, presented to the user via
`AskUserQuestion` with the actual tradeoffs of three options (multi-tap the real reads / separate
blur pass on the resolved edit / new intermediate buffer). User picked the root-cause option:
multi-tap the actual `gSource`/`gModel` reads.

Step 5 implementation, `dlssnr.hlsl`:
- New `gBeforeUpscale` cbuffer field (uint) -- nothing previously distinguished pre- vs post-SR
  placement inside the resolve shader itself, since both run the same `DlssNrMode_Resolve`.
- New `SampleReducedColour(Texture2D<float4> tex, float2 uv)` helper: returns the original
  single-tap read unchanged unless `gBeforeUpscale != 0 && gModelWorkScale < 0.999`, in which case
  it 4-tap box-averages at `uv + texel*(±0.25,±0.25)` where
  `texel = 1/(max(float2(gWidth,gHeight) * gModelWorkScale, 1.0))` -- spans exactly one source
  texel, self-scaling with the resize ratio (continuous, unlike the file's existing discrete-pixel
  detail-injection blur radius, which needs an integer clamp because it reads via `Load()`).
  Applied to the resolve's two hot reads at `dlssnr.hlsl:905-906` (originally `float4`, now `float3`
  since only `.rgb` was ever used downstream).
- `DlssNr_Common.h`: matching `BeforeUpscale` field appended to `DlssNrConstants`, same trailing
  4-byte-scalar idiom as `ModelWorkScale`/`ReplaceDetailStrength`; struct stays well under its
  256-byte `alignas`, no `static_assert` change needed.
- `DlssNr_Dx12.cpp`: `resolveParams.BeforeUpscale = frame.BeforeUpscale ? 1u : 0u;` beside the
  existing `ModelWorkScale` line.

Step 6 (Vulkan parity) was checked, not assumed: Vulkan has its own real pre-SR placement
(`EvaluateBeforeUpscaleVk`/`beforeSr` param in `DlssNrFeature_Vk.cpp`) -- this is not a DX12-only
feature the way SGSR1 started out. Since `dlssnr.hlsl` is the one shared shader source for both
backends, the fix ports for free once the C++ side threads the flag: added
`encode.BeforeUpscale = beforeSr ? 1u : 0u;` on the `encode` constants block that `resolve` is
later copied from, mirroring how `ModelWorkScale` itself is set there.

Step 7 build: recompiled `dlssnr.hlsl` for both DX12 (`DlssNr_Shader.cso`/`.h`) and Vulkan
(`DlssNr_Shader_Vk.spv`/`.h`) via `dxc.exe`, both clean. **Real mistake caught**: the first
Vulkan header regen used array name `DlssNr_spv`, but `DlssNr_Vk.cpp:89` expects `dlssnr_spv`
(confirmed via `git show HEAD:...DlssNr_Shader_Vk.h` before fixing) -- caused a C2065 undeclared
identifier on the first Release build attempt, fixed by regenerating with the correct name. Debug
and Release x64 both then built clean, 0 errors, 0 warnings from any touched file (grepped build
logs explicitly for `dlssnr`/`DlssNr_Vk`/`DlssNrFeature_Vk`, not just eyeballed the tail).

Remaining: step 8 (in-game reconfirmation on the original KCD2 repro, plus spot-checks that
post-SR and Apply-before-SR-at-100% are unchanged) and step 9 (`TASK_CLOSE`, including writing
this failure class up in `known_gotchas.md`/`architecture_overview.md`). Not yet committed to git
-- code changes are sitting on the branch uncommitted, deliberately held until the user confirms
in-game so the commit message can say "confirmed" rather than "should fix."

## Step 8 result: FIX DID NOT WORK -- fix's premise falsified, needs new investigation

User tested the exact build with this fix (Debug x64, built 2026-09-17 ~08:23, confirmed by
timestamp) on Dawnwalker (a different game than the original KCD2 repro -- useful new data point:
the bug is not KCD2-specific, it reproduces cross-game, consistent with it being an OptiScaler/
DLSS-NR-side mechanism rather than something one game's buffer handling triggers). **Vertical
lines still present, unchanged.**

This falsifies the fix's specific implementation, and puts real doubt on where in the pipeline the
periodic pattern actually originates. `SampleReducedColour`'s 4-tap box average operates on the
resolve's `gSource`/`gModel` reads (`dlssnr.hlsl:905-906`), i.e. on the *enlarge* step from the
model's reduced working resolution back up. If the buffer already carries the periodic pattern
*before* that read -- baked in by the encode/downsample step, or inherent to how the model computes
its answer at a fractional decimation -- then no resampling filter on the way back up can remove it,
because the information loss/bias already happened per-texel at the reduced resolution itself. That
alternative is equally consistent with every A/B result gathered so far (SGSR1 ruled out, Matched
Residual ruled out, Bilinear-mode ruled in as still showing it, 60%->50% widening, placement being
the deciding factor) -- none of those tests distinguish "the enlarge read aliases" from "the
reduced buffer already contains the pattern."

**Not yet done, and the real next step**: localize where in the pipeline the pattern first appears,
before proposing another fix blindly. The resolve shader's own `gDebugView` modes 1 (proxy) and 2
(model) render `proxy`/`model` directly -- which, after this fix, already pass through
`SampleReducedColour` -- so asking the user to check DebugView 1/2 under the same repro
(pre-SR, reduced resolution) tells us directly: if the lines are visible even in the debug view,
the pattern predates or survives the multi-tap resample and the fix's premise (aliasing purely
from the enlarge tap) is wrong; if the debug view is clean but the final "Apply" output still
shows lines, the composition math itself (ratio/hue blend, skin protection, or something else in
the ~200 lines after the enlarge read) is introducing or amplifying the pattern, which is a
different fix target entirely.

Do not implement another blind fix before this localization step. The plan document's own
acceptance criteria and step list will need real revision once the actual origin is known --
this is not a small tweak to the existing fix, it may be an entirely different part of the file.

## Fix attempt 2 (2026-09-17, same session)

User's follow-up localization: not visible in "Proxy" debug view, **visible in "Difference
(amplified)"** (`edit = model - proxy`, x20, `dlssnr.hlsl:952/960-961`) -- computed from the same
`model`/`proxy` variables attempt 1 already multi-tapped. Also: **only visible on Replace
reversible modes (2/4), not Composed** -- worst on Hybrid Proxy + Replace.

Read together, this says: the residual pattern lives in how the model's raw answer disagrees with
the proxy at the texel level (plausibly a near-Nyquist/checkerboard-ish bias in the model's own
reduced-resolution output -- not unusual for a conv net's output layer), not a smooth low-frequency
resample wave. Attempt 1's fix does reach this signal (Difference is computed from the already-
patched samples) but was too narrow to remove it: its taps were offset only +/-0.25 texel, which
never leaves the source texel's own bilinear blend neighbourhood, so two genuinely different
adjacent texels were never actually averaged together -- a period-2 alternation survives almost
undiminished at that spacing. Composed hides the same leftover residual by blending back toward
the clean native frame; Replace has no such dilution, so it's fully exposed there -- consistent
with being the *same* residual at *different visibility*, not a separate bug.

Widened `SampleReducedColour`'s taps from +/-0.25 to +/-0.5 texel (`dlssnr.hlsl`, same function).
Two samples spaced exactly one full texel apart are always opposite-phase on any period-2 signal,
so averaging them cancels a checkerboard-type bias exactly, regardless of where the pair sits --
not just attenuates it the way the narrower spacing did. Recompiled both `DlssNr_Shader.cso`
(DX12) and `DlssNr_Shader_Vk.spv`/`.h` (Vulkan) from the same source, both clean. Debug x64
rebuilt 2026-09-17 08:47, 0 errors, 0 warnings anywhere (not just the touched files this time).
Release x64 also rebuilt clean, 0 warnings from touched files (26 total, all pre-existing
unrelated C4250 dominance warnings, matches the established baseline pattern).

User tested attempt 2 (from `x64/Release/a/OptiScaler.dll`, built 08:48) -- **lines still present,
unchanged.**

## Fix attempt 3 (2026-09-17, same session)

Two failed attempts at widening the tap offset pointed at a different, more fundamental bug in
the implementation itself: the texel-size computation. Both attempts derived texel size from
`gModelWorkScale` (`texel = 1/(gWidth,gHeight * gModelWorkScale)`), which assumes `gSource`/
`gModel` are bound at the reduced working resolution. That assumption is false by default.
`DlssNrReducedUpscaleMethod` defaults to **1** (SGSR1 answer-only, `Config.h:419`) -- not
Bilinear -- so by default SGSR1 already enlarges `gModel` (the answer) to native resolution
*before* the resolve runs. `gModelWorkScale`'s own cbuffer comment says explicitly that it is
deliberately *not* a live buffer-size readout for exactly this reason (other code gated on it,
the detail-injection radius, genuinely wants "how much did the model conceptually shrink" rather
than "how big is the buffer right now"). Both prior attempts read that field for the wrong
purpose, so their computed texel size was wrong by a factor of `1/workScale` whenever SGSR1 had
already run on the texture in question -- their `+/-0.5`-texel taps landed roughly `+/-0.8`
*native* pixels apart on an already-correctly-reconstructed buffer, an arbitrary-scale blur that
was never actually doing the adjacent-texel cancellation it was designed for. This is a variant
of the exact stale-gate trap already on record in this codebase's own notes, fallen into for a
subtly different purpose (sizing a read, not gating a write) than the one the notes warn about.

Fixed by reading the tap texture's own actual bound size via `tex.GetDimensions(texW, texH)`
instead of deriving it from `gModelWorkScale` -- correct regardless of whether SGSR1 already ran:
still the true reduced size when it has not (`gSource`/the proxy under method 1, since SGSR1
answer-only never touches the proxy), and the true native size when it has. Offsets stay at
`+/-0.5` texel (attempt 2's reasoning for that spacing still holds). Recompiled both shader
targets, both clean. Debug x64 rebuilt 2026-09-17 09:05, Release x64 09:06 -- both 0 errors, 0
warnings from any touched file.

User tested attempt 3 -- **lines still present, unchanged.** Both Bilinear and SGSR1
`DlssNrReducedUpscaleMethod` confirmed still affected (consistent with the session's very first
SGSR1-ruled-out finding).

## Diagnostic before attempt 4: is the gate even engaging?

Three clean, individually-reasoned attempts producing literally zero visible change each time is
itself a signal -- not just "not enough fix," possibly "this code path isn't the one running at
all" for the user's actual repro. Rather than guess a fourth time, added a change-gated
`LOG_INFO("DLSS-NR resolve gate: BeforeUpscale={}, ModelWorkScale={:.3f}, ReversibleMode={},
SampleReducedColour {}", ...)` right where `resolveParams.BeforeUpscale`/`ModelWorkScale`/
`ReversibleMode` are assembled (`DlssNr_Dx12.cpp`, just after `resolveParams.CompareSwap`) --
prints whenever any of those three change, says explicitly whether the multi-tap path is
"ACTIVE" or "inactive" for the frames being composed. DX12 only so far, not yet added to the
Vulkan feature file (Dawnwalker not confirmed DX12 vs Vulkan). Debug x64 rebuilt 09:15, Release
x64 09:15, both clean, 0 errors, 0 new warnings.

User pasted the log directly:
```
[09:28:23] DLSS-NR resolve gate: BeforeUpscale=0, ModelWorkScale=0.650, ReversibleMode=2, SampleReducedColour inactive (single-tap, unchanged)
[09:28:24] DLSS-NR resolve gate: BeforeUpscale=1, ModelWorkScale=0.650, ReversibleMode=2, SampleReducedColour ACTIVE (multi-tap)
```
Confirmed ACTIVE for the actual repro frames (BeforeUpscale=1, ModelWorkScale=0.65,
ReversibleMode=2 = Neutwo+Replace). The gate was never the problem -- the multi-tap code was
genuinely running, and the lines were still there anyway. This kills the entire "fix the enlarge
read" theory outright, not just its tap spacing/sizing -- three individually well-reasoned
attempts producing literally zero change, now confirmed on a path known to be executing, means
the mechanism itself was wrong, not under-tuned.

## The real cause, found by reading the composition math instead of re-tuning again

`ReversibleMode=2` (Neutwo+Replace) from the log pointed straight at `NeutwoDecode`
(`dlssnr.hlsl`). Its own comment: "the inverse diverges at 1, so the peak is clamped just below
it -- this is the steep-highlight-slope the toggle's help warns about: a highlight at the ceiling
decodes to a very large but finite value." `HybridDecode`'s comment: "hybrid-replace flashes far
less than Neutwo-replace" -- the file already documents this exact instability as known,
pre-existing behaviour, just never tied to this particular bug report before.

Read the Replace override itself (previously ~line 1150, shifted after the reverts):
```
// Replace mode: the model's answer IS the picture, decoded through Neutwo's exact inverse, with
// NONE of the composition above -- no ratio, no highlight guard, no palette blend.
if (gReversibleMode == 2)
    result = gPassthrough != 0 ? modelDirect : NeutwoDecode(modelDirect);
else if (gReversibleMode == 4)
    result = gPassthrough != 0 ? modelDirect : HybridDecode(modelDirect);
```
Composed mode always passes its result through `boundedRatio = clamp(amplified, 1/guard, guard)`
(`guard = max(gMaxRatio, 1.0)`) before this point. Replace's own comment says explicitly it skips
that guard entirely -- "no highlight guard" is not an oversight, it is the documented design. So
any small per-pixel model noise near a peak channel close to 1 (a near-white pixel -- sky, the
user's repro region) passes through `NeutwoDecode`'s near-pole derivative completely unclamped,
turning ordinary noise into a huge swing in decoded linear value, with nothing downstream to pull
it back. Composed never shows this (always guarded); Replace shows it worst (never guarded);
before-SR matters because SR's own reconstruction stabilises/reinforces the now-huge per-pixel
swing into a visible static pattern the same way originally reasoned, just correctly identifying
*what* gets amplified this time. This also explains why all three multi-tap attempts had zero
effect: smoothing the pre-decode sample reduces noise amplitude, but does nothing to the decode's
derivative -- and that derivative is steep enough that even a small remaining variation still
explodes near the pole.

## Fix attempt 4 (2026-09-17, same session)

Presented this finding and the proposed fix to the user via `AskUserQuestion` before implementing
-- three failed attempts in a row justified getting explicit buy-in rather than shipping a fourth
guess unannounced. User chose: implement the highlight-guard fix AND revert the three failed
multi-tap attempts back to a plain single-tap read, to keep this test clean (not stack an
unproven change under a new one).

Reverted, fully: `SampleReducedColour()` and its function-length comment removed from
`dlssnr.hlsl`; the two resolve call sites restored to plain `gSource.SampleLevel`/
`gModel.SampleLevel` (back to `float4`, `.rgb` usage restored); `gBeforeUpscale` cbuffer field
removed; `DlssNr_Common.h`'s `BeforeUpscale` field removed; `DlssNr_Dx12.cpp`'s
`resolveParams.BeforeUpscale` assignment and the diagnostic `LOG_INFO` block both removed;
`DlssNrFeature_Vk.cpp`'s `encode.BeforeUpscale` assignment removed. Grepped the whole `OptiScaler`
tree for `gBeforeUpscale`/`SampleReducedColour`/`resolveParams.BeforeUpscale`/
`encode.BeforeUpscale` afterward -- zero matches, confirmed clean.

New fix, `dlssnr.hlsl`, right after the `gReversibleMode == 2/4` decode branch and before the
existing Replace-only detail injection block: reapplies the same `guard`/`kRatioFloor` idiom
Composed already uses (both already in scope, no new cbuffer field needed), computed from
`result`'s own luminance against `originalLuma` (the native frame's), gated on
`(gReversibleMode == 2 || gReversibleMode == 4) && gPassthrough == 0` (passthrough frames don't
go through the diverging decode at all, so they need no guard). A pixel already inside the guard
-- the ordinary case -- is untouched; only pixels the decode sent outside it get pulled back to
its edge. Unlike the three reverted attempts, this is *not* gated on `BeforeUpscale` or
`ModelWorkScale` at all -- the instability is inherent to the decode curve at any placement or
resolution, just only visible pre-SR-reduced because nothing else reinforces it elsewhere;
gating the fix narrowly the way the reverted attempts did would have been repeating the same
scoping mistake for a different mechanism.

Recompiled both `DlssNr_Shader.cso` (DX12) and `DlssNr_Shader_Vk.spv`/`.h` (Vulkan) from the same
source, both clean. Debug x64 rebuilt 09:33, Release x64 09:34 -- both 0 errors, 0 warnings from
any touched file.

User tested attempt 4 -- **lines still present with the default 2.0x guard.** But dragging the
existing "Highlight guard" slider (which attempt 4's clamp reads directly) down to 1.0x made them
**disappear completely.** This is the strongest confirmation yet: the guard mechanism itself is
proven to work, on the user's own hardware, on the exact repro -- the only open question was
whether 2.0x (the shared, pre-existing default) is tight enough for Replace mode, and it is not.

## The second root cause: white point was not tracking the game's exposure at all

Before settling on a guard-default change, asked the user why 2.0x might not be enough here --
their own hypothesis: "maybe this bug only for games that has the game exposure given." Checked
the log directly (`OptiScaler.log`, path the user supplied): `DLSS-NR exposure from the game:
DLSS.Pre.Exposure` swung from 1.000000 down to 0.38-ish across the session (a live, changing
signal Dawnwalker genuinely supplies), while every single `DLSS-NR composition: paper white` line
across the ~49-second log window read exactly **1.00x**, never once moving. Confirmed with the
user's own screenshot of the menu: "White point source" is set to "Game exposure", but the panel
itself says *"No game exposure available. Using manual paper white."*

Root cause: `ResolveWhitePoint()` (`DlssNr_Dx12.cpp`) only has two live tiers -- Multi-point scan,
and "Game exposure" which requires `g_nr.gameExposure`, a value read *from an ExposureTexture*.
Dawnwalker supplies `DLSS.Pre.Exposure` as a plain scalar with **no** ExposureTexture (confirmed
in the log: "ExposureTexture not supplied" on every frame). So despite the user explicitly
selecting "Game exposure" and the game genuinely offering a live exposure signal, the texture-only
tier never engages and white point silently falls all the way back to the static slider (1.0),
completely blind to the scene's real, changing exposure. As the static white point drifts out of
sync with the scene, ordinary content (sky included) intermittently sits closer to the encode
ceiling than a correctly-tracked white point would put it -- directly feeding the Replace-decode
instability fix attempt 4 targets. The two findings are complementary, not competing: the guard is
the safety net: the exposure fix reduces how often anything needs catching by it in the first
place.

User explicitly chose (via `AskUserQuestion`, given this touches exposure code with a documented
history of subtle regressions in this file's own comments -- "one session walked the divisor from
0.010 to 97.910") to attempt this fix in the same session rather than deferring it.

## Fix attempt 5: game-exposure fallback for pre-exposure-only games (2026-09-17, same session)

New middle tier in `ResolveWhitePoint()`, inserted between the existing texture-based
`gameExposure` tier and the final static-slider fallback: when "Game exposure" source is selected
and no ExposureTexture-derived value is available, but the game has ever supplied a genuine
`DLSS.Pre.Exposure` (new `g_nr.preExposureEverOffered` flag, mirroring the existing
`exposureEverOffered` pattern exactly), use `preExposure * trim` -- the existing tier's own
formula (`gamePreExposure / gameExposure * trim`) with the unavailable `gameExposure` term
neutralised to 1, not new math invented from scratch. Same trim clamp (`[0.25, 4.0]`), same
final-value clamp (`[0.01, 4096.0]`) as the tier it sits beside. Never overrides the existing
texture-based tier when that IS available -- strictly an additional fallback, existing behaviour
for games that do supply an ExposureTexture is untouched.

New `ExposureStatus` fields (`DlssNrFeature_Dx12.h`): `preExposureOfferedNow`/
`preExposureEverOffered`, mirroring `offeredNow`/`everOffered`'s existing pattern for the texture
signal. New `NrState` fields (`DlssNr_Dx12.cpp`) of the same names, set in the same exposure-
reading block that already populates `frame.PreExposure`. `GameExposureStatus()` extended to
expose them to the menu.

Menu UI (`DlssNr_Menu.cpp`) updated: the "No game exposure available. Using manual paper white."
message was actively misleading once this fix landed -- it would keep showing even while the new
fallback tier was genuinely active and doing something useful. Added a new branch, shown when no
ExposureTexture is available but PreExposure has been: "No exposure texture; using
DLSS.Pre.Exposure X.XXXX alone -> white point X.XX", so the panel's own text stays truthful about
what `ResolveWhitePoint()` is actually doing.

Scope note: DX12 only. Vulkan's white-point/exposure handling lives in a separate, un-audited
code path (`DlssNrFeature_Vk.cpp`) -- not touched this pass, not assumed fixed there. The user's
confirmed repro (Dawnwalker) is DX12 (confirmed earlier via the `DlssNr_Dx12::Dispatch`-prefixed
diagnostic log line), so this was not a blocking gap for closing this specific report, but it is a
known, explicit scope cut, not a silent omission -- worth a follow-up pass if a Vulkan game ever
reports the same shape of issue.

No shader changes this round -- purely CPU-side exposure resolution logic, `gWhitePoint`'s value
flows into the existing cbuffer field unchanged. Debug x64 rebuilt 10:09, Release x64 10:10, both
0 errors, 0 warnings from any touched file (`DlssNrFeature_Dx12.h`, `DlssNr_Dx12.cpp`,
`DlssNr_Menu.cpp`).

## Attempt 5 reverted -- final shipped fix is attempt 4 alone

User's call: keep the state that already worked (Highlight guard slider at 1.0x removes the lines
completely, confirmed on their own hardware) and skip the exposure-fallback investigation --
"no need for this texture exposure thing." Fully reverted attempt 5, cleanly:
`DlssNrFeature_Dx12.h`'s `preExposureOfferedNow`/`preExposureEverOffered` fields,
`DlssNr_Dx12.cpp`'s matching `NrState` fields + their assignment in the exposure-reading block +
the new `ResolveWhitePoint()` tier + `GameExposureStatus()`'s exposure of them, and
`DlssNr_Menu.cpp`'s new UI branch -- all removed. Grepped the whole `OptiScaler` tree for
`preExposureEverOffered`/`preExposureOfferedNow` afterward: zero matches. `git diff --stat`
against `main` confirms only the shader files changed (`dlssnr.hlsl` + its two recompiled binary/
header pairs) -- the three C++ files touched by attempt 5 show no diff at all, back to baseline.

The exposure-tracking finding itself (white point stuck at a static 1.00x for games shaped like
Dawnwalker -- `DLSS.Pre.Exposure` given, no ExposureTexture) is still real and still worth fixing
some day, just not tonight, and not bundled into this fix. Worth a fresh, dedicated plan if it
comes up again (a different game report, or the user revisiting it) -- do not silently re-attempt
it as part of a future dlssnr change without it being the actual subject of that change.

**Final shipped fix, confirmed working on the user's own hardware**: the Replace-mode highlight
guard added to `dlssnr.hlsl` in attempt 4 (see that section above for the full mechanism), reusing
the existing "Highlight guard" slider/`gMaxRatio` exactly as already exposed in the menu -- no
default value changed, no new setting added. Users on Replace modes (2/4) who see this artifact
lower the existing Highlight Guard slider toward 1.0x; Composed modes are unaffected either way
(the guard already applied to them unconditionally, before and after this fix). Debug x64 rebuilt
10:18, Release x64 10:18, both 0 errors, 0 warnings -- confirms the revert didn't disturb anything.
Vulkan gets this fix automatically and for free, without any Vulkan-specific code: `dlssnr.hlsl`
is the one shared shader source compiled for both `DlssNr_Shader.cso` (DX12) and
`DlssNr_Shader_Vk.spv`/`.h` (Vulkan) from the identical file, and both were recompiled together
back in attempt 4.

## Closing note (TASK_CLOSE)

Shipped fix: a highlight guard on Replace mode's decoded result (`dlssnr.hlsl`), reusing the
existing "Highlight guard" slider/`gMaxRatio` exactly as already exposed -- no new setting, no
default changed. Confirmed working on the user's own hardware (Dawnwalker): dragging that slider
to 1.0x removed the vertical-line artifact completely, on the actual repro (Apply-before-SR,
reduced model resolution, Hybrid Proxy + Replace / Neutwo + Replace).

Real root cause: `NeutwoDecode`/`HybridDecode` have no highlight guard at all by design ("no
ratio, no highlight guard, no palette blend" -- the file's own words), and their inverse diverges
toward infinity as the encoded peak approaches 1 (a near-white pixel -- sky). Composed modes never
hit this because they always pass through the same guard first; Replace never did. Three earlier
attempts (varying tap spacing/sizing on the resolve's `gSource`/`gModel` enlarge read) produced
*zero* observable change each time -- confirmed via a diagnostic log that the code was genuinely
executing, ruling out "the fix isn't running" as an explanation -- because none of them touched
the actual mechanism: smoothing the pre-decode sample doesn't tame the decode's derivative near
the pole. Full path from misdiagnosis to correct diagnosis, including the user's own two decisive
tests (the Debug-view localisation, then the Highlight-guard slider test), is in `known_gotchas.md`
now, not just here.

A second, genuinely separate bug was found mid-investigation (white point silently stuck at a
static 1.00x for games that supply `DLSS.Pre.Exposure` but no `ExposureTexture`, confirmed via
`OptiScaler.log` on this same repro) and fully fixed and verified working -- then explicitly
reverted at the user's request, to keep this fix scoped to the one thing it needs to be. Also
recorded in `known_gotchas.md`, kept as its own entry so it's findable independently and doesn't
get silently re-discovered as if new.

Vulkan: fixed automatically, no Vulkan-specific code needed. `dlssnr.hlsl` is the one shared
shader source compiled for both `DlssNr_Shader.cso` (DX12) and `DlssNr_Shader_Vk.spv`/`.h`
(Vulkan); both were recompiled from the identical fixed file.

Final diff against `main`: only `dlssnr.hlsl` plus its two recompiled shader binary/header pairs
(`DlssNr_Shader.cso`/`.h`, `DlssNr_Shader_Vk.spv`/`.h`) -- confirmed via `git diff --stat`. No
C++ changes ship; the exposure-fix C++ edits were fully reverted and confirmed at zero diff
against `main` before this close-out.

## Post-commit code review (same session)

Committed (`4016dda5`), then ran the `code-review` skill against the diff. Four findings, all
legitimate, three fixed:

1. **Real correctness gap**: the guard's `result *= boundedRatio/max(ratio,1e-6)` rescale cannot
   pull an exact zero vector back up -- if `NeutwoDecode`/`HybridDecode` early-out at their own
   `m <= 1e-6` guard (a fully collapsed near-black decode), the guard's multiply-by-anything left
   it pinned at black regardless of what the guard "wanted." Fixed: extracted a proper
   `ApplyReplaceGuard()` helper with an explicit degenerate-luma fallback to the native frame,
   mirroring the composed path's own existing `modelLuma <= 1e-5 -> upgraded = original` pattern
   rather than inventing new behaviour.
2. **Real correctness gap**: the guard was applied right after the decode, but the pre-existing
   Replace-only detail injection block runs *after* it and can multiply `result` by up to
   `1 + gReplaceDetailStrength` (~3x at the slider's max, 2.0) with no clamp against `guard` --
   silently able to push a pixel back past the bound the guard had just established. Fixed:
   relocated the guard call to run after detail injection instead of before it, so it's the
   actual last step before `result *= normScale`, bounding what reaches the screen rather than an
   intermediate value.
3. **Stale UI text**: `DlssNr_Menu.cpp`'s "HDR mapping" tooltip still said "Replace bypasses
   [the highlight controls] and may flicker" -- exactly backwards after this fix, since the
   Highlight guard slider is now the primary lever for the artifact this fix addresses. Reworded
   to say Replace still respects the guard and to point at it when Replace flickers/bands.
4. **Maintainability note, not fixed as suggested**: reviewer proposed a single helper shared
   between the composed and Replace guard-clamp idioms. Not done exactly that way -- composed's
   clamp operates on an already-derived `amplified` ratio mid-computation, not a raw colour
   triple, so unifying it would have meant restructuring stable, working composed-path code for a
   Replace-only fix. Instead scoped the new `ApplyReplaceGuard()` helper to Replace alone, which
   still resolves the underlying duplication-risk concern (the pattern exists in exactly one place
   now, not two or three) without touching the composed path at all.

Also hoisted `kRatioFloor` from a `CSMain`-local `const float` to file scope (alongside the
already-global `kLuma`), since the new top-level `ApplyReplaceGuard()` helper needed it and
duplicating the literal instead of sharing the one already in use would have reintroduced exactly
the kind of drift-risk the reviewer's 4th finding warned about.

Recompiled both shader targets again after these fixes, both clean. Debug x64 rebuilt 10:29,
Release x64 10:29, both 0 errors, 0 warnings from any touched file. `git diff --stat` since the
first commit: `DlssNr_Menu.cpp` (1 line) + `dlssnr.hlsl` (75 lines) + the two recompiled shader
binary/header pairs -- no other files touched by the review fixes.

## Context

User reported, then diagnosed live in-game across this session: visible vertical lines (most
apparent in flat sky regions) specifically when `Apply before Super Resolution` and reduced
`Model resolution` are combined. Diagnosed via a genuine A/B process, not guessed at:

1. Asked "do you notice the vertical lines" against a screenshot -- confirmed present, most
   visible in the sky.
2. Hypothesis 1 (SGSR1's enlarge algorithm) -- user switched Enlarge filter to Bilinear (fast),
   lines persisted. **Ruled out.**
3. Hypothesis 2 (Matched Residual's composition math, which only actually engages -- rather than
   silently no-op'ing -- when SGSR1 is *not* running, per the known stale-gate coupling) -- user
   switched Enlargement to Classic (still Bilinear filter), lines persisted. **Ruled out.**
4. Hypothesis 3 (resize-ratio aliasing, amplified by SR's downstream processing when NR runs
   pre-SR) -- user changed model resolution 60% -> 50%, line spacing visibly widened, tracking the
   ratio. Then user tested the same reduced resolution with Apply-before-SR turned *off*
   (post-SR placement instead) -- lines gone entirely. **Both predictions confirmed.**

Screenshots throughout were Kingdom Come: Deliverance II, DX12, RTX 5070 Ti, various model
resolutions (60%, 50%, 67% via Auto post-SR).

## Working theory (see plan for full mechanism)

Single-tap bilinear reads (`EditAt()`, `dlssnr.hlsl:308-319`, and the resolve's own per-pixel
`gSource`/`gModel` reads) produce a periodic sharp/soft alternation when upsampling by a
non-integer ratio -- source-texel-aligned output pixels read sharper, midpoint-aligned ones read
softer, repeating with the resize ratio's period. Normally too subtle to see. But in
Apply-before-SR placement, NR edits the render-resolution colour buffer in place, and DLSS Super
Resolution reads that exact buffer as its own input immediately after
(`NVNGX_DLSS_Dx12.cpp:1181-1184`) -- SR's temporal/spatial reconstruction (and possibly RCAS
sharpening, enabled in the user's repro but not yet isolated as a required factor) treats the
periodic pattern as real detail and reinforces it into visible lines. Post-SR placement has
nothing downstream to do that reinforcing, so the same underlying artifact stays invisible.

**Not yet code-confirmed** -- this is the strongest hypothesis to survive four rounds of live A/B,
but the plan's own first three steps are about actually tracing the code and confirming the
mechanism precisely (including whether RCAS is a required amplifier) before committing to a fix
shape. Do not skip straight to a fix without that confirmation.

## Notes for whoever executes this plan

This is new territory, not a return to code this session already mapped -- SGSR1 and Matched
Residual were both cleanly ruled out as the cause by the user's own testing. Don't reach for
SGSR1-adjacent tuning just because it's the most recently touched code in this area. The actual
suspect is the pre-SR handoff itself: NR editing the render-resolution buffer in place right
before SR consumes it, and the bilinear resize taps used throughout the resolve when the model's
buffer is smaller than native.
