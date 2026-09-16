# Plan: SGSR1 enlarge for DLSS-NR's model resolution < 100%

- Branch: `feat/dlssnr-sgsr1-upscale`
- Created: 2026-09-16
- Status: done
- Task file: memory/tasks/feat_dlssnr-sgsr1-upscale.md

## Context

When `DlssNrWorkingScale`/`ModelResolutionAuto` puts NR's model resolution below 100%
(`Config.h:400`, `DlssNr_Dx12.cpp:1844-1855`), the model's answer comes back at
`workWidth x workHeight` (smaller than native) and today gets enlarged back to native only
implicitly — the resolve compute pass (`DlssNrMode_Resolve`) reads the small answer texture
with a single HW-bilinear tap while compositing (`DlssNr_Dx12.cpp:2896-2897`, comment at
2881-2886). The mirror case, `workScale > 1.0` (supersampling), already has a real dedicated
pass: `superDown` (an `OS_Dx12` instance) averages the oversized answer back to native with a
real filter before resolve ever sees it. This plan adds the equivalent for the `< 1.0` side: a
dedicated enlarge pass using Qualcomm's SGSR1 (single-pass, edge-directed 12-tap Lanczos-like
upsample + adaptive sharpen — no motion vectors/history needed, fits a single still frame)
instead of the implicit bilinear tap.

