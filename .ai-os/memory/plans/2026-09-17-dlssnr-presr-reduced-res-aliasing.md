# Plan: fix vertical-line artifact from reduced-resolution NR under Apply-before-SR

- Branch: fix/dlssnr-presr-reduced-res-aliasing
- Created: 2026-09-17
- Status: done -- **fix confirmed working on the user's own hardware.** Attempts 1-3 (resample
  theory) were abandoned, confirmed wrong via a diagnostic log. Attempt 4 (a highlight guard on
  Replace mode's decode, reusing the existing "Highlight guard" slider/`gMaxRatio`) is the shipped
  fix: the user's own test proved dragging that slider to 1.0x makes the lines disappear
  completely. A follow-on investigation (attempt 5) found and fixed a real, separate white-point/
  exposure bug that likely explains why the pre-existing 2.0x default wasn't tight enough for this
  repro -- but the user chose to revert attempt 5 and just keep using the Highlight Guard slider
  manually, so attempt 5 is fully backed out and not part of this fix (see `known_gotchas.md` for
  both findings). Only `dlssnr.hlsl` (plus its two recompiled shader binary/header pairs) differs
  from `main` -- confirmed via `git diff --stat`. Debug+Release x64 rebuilt clean, 0 warnings.
  Vulkan gets the fix automatically (shared shader source, both targets recompiled together).
- Task file: memory/tasks/fix_dlssnr-presr-reduced-res-aliasing.md

## Context

User-reported and in-game A/B diagnosed this session (screenshots, Kingdom Come: Deliverance II,
DX12, RTX 5070 Ti): visible vertical line artifacts, most apparent in low-contrast sky regions,
appearing only when **both** `DlssNrRunBeforeSr` (Apply before Super Resolution) and reduced
`DlssNrWorkingScale`/model resolution are active together. Neither condition alone reproduces it
(user confirmed: same reduced resolution, Apply-before-SR off -> clean; Apply-before-SR on at
100% was never the report -- the "and" in the original report was load-bearing).

**Diagnosis, via live in-game A/B this session, not guessed:**
- Enlarge filter (SGSR1 vs Bilinear) -- **ruled out**. Lines persist identically on Bilinear
  (fast), which fully bypasses SGSR1 -- SGSR1 is not the cause.
- Composition mode (Classic vs Matched Residual) -- **ruled out**. Lines persist on Classic +
  Bilinear, the oldest and simplest reduced-resolution code path in the codebase, predating every
  SGSR1/toggle/Vulkan feature built earlier this session.
- Resize ratio -- **implicated**. Changing model resolution from 60% to 50% (a coarser upsample
  ratio, 2.0x vs ~1.67x) made the line spacing visibly *wider*, tracking the ratio directly. A
  fixed-position bug (buffer edge, thread-group boundary, UI overlap) would not respond to this.
- Placement (before vs after SR) -- **the deciding factor**. Same reduced resolution, same scene,
  NR placed *after* SR instead of before -- lines gone entirely.

**Working theory (mechanism, not yet code-confirmed as the fix target):** `EditAt()`
(`dlssnr.hlsl:308-319`) and the resolve's own per-pixel reads sample `gSource`/`gModel` via a
single-tap `gLinear.SampleLevel` bilinear read with no prefiltering (`dlssnr.hlsl:263`'s own
comment: "so the edit can be read at a different size"). Plain single-tap bilinear upsampling by
a non-integer ratio has a well-known artifact: the interpolation weight between the two nearest
source texels varies periodically across output pixels -- some output pixels land near a source
texel centre (sharper), others near the exact midpoint between two texels (softer) -- producing a
periodic sharp/soft alternation whose spatial period tracks the resize ratio. In isolation this is
usually too subtle to see. The reason it only shows up in Apply-before-SR: in that placement, NR
edits the render-resolution colour buffer *in place*, and that exact buffer is what DLSS Super
Resolution reads as its own colour input immediately afterward
(`NVNGX_DLSS_Dx12.cpp:1181`/`:1216`, `EvaluateBeforeUpscale` runs before
`D3D12_EvaluateFeature`/`TryEvaluateOptiFeature`). SR's own temporal/spatial reconstruction and
any downstream sharpening (RCAS was enabled in the user's repro session, though not yet confirmed
as a required amplifier vs. SR's own accumulation alone) are very good at detecting and
reinforcing structured, non-random per-pixel patterns -- treating the periodic
sharp/soft alternation as real high-frequency detail worth preserving or sharpening, rather than
letting it stay subliminally soft the way it does in post-SR placement (where NR's edit is the
last thing applied, with nothing downstream to reinforce it).

**Not yet done, and the plan's first real step:** actually tracing the exact code path from NR's
final write in the pre-SR seam through to what SR reads, and confirming (not assuming) whether
RCAS/sharpening is a necessary amplifier or the artifact is visible from SR's own reconstruction
alone. The mechanism above is a strong, evidence-backed hypothesis from this session's live A/B,
not a confirmed root cause at the code level yet.

**Acceptance criteria:**
- Vertical-line artifact is gone or reduced to below-visible on the user's own repro (KCD2, Apply
  before Super Resolution on, model resolution 50-67%, same scene/sky region used in this
  session's testing).
- No regression to: post-SR NR at any resolution, Apply-before-SR at 100% model resolution,
  Composed and Replace modes, SGSR1 enlarge filter, Matched Residual composition, Vulkan parity
  (whatever fix lands on DX12 needs an explicit decision on whether it's ported to Vulkan too --
  not assumed automatically).
- Debug + Release x64 build clean, 0 new warnings.
- In-game re-confirmation by the user on the same repro before considering this closed.

**Out of scope (unless investigation forces it):**
- Anything about SGSR1's own algorithm or the enlarge-method toggle -- both already ruled out as
  the cause.
- Anything about Matched Residual's composition math -- also already ruled out.
- Re-litigating the ResidualAcrossRR/post-RR-placement decisions from earlier this session --
  unrelated axis (that was about RR specifically; this reproduces with RR off, on a plain SR-only
  pre-SR placement).

## Steps

1. [x] **Trace the exact pre-SR handoff.** Confirmed by reading, not inferred. In
   `NVNGX_DLSS_Dx12.cpp:1180-1184`/`:1215-1219`, `DlssNr::EvaluateBeforeUpscale(InCmdList,
   InParameters, ...)` is called on the same `InParameters` object that is handed to
   `NVNGXProxy::D3D12_EvaluateFeature()`/`TryEvaluateOptiFeature` on the very next lines. Inside
   `EvaluateInternal` (`DlssNr_Dx12.cpp:3376-3379`), when `beforeUpscale` is true, `target =
   GetResource(params, NVSDK_NGX_Parameter_Color, "DLSSD.Color")` -- pulled from that identical
   `params` object -- and the dispatch is `g_compose->Dispatch(cmdList, target, depth, motion,
   target, frame, timingQueue)`: source and destination are the same resource pointer. Downstream,
   `Dispatch()` (`DlssNr_Dx12.cpp:1697` on) writes the resolve's result either directly into that
   resource via UAV, or -- when the resource lacks UAV support -- through a scratch-and-copy
   fallback that still lands back in the same resource before returning (comment at
   `DlssNr_Dx12.cpp:1726-1728`: "both paths return the resource exactly as their caller gave it to
   us"). So when the SR call reads `NVSDK_NGX_Parameter_Color` moments later, it gets the literal
   same `ID3D12Resource*`, already carrying NR's edit -- not a copy, not an intermediate resolve
   state. Confirms the plan's premise.
2. [x] **Confirm or rule out RCAS/sharpening as a required amplifier.** User tested the same
   repro with RCAS/Motion Adaptive Sharpness off -- lines persisted. **Sharpening is not required;
   SR's own temporal/spatial reconstruction alone is sufficient to amplify the periodic pattern
   into visible lines.** Fix should target the resize tap producing the pattern in the first place,
   not anything RCAS/sharpening-specific.
3. [x] **Confirm the resize-tap theory at the code level**, not just behaviourally. Confirmed --
   `dlssnr.hlsl:905-906`, the resolve's *main* per-pixel read (not gated behind any debug/compare
   mode; `gCompareMode`'s own remapping of `cmpUv` only matters in the split-screen debug view --
   this line runs every ordinary frame): `proxySample = gSource.SampleLevel(gLinear, cmpUv, 0)`,
   `modelSample = gModel.SampleLevel(gLinear, cmpUv, 0)`. `cmpUv` defaults to plain `uv` (line
   875), and `uv = (float2(id.xy) + 0.5) / float2(gWidth, gHeight)` (line 570) -- `id.xy` is the
   *native*-resolution dispatch thread id, `gWidth`/`gHeight` the *native* output dims. `gSource`/
   `gModel` are bound at the model's working resolution (`workScale * native`), so this is by
   construction a single-tap bilinear upsample with no mip chain involved (`SampleLevel(..., 0)`
   always reads mip 0, the reduced-res texture itself -- there is no box-filtered mip to fall back
   on even if the sampler's filter mode wanted one).
   Math: for native pixel `n` and working scale `s`, the sampled source-texel coordinate is
   `n*s + 0.5*s` (up to the half-texel SampleLevel offset), so the bilinear blend weight is
   `frac(n*s + const)` -- a sawtooth in `n` with period `1/s` native pixels whenever `s` is
   rational. At `s=0.5` that period is exactly 2 -- every other native pixel lands exactly on a
   source texel (weight 0, sharp) and the ones between land exactly at the midpoint (weight 0.5,
   maximally soft) -- the cleanest, most visible possible alternation, i.e. vertical lines every 2
   pixels. At `s=0.6` the period is 1/0.6 ~= 1.667, a beat pattern against the display's integer
   pixel grid rather than a clean 2px stripe -- still present, less crisply "line"-shaped. This
   matches the user's own report exactly (50% wider/more visible gaps than 60%) and requires no
   other mechanism.
   Ruled out explicitly: there is no fixed-size box filter or kernel radius anywhere in this read
   path -- it is one `SampleLevel` call, nothing else resolution-dependent competes as an
   explanation. The alternation is a direct, unavoidable consequence of single-tap bilinear
   resampling by a non-integer ratio; nothing else needs to be true for it to occur.
4. [x] **Candidate fix.** User chose, after seeing the tradeoffs: replace the single-tap
   `SampleLevel` at the actual resize taps (`dlssnr.hlsl:905-906`, the resolve's main `gSource`/
   `gModel` reads) with a small multi-tap average, gated to `BeforeUpscale && workScale<1.0`.
   Root-cause fix -- targets the exact mechanism confirmed in step 3, rather than patching the
   symptom after compositing. Implemented as `SampleReducedColour()`: a 4-tap box average at
   `uv + texel*(±0.25,±0.25)`, where `texel = 1/(gWidth,gHeight * gModelWorkScale)` -- spans
   exactly one source texel, so it scales with the resize ratio automatically (no separate radius
   constant to keep in sync, unlike the discrete-pixel-offset detail-injection blur this file
   already has, which needed one because it reads via `Load()` at native res instead of continuous
   `SampleLevel()`). Gated off (falls through to the original single-tap read, byte-identical)
   whenever `gBeforeUpscale == 0` or `gModelWorkScale >= 0.999` -- new `gBeforeUpscale` cbuffer
   field added specifically because nothing already distinguished placement inside the resolve
   shader (the same `DlssNrMode_Resolve` runs for both).
5. [x] **Implemented.** `dlssnr.hlsl`: new `gBeforeUpscale` cbuffer field, new
   `SampleReducedColour()` helper, the resolve's two hot reads switched to it. `DlssNr_Common.h`:
   matching `BeforeUpscale` field appended to `DlssNrConstants` (trailing scalar, same "flat run of
   4-byte scalars" idiom as `ModelWorkScale`/`ReplaceDetailStrength` -- still well inside the
   struct's 256-byte `alignas`, no `static_assert` change needed). `DlssNr_Dx12.cpp`:
   `resolveParams.BeforeUpscale = frame.BeforeUpscale ? 1u : 0u;` beside the existing
   `ModelWorkScale` line. Gating reviewed against both points explicitly: the scale signal is
   `gModelWorkScale`, already computed in C++ (not inferred from a buffer's bound size, so it does
   not fall into the SGSR1-enlarge stale-gate trap); the new box-average's `texel` denominator is
   `max(float2(gWidth,gHeight) * gModelWorkScale, 1.0)`, floored so it cannot divide by zero at
   `workScale -> 0`. At `workScale == 1.0` or post-SR, the function returns the original single-tap
   read unchanged -- byte-identical to before this change in both those cases.
6. [x] **Vulkan parity: in scope, implemented.** Checked rather than assumed -- Vulkan has its own
   `EvaluateBeforeUpscaleVk`/`beforeSr` pre-SR placement (`DlssNrFeature_Vk.cpp`), so this bug and
   fix both apply there too; it is not DX12-only the way SGSR1 started out. `dlssnr.hlsl` is the
   one shared shader source compiled for both backends (`VK_MODE` only changes resource binding
   decorations, not this logic), so the fix ports for free once the C++ side sets the new field:
   added `encode.BeforeUpscale = beforeSr ? 1u : 0u;` in `DlssNrFeature_Vk.cpp`, on the `encode`
   constants block that `resolve` is later copied from (mirrors how `ModelWorkScale` itself is set
   there). Both `DlssNr_Shader.cso` (DX12) and `DlssNr_Shader_Vk.h`/`.spv` (Vulkan) recompiled from
   the same updated `dlssnr.hlsl` via `dxc.exe`, both compiles clean with no warnings.
7. [x] Debug + Release x64 full build. Both built clean, 0 errors. 0 warnings from any of the
   touched files (`dlssnr.hlsl`'s recompile, `DlssNr_Common.h`, `DlssNr_Dx12.cpp`,
   `DlssNrFeature_Vk.cpp`) in either configuration -- checked explicitly by grepping the build
   logs for those filenames, not just eyeballing the tail. (One real mistake caught here: the
   first Vulkan header regeneration used array name `DlssNr_spv`, but `DlssNr_Vk.cpp:89` expects
   the original `dlssnr_spv` -- confirmed against the pre-edit header via `git show HEAD:...`
   before regenerating with the correct name. C2065 undeclared-identifier until fixed.)
8. [x] **In-game re-confirmation.** Attempt fix build (Debug x64, ~08:23, single-tap enlarge
   theory) FAILED -- lines unchanged on Dawnwalker, a cross-game data point ruling KCD2-specific
   causes out. `gDebugView` localisation (Proxy clean, Difference-amplified showed the pattern,
   only on Replace modes) redirected the investigation to `NeutwoDecode`/`HybridDecode`'s missing
   highlight guard instead of the resample read. That fix (attempt 4) is **confirmed working**:
   the user's own test, dragging the existing "Highlight guard" slider from 2.0x to 1.0x, made the
   lines disappear completely, on their own hardware, on the actual repro. A related exposure bug
   found along the way (attempt 5) was fixed, verified, then explicitly reverted per the user's
   own choice to keep the fix scoped to the guard alone.
9. [x] `TASK_CLOSE`. The mechanism (Replace mode's decode divergence near white, and the three
   failed resample-based attempts before the real cause was found) is recorded in
   `known_gotchas.md`, along with the separate reverted exposure finding, kept distinct and
   findable rather than folded into one entry. This is a genuinely new failure class for this
   codebase, not a repeat of the stale-gate or cancellation bugs already on record.

## Notes

This bug predates every SGSR1/toggle/Vulkan-port feature built earlier this session -- it
reproduces on the plainest, oldest reduced-resolution code path (Classic composition, implicit
Bilinear enlarge). Nothing built this session caused it, and nothing built this session is
positioned to fix it by construction; this is new territory (the pre-SR handoff to SR itself),
not a return to already-mapped code. Resist the temptation to reach for SGSR1-adjacent fixes
(tuning its threshold, etc.) just because it's the most recently touched code in this area --
it's already been cleanly ruled out.