User-confirmed via `PLAN_BRAINSTORM` (2026-09-16): always-on when reduced (no new UI/config),
DX12 only, dedicated post-NR pass (not a general `Scaler` enum entry), written from scratch
since no local SGSR1 source exists in this repo. The actual algorithm was pulled from
Qualcomm's official repo (`SnapdragonGameStudios/snapdragon-gsr`, `sgsr/v1/include/hlsl/
sgsr1_mobile.h` + `sgsr1_shader_mobile.hlsl`, BSD-3-Clause) during brainstorming rather than
guessed — `SgsrYuvH`/`weightY`/`fastLanczos2`/`edgeDirection`, `OperationMode 1` (RGBA),
`EdgeThreshold 8.0/255.0`, `EdgeSharpness 2.0`, edge direction off by default. `ViewportInfo`'s
component semantics (`float4(1/srcW, 1/srcH, srcW, srcH)`) were derived directly from the
shader's own `imgCoord`/`coord` math (`imgCoord = uv*con1.zw - (0.5,-0.5)`, `coord =
imgCoordPixel*con1.xy`), not from external docs — the official README doesn't spell this out.

**Not verified yet — first step of execution, not assumed here:** whether `outputNative`'s
buffer (currently only created for `workScale > 1.0`) can be safely reused for the `< 1.0` leg
too, and whether `resolveProxy`'s native-buffer substitution (the `colorCopy` swap at 2896)
needs an equivalent on this side.

### Out of scope
- Vulkan (`DlssNrFeature_Vk.cpp` untouched — matches the DX12-only precedent already set by
  `ResidualAcrossRR`'s removal, `feat/dlssnr-postrr-simplify-v2`).
- `workScale == 1.0` and `> 1.0` paths — unchanged.
- SGSR2 (temporal variant) — SGSR1 only, matches the "single input, no history" fit for this
  seam.
- Exposing SGSR1 as a general `DlssNrScalingDownscaler`/`OutputScalingDownscaler` filter choice.

## Steps

1. [x] **Audit the exact injection point.** Confirm in `dlssnr.hlsl`/`DlssNr_Common.h` that
   `DlssNrMode_Resolve`'s answer-texture read really is an implicit `SampleLevel` bilinear tap
   when the bound SRV's dims don't match native (not something else). Confirm the answer
   buffer's DXGI format supports `Texture2D::Gather*` (SGSR1 needs it). Confirm whether
   `outputNative` can be shared between the `>1` and `<1` legs (mutually exclusive per-frame,
   but `>1`'s comment calls it "UAV from last frame" — check there's no cross-frame dependency
   that reuse would break) and whether `resolveProxy` needs a native-resolution counterpart on
   the `<1` side the way `colorCopy` provides one on the `>1` side. — verify: read the actual
   shader source + `DispatchPass` SRV/sampler setup, write findings inline in this plan before
   touching code (same discipline as the `ResidualAcrossRR` audit).
2. [x] **Port the shader.** `OptiScaler/shaders/sgsr1/precompile/sgsr1.hlsl` (mirrors
   `output_scaling/fsr1`'s layout), adapted from Qualcomm's official mobile shader into a
   `numthreads(8,8,1)` compute kernel — one `Texture2D` SRV in (`t0`), one `RWTexture2D` UAV out
   (`u0`), one CBV (`b0`) holding `ViewportInfo = float4(1/srcW, 1/srcH, srcW, srcH)` plus
   `DstSize` (needed for the per-thread bounds check/UV that a compute kernel has to do itself,
   unlike the original pixel shader). Per-thread UV = `(dispatchThreadID + 0.5) / DstSize`. Uses
   `GatherGreen` only, not all four channels — with `OperationMode` fixed to `1` (RGBA) at port
   time, upstream's own `SGSRH(coord, mode)` dispatcher always resolves to the green channel, so
   carrying the other three gather variants would have been dead code. Kept Qualcomm's
   BSD-3-Clause header verbatim. Defaults from upstream: edge direction off, edge threshold
   8/255, edge sharpness 2.0 — not user-configurable.
3. [x] **Compile.** `dxc -T cs_6_0 -E CSMain -O3 -Qstrip_debug -Qstrip_reflect` via the repo's own
   vendored `OptiScaler/shaders/shader_tools/dxc.exe` (found in-tree — `DEVELOPMENT.md` rule 6
   names the same flags/entry point). Clean compile, `sgsr1_Shader.cso` (3812 bytes, valid DXBC
   magic). Header array (`sgsr1_Shader_cso[]`) generated by hand in the same format
   `create_header.py` produces (`python`/`py` weren't on PATH in this environment) — pragma-once,
   12 bytes/line, `0x%02x`. DX12 only, matching the plan's scope — no `_Vk`/`.spv`, no DX11
   variant (the `bcds_*`/`fsr_easu` filters carry those because Output Scaling supports DX11/VK;
   this NR-internal pass doesn't).
4. [x] **New pass class.** `SGSR1_Dx12` (`shaders/sgsr1/SGSR1_Dx12.h/.cpp`), `Shader_Dx12`-
   derived exactly like `OS_Dx12`: root signature (1 SRV, 1 UAV, 1 CBV, 1 bilinear-clamp static
   sampler), `CreateComputePipeline` from step 3's blob, `Dispatch(cmdList, in, out)` sized off
   the destination resource. No filter-switch logic — one fixed shader. — verify: builds
   against `Shader_Dx12`'s existing interface, no new virtuals needed.
5. [x] **Wire into `NrState`.** Deviated slightly from the draft: `sgsr1Up` is built lazily at
   its own use site (Step 6, inside the resolve block) rather than next to `superUp`/`superDown`
   in the model-input proxy block — that block is specifically about the model's *input* (a
   different concern from this leg's *output* enlarge), and `sgsr1Up` has no `nrScaler`
   dependency to piggyback on, so coupling it there would have been arbitrary. `outputNative`'s
   creation gate (`DlssNr_Dx12.cpp`) broadened from `workScale > 1.0f` to `workScale != 1.0f` —
   confirmed safe by Step 1's audit, no park/release changes needed (already resource-pointer-
   generic).
6. [x] **Dispatch the up-leg.** Added parallel to the existing down-leg: `sgsrUpOk`, lazy
   `g_nr.sgsr1Up` construction, `Dispatch(cmdList, finalAnswer, g_nr.outputNative)`, same barrier
   shape as `superDownOk`. Both ternaries widened to `resolveNativeOk = superDownOk || sgsrUpOk`
   — covers Step 1's `resolveProxy` finding (native `colorCopy` on success, not just the answer).
   The trailing `outputNative` state-restore barrier (previously gated on `superDownOk` alone)
   also widened to `resolveNativeOk`, since the new leg needs its buffer transitioned back to UAV
   for the next frame too.
7. [x] **Lifecycle.** `sgsr1Up` added to the shutdown release list next to `superUp`/`superDown`.
   `ParkNrResource`'s coverage of `outputNative` was already resource-pointer-generic (not gated
   on `workScale`), confirmed no change needed.
8. [x] **Docs.** `Config.h`'s `DlssNrWorkingScale` doc comment updated (sub-100% now enlarges via
   SGSR1, not an implicit bilinear tap; above-100% behavior unchanged). `pre-sr-multipass.md`
   turned out to be scoped narrowly to pre-SR placement/multipass persistence specifically (its
   only sections are "Multipass lifetime and data flow," "Resource-state rules," "Guardrails") --
   it never covered `WorkingScale`/supersampling at all, so the plan's original assumption of an
   "existing supersample explanation" there was wrong. Left it untouched rather than forcing an
   unrelated note into it; the `DlssNr_Dx12.cpp` inline comments at the new up-leg + the `Config.h`
   comment are this feature's actual documentation, matching how `WorkingScale`/`ModelResolutionAuto`
   were already documented (menu tooltip + code comments, no separate design doc).
9. [x] **Build.** Debug|x64 and Release|x64 both built clean through `OptiScaler.sln` (`/t:OptiScaler`),
   exit 0, 0 errors, both. Debug: 63 warnings (matches this branch's own recorded baseline).
   Release: 64 warnings. Neither log has a single SGSR1/sgsr1-tagged warning (grepped explicitly)
   — the new files add zero warnings of their own; the absolute counts are the branch's inherited
   baseline, not isolated against a separate pre-change build. Had to add
   `shaders\sgsr1\SGSR1_Dx12.{h,cpp}` to `OptiScaler.vcxproj`/`.vcxproj.filters` first — the
   linker's first attempt failed with 3 unresolved externals because new files aren't picked up
   by the project automatically (no wildcard globbing here), same as any other new source file in
   this codebase.
   **Manual verify: not performed, disclosed rather than claimed.** No GPU/game environment is
   available in this session to actually run the DLL and compare SGSR1 vs. bilinear or confirm
   the `== 1.0`/`> 1.0` paths are visually unaffected. What's confirmed instead: the `== 1.0` path
   never touches any new code (untouched `workScale == 1.0f` branch), and the `> 1.0` path's own
   code is untouched (`superDownOk`'s branch and both ternaries were only *widened* with an `||`,
   never altered on the `superDownOk`-true side) — a code-level argument for no regression there,
   not an in-game confirmation. The actual SGSR1-vs-bilinear quality comparison is genuinely
   unverified and needs the user (or a session with GPU access) to check in-game.
10. [x] **Independent Review Pass** (`code-review` skill, high effort) on the full diff. 2
    findings, both real but left as-is rather than fixed, for the same reason: each mirrors an
    existing, consistently-applied pattern in sibling code, not something this diff newly
    introduces.
    - **`workScale < 1.0f` (float) vs. `reduced` (the actual rounded-size flag) can disagree in a
      narrow rounding band near 1.0** (e.g. `workScale = 0.9998` at some widths rounds
      `workWidth == width`, so `reduced` is false but the new leg's `workScale < 1.0f` check is
      still true, dispatching a same-size SGSR1 pass). Confirmed: the pre-existing `workScale >
      1.0f` down-leg gate has the identical mismatch already — this diff mirrors that gate's own
      condition shape rather than introducing a new one. Diverging the new leg to check `reduced`
      instead would desync the two legs' gating logic for no clear benefit and leave the
      down-leg's identical case unfixed anyway.
    - **`g_nr.outputNative`'s `CreateScratch` has no failure latch, so a failed allocation retries
      every frame** — true, and this diff's widened trigger (`workScale > 1.0f` -> `!= 1.0f`)
      does make that reachable from one more direction. But confirmed `g_nr.colorSmall`
      (`1978-1979`, created on the exact same `reduced` condition, structurally identical) has no
      latch either — this is the established convention for the "core" scratch buffers
      (`output`/`colorCopy`/`hdrCopy`/`colorSmall`/`outputNative`); only the genuinely optional
      multi-pass extras (`passScratch`/`passClampScratch`) get individual `*Failed` latches, which
      read as an intentional degrade-gracefully-vs-retry-forever distinction, not an oversight.
      Adding a latch to `outputNative` alone would single it out inconsistently from its
      structural sibling right next to it.

## Post-ship correction (2026-09-16, in-game testing)

User tested the built DLL in-game (Assetto Corsa, `WorkingScale` manually set below 100%). First
report: "the upscale is blurred." Ruled out non-engagement via a new INFO-level status log (the
pass's own `Dispatch()` only logs at DEBUG, same as the pre-existing `superUp`/`superDown` --
confirmed those are equally silent in the same log, so silence wasn't itself informative) --
confirmed `engaged`, `sgsr1Up init ok`. Second, more precise report after a targeted question:
"below 100 looks like the low res image got combined with the final image." That description was
the actual signal.

**Root cause: Step 1's `resolveProxy` finding was wrong.** Routing the `<1` leg's proxy to the
native `colorCopy` (mirroring the `>1` leg) assumed the two cases were symmetric. They aren't: the
`>1` leg's model input *was* the native proxy (just resampled larger, no detail lost), so comparing
its enlarged answer against native `colorCopy` is valid. The `<1` leg's model only ever saw the
*downsampled* `colorSmall` -- it never had native detail. `edit = SGSR1(model(colorSmall)) -
colorCopy`, added back onto the native original (`finalOutput = original + edit`), makes the native
terms nearly cancel algebraically -- the display ends up dominated by the small buffer's own
limited detail, blended with leftover fragments where the nonlinear encode/decode (soft
knee/Neutwo, sRGB<->linear) keeps the cancellation from being exact. That's "the low res image
combined with the final image," precisely.

**Fix:** a second native scratch buffer (`NrState::proxyNative`) and a second `SGSR1_Dx12`
dispatch, enlarging `modelInput` (`colorSmall`) the same way the answer is enlarged -- so both
sides of the resolve's `edit = answer - proxy` are on the same detail basis (both derived from the
downsampled source via the same kernel), matching what the `>1` leg already had implicitly (both
its sides already share the native basis). `resolveProxy` now branches three ways: `colorCopy` for
`superDownOk`, the new `proxyNative` for `sgsrUpOk` (now requiring *both* the answer and proxy
dispatches to succeed), `modelInput` as the shared fallback. Full lifecycle (creation gate, park,
shutdown release, barrier restore) mirrors `outputNative`'s, independently tracked since
`proxyNative` is never touched by the `>1` leg.

Both configs rebuilt clean, 0 errors, after the fix. Not yet re-tested in-game as of this write --
next step is the user re-deploying and confirming the artifact is gone.

## Second Review Pass (2026-09-16, after the proxy-enlarge fix)

Run against the updated diff (code-review skill, high effort). 2 findings, both real, both fixed:

- **One `SGSR1_Dx12` instance dispatched twice per frame.** `SGSR1_Dx12`, like `OS_Dx12`, is built
  for one `Dispatch()` call per frame per instance -- a single non-double-buffered
  `_constantBuffer` every call's `CreateConstantsBuffer` overwrites, and a 2-slot
  `FrameDescriptorHeap` meant to alternate *across frames*. The proxy-enlarge fix above called
  `Dispatch()` twice (answer, then proxy) on the same `g_nr.sgsr1Up`, back-to-back, before either
  is submitted to the GPU -- the second call's CPU-side constants write lands before the GPU
  executes either dispatch, so both invocations would read whichever call wrote last. Currently
  masked (both calls happen to share identical src/dst dimensions this session, so the
  overwritten constants are numerically the same either way) but not guaranteed, and the 2-slot
  heap ping-pong also collapses to zero cross-frame lead time per call site under CPU-ahead-of-GPU
  queuing. **Fixed**: split into `sgsr1UpAnswer`/`sgsr1UpProxy`, two separate instances, mirroring
  how `superUp`/`superDown` are already two separate `OS_Dx12` instances for the `>1` leg.
- **Engage/creation gates used raw `workScale < 1.0f` instead of `reduced`.** With
  `ModelResolutionAuto`'s continuous ratio, a `workScale` landing just under 1.0 in the rounding
  band (e.g. ~0.9998) can round `workWidth == width`, making `reduced` false -- `colorSmall` is
  never built and `modelInput` stays native -- while `workScale < 1.0f` is still true, engaging
  SGSR1 at a wasted 1:1 scale with no correctness guarantee of being identity-preserving at unity
  (a real, if narrow, unwanted-sharpen risk). **Fixed**: `proxyNative`'s creation gate and the
  up-leg's engage condition now both require `reduced && workScale < 1.0f`. Left `outputNative`'s
  shared creation gate (`workScale != 1.0f`, covering both legs) and the pre-existing `>1` leg's
  own gate unchanged -- over-allocating a buffer that goes unused is harmless, and fixing the
  `>1` leg's identical pre-existing pattern is still out of scope per the first Review Pass's
  disposition.

Both configs rebuilt clean, 0 errors, after these fixes too. Still not re-tested in-game.

## Third fix: Neutwo/Hybrid domain mismatch (user report, post-Second-Review-Pass)

User re-tested and reported a *different* blur: "it becomes blurred when i choose neuto replace
or hybrid replace" (the two "Replace" values of `DlssNrReversibleMode`, 2 and 4). Confirmed via
`AskUserQuestion` that model resolution was below 100% when this happened, implicating SGSR1.

**Root cause.** When Neutwo/Hybrid mapping is on, the model answer/proxy textures SGSR1 upscales
are not plain sRGB gamma — they're `LinearToSrgb(NeutwoEncode(normalized))` or
`LinearToSrgb(HybridEncode(normalized))` (`dlssnr.hlsl:772-784`), an extra compressive curve
stacked on top of gamma, squashing local contrast hardest in highlights. `sgsr1.hlsl`'s edge
detector compares neighbouring green-channel texels against a fixed `kEdgeThreshold = 8/255`
(Qualcomm's own constant, tuned for ordinary gamma-encoded content). Against the extra-compressed
curve that threshold rarely trips, so the pass silently fell back to its plain-bilinear branch
(`sgsr1.hlsl`'s `pix.xyz` returned unchanged when `edgeVote <= kEdgeThreshold`) far more than
intended — a real loss of sharpening, not a perception effect.

Composed modes (0/1/3) mask this: the resolve's ratio/hue blend leans on the native-resolution
`original` for most of its spatial detail (`dlssnr.hlsl:959-1067`). Replace modes (2/4) take the
SGSR1 output completely raw (`dlssnr.hlsl:1072-1079`, "NONE of the composition above"), so the
softness shows through directly — matching exactly what was reported and exactly why the
Second-Review-Pass's earlier in-game "100 and above looks good" report (composed/default mode at
the time) didn't catch it.

**Fix (user chose "decode before SGSR1, re-encode after"):** ported the Neutwo/Hybrid
encode/decode math (`Neutwo`, `NeutwoEncode/Decode`, `HybridCurve(Inv)`, `HybridEncode/Decode`,
`SrgbToLinear`/`LinearToSrgb`) from `dlssnr.hlsl` into `sgsr1.hlsl`, keyed off a new
`ReversibleMode`/`Passthrough` pair added to `sgsr1.hlsl`'s `Params` cbuffer (replacing the
`_Pad` placeholder) and threaded through `SGSR1_Dx12::Dispatch()`'s new parameters, sourced from
the resolve's own `resolveParams.ReversibleMode`/`.Passthrough` at both call sites in
`DlssNr_Dx12.cpp`. `DecodeDomain`/`EncodeDomain` (full RGB, exact hue-preserving peak-channel
curve) wrap the pass's base colour sample and its final written-out colour; `DecodeGreenScalar`
(a single-channel approximation, since `GatherGreen` only ever returns one channel and a
hue-preserving decode needs the full triple) feeds the edge-vote/weight heuristics only, never
the written colour. All four are identity when `ReversibleMode == 0` or `Passthrough != 0`, so
the already-working SoftKnee/Off case is untouched byte-for-byte. Recompiled via `dxc` (now found
on PATH — `create_header.py` also ran successfully this time, unlike the manual `od`/awk
workaround needed earlier in this plan). Both configs rebuilt clean, 0 errors.

**Still reported blurred after this fix** ("still blurred when using the Replace options"). Before
asking for another repro, re-read my own diff and found a real bug in it: `DecodeDomain` went all
the way through `NeutwoDecode`/`HybridDecode` to the unbounded scene-linear value, then piped that
into `LinearToSrgb` — which **saturates its input to `[0,1]` before gamma-encoding**. Any pixel
above the white point (exactly what Neutwo/Hybrid exist to represent) got crushed flat to `1.0`
*before SGSR1 ever ran its edge/sharpen math on it*, i.e. highlight detail was being destroyed by
my own fix, independent of whatever the edge-threshold theory did or didn't explain. Also
reconsidered the theory itself against the code's own comment ("Neutwo puts them ~0.076 apart" vs
SoftKnee's "~0.001 apart" — Neutwo separates highlight contrast *more* than SoftKnee, not less),
which undercuts the original "extra compression squashes edge-vote contrast" framing.

**Fix, scoped down:** `DecodeDomain`/`EncodeDomain`/`DecodeGreenScalar` now strip and reapply only
the *outer* sRGB gamma (`SrgbToLinear`/`LinearToSrgb`), landing on `NeutwoEncode(N)`/
`HybridEncode(N)` itself — a value the reversible encode already bounded to `[0,1)` — rather than
continuing on to the unbounded `N` behind it. `SrgbToLinear`/`LinearToSrgb` are genuine inverses
of each other for an input already in `[0,1)`, so this round-trips losslessly (no saturation ever
triggers), while still moving the edge-vote/weight math off the outer gamma curve. Removed the
now-dead `Neutwo*`/`Hybrid*` curve functions from `sgsr1.hlsl` entirely — nothing in the pass
touches that curve anymore, only the plain gamma wrapping it. Both configs rebuilt clean, 0
errors. **Not yet re-tested in-game** — flagged directly to the user that the edge-threshold
theory may not be the full explanation, alongside this fix.

**Still blurred a third time** ("still blurred when using the Replace options", NBA 2K screenshot,
described as "whole image like CRT" — uniform, not confined to highlights or fine detail).
Uniform-across-the-whole-image pointed away from the domain-curve theory (which predicts a
highlight-localised effect) and toward either a structural bug in the SGSR1 port's tap/coordinate
math, or something inherent to Replace mode itself. Pulled Qualcomm's actual upstream source
(`curl` against `SnapdragonGameStudios/snapdragon-gsr`'s `sgsr1_mobile.h` +
`sgsr1_shader_mobile.hlsl` directly, not from memory) and diffed it against `sgsr1.hlsl` line by
line: the `imgCoord`/`imgCoordPixel`/`coord`/`pl` derivation, the `coord.x += con1.x` mutation
before the right/upDown taps, all twelve `weightY` calls' offsets, and `SGSRGH` = `GatherGreen`
for `OperationMode 1` all match this port exactly. **The port is faithful to upstream — ruled
out.**

Asked the user a differentiating question instead of patching again: does Composed mode (not
Replace) look fine at the *same* reduced model resolution, same scene? **Yes.** That isolates the
cause structurally rather than in SGSR1's own math: Composed mode's ratio/hue blend
(`dlssnr.hlsl:959-1067`) is anchored on the native-resolution `original` frame the whole time, so
it can look sharp even when the model+SGSR1 pipeline underneath is somewhat soft. Replace mode
(`dlssnr.hlsl:1072-1079`) has no such fallback — its output is entirely the model's answer at its
*reduced working resolution*, run through SGSR1. SGSR1 makes that upscale step sharper than plain
bilinear (its whole purpose), but it cannot invent detail the model never computed at a lower
internal resolution, and unlike Composed, Replace has nothing else to lean on. **Conclusion: not a
remaining bug — an inherent resolution ceiling in Replace mode + reduced model resolution that
Composed mode structurally papers over and Replace mode cannot.** No further code change made;
reported this directly to the user rather than continuing to patch blind.

## Progress log
- 2026-09-16: plan approved by user ("execute plan"), branched off `main` `30093a4e`. Starting
  step 1.
- 2026-09-16: **branch corrected** to `feat/dlssnr-postrr-simplify-v2` (`bc2d0185`) instead of
  `main` — caught mid-Step-1-audit that `main` still has the pre-removal ResidualAcrossRR code
  and doesn't match this plan's own line citations (those were read from the postrr-simplify-v2
  branch during `PLAN_BRAINSTORM`). No commits existed yet on the branch, so repointing was
  non-destructive.
- 2026-09-16: **Step 1 audit complete**, findings below.
  - **Implicit bilinear confirmed.** `dlssnr.hlsl:263` — `SamplerState gLinear : register(s0); //
    so the edit can be read at a different size`. `EditAt()` (`dlssnr.hlsl:303-315`, "exactly as
    the resolve computes its own") samples both `gSource` (proxy) and `gModel` (answer) via
    `gLinear.SampleLevel` at the same normalized UV — this is the mechanism, confirmed by code,
    not inferred from the comment alone.
  - **Gather-format risk: real check, not a real risk.** The NR answer/proxy buffers are created
    via `CreateScratch(device, desc.Format, ...)` (`1926-1976`) where `desc.Format` is whatever
    typed format the game's own `Color` resource uses (HDR: `R16G16B16A16_FLOAT`, SDR:
    `R8G8B8A8_UNORM` typically) — never block-compressed, never typeless-without-a-view. Both
    fully support `Texture2D::Gather*` in D3D12. **Revised from the brainstorm's initial
    over-caution**: use hardware `Gather()` directly, matching Qualcomm's shader byte-for-byte,
    rather than hand-reimplementing its 4-tap neighborhood via `Load()` — manually replicating
    `Gather`'s fixed component ordering (`x`/`y`/`z`/`w` map to specific corners of the 2x2
    footprint per the D3D spec) is a real place to introduce a silent, hard-to-catch ordering bug
    with no test harness to catch it; using the real intrinsic removes that risk entirely instead
    of trading one for another.
  - **`outputNative` buffer sharing is safe.** `superDown->Dispatch(cmdList, finalAnswer,
    g_nr.outputNative)` (`DlssNr_Dx12.cpp:2889`) is a full unconditional overwrite every call, no
    blend with prior content — the `>1` comment's "UAV from last frame" (`2886`) describes the
    *allocation* persisting across frames, not a read-before-write dependency. `workScale` is a
    single scalar (never both `<1` and `>1` in the same frame), so the buffer can serve either
    leg. Park (`1917`) and release (`3711-3714`) are already unconditional on the resource
    pointer being non-null — broadening the creation gate (`1975`, currently `workScale > 1.0f`)
    to `workScale != 1.0f` needs no other lifecycle change.
  - **`resolveProxy` DOES need a change — this was the real finding.** For the `<1` case today,
    `modelInput` (and therefore `resolveProxy`, since `superDownOk` is false) resolves to
    `g_nr.colorSmall` (`2474-2536`) — the *small* proxy, not the native `colorCopy`. If the new
    SGSR1 pass only enlarges the *answer* while the proxy stays small-and-implicitly-bilinear,
    `EditAt`'s `m - p` diff would compare a sharp SGSR1-enlarged answer against a
    softer-bilinear-enlarged proxy at every native pixel — a resize-kernel mismatch that would
    inject spurious edge/ringing detail into the edit signal, not genuine model output. Fix:
    the new leg must route `resolveProxy` to `g_nr.colorCopy` (already created unconditionally at
    native size, `1927`) on success, exactly mirroring the `>1` leg's own choice — not a new
    resource, just widening the existing `superDownOk ? g_nr.colorCopy : modelInput` ternary
    (`2896`) to also cover the new leg's success case. Steps 5/6 below are written against this
    finding.

## Closing note (TASK_CLOSE)

User confirmed the linear-light downsample fix (the last change on this branch) tested OK
in-game. Composed-mode SGSR1 enlarge was separately confirmed working earlier. Replace mode's
remaining softness below 100% model resolution was traced to an inherent resolution ceiling
(Composed masks it via its native-original fallback, Replace has none) rather than a further
bug — confirmed via a direct in-game comparison (Composed fine / Replace soft at the same
resolution), not further code changes. Nothing on this branch is committed yet as of
`TASK_CLOSE` — that's a user decision outside this plan's scope.
