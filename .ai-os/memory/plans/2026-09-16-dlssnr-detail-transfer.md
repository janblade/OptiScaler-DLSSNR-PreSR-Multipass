# Plan: Hybrid detail transfer -- frequency-split NR composition (new mode 5)
- Branch: feat/dlssnr-detail-transfer
- Created: 2026-09-16
- Status: approved
- Task file: memory/tasks/feat_dlssnr-detail-transfer.md

## Context

Every existing `DlssNrReversibleMode` picks one blend strategy for the whole image: Composed
(0/1/3) rescales the model's answer by a single luminance ratio anchored to the native original;
Replace (2/4) shows the model's raw decoded answer directly, no anchoring at all. Neither
separates "what the model is actually good at" (high-frequency denoise/detail) from "what needs
grounding against native" (low-frequency tone/exposure).

Mode 5 does: **low-frequency always from `original` (native), high-frequency always from the
model's own resolved answer.** `result = lowpass(original) + strength * (modelAnswer -
lowpass(modelAnswer))`. When `modelAnswer` and `original` agree everywhere within the low-pass
radius (no real edit), `lowpass(modelAnswer) == lowpass(original)` and the formula collapses to
`result = original` exactly -- same identity-preserving invariant the rest of this file already
holds itself to (e.g. Matched Residual's "shipped configuration cannot be changed by it at all").

**Acceptance criteria:**
- New "Hybrid detail transfer" option in the HDR mapping combo (`DlssNrReversibleMode == 5`),
  existing 0-4 untouched byte-for-byte (verify: diff the resolve output at each existing mode
  before/after this change, same seed/scene, must be identical).
- A `DetailTransferStrength` slider (default 1.0), same UI pattern as `ReplaceDetailStrength`.
- Mode 5 works at any model resolution (100% and reduced), independent of the
  Bilinear/SGSR1/SGSR1-both enlarge choice from the other in-flight branch -- this composition
  step reads whatever `resolveAnswer`/`resolveProxy` the existing pipeline already selected, no
  new coupling.
- Debug + Release x64 build clean, 0 new warnings.
- In-game: mode 5 doesn't flicker the way Replace can (tone is always native-anchored), shows
  visibly more fine detail than plain Hybrid Composed on the same scene, and doesn't introduce
  colour shift Composed sometimes has (chroma comes from the model in both, but grounding the
  *tone* more tightly than Composed's single luma ratio may reduce it -- observe, don't assume).

**Out of scope (deliberate v1 scope cuts):**
- Combining with SoftKnee/Neutwo curves -- mode 5 hardcodes Hybrid's decode, mirroring how modes
  3/4 are already Hybrid-specific. Making the composition strategy orthogonal to curve choice
  (`DlssNrReversibleMode` becoming a 2-axis curve x strategy setting) is a bigger structural
  change, not needed to validate the core idea.
- Tunable low-pass radius -- fixed small constant for v1 (candidate: 2, matching the low end of
  `ReplaceDetailStrength`'s existing radius range). Expose as a slider later only if the fixed
  value proves too coarse/fine across tested content.
- Detail sourced from the edit/residual (`model - proxy`) instead of the model's own answer --
  conceptually closer to "transfer only what changed" but depends on proxy being on the same
  detail basis as the model (the mismatch bug class from `feat/dlssnr-sgsr1-upscale`), more
  moving parts for a first version.
- Multi-band (Laplacian pyramid) split -- a simple 2-band split first; only add more bands if
  the simple version's single "detail" band isn't enough on real content.

## Steps

1. [ ] `shaders/dlssnr/DlssNr_Common.h`: add `float DetailTransferStrength;` trailing field to
   `DlssNrConstants`. Confirm `static_assert(sizeof(DlssNrConstants) == 256)` still holds
   (spare `alignas(256)` padding budget confirmed present as of the last two field additions
   this session). -- verify: compiles, assert doesn't fire.
2. [ ] `Config.h`/`Config.cpp`: add `CustomOptional<float> DlssNrDetailTransferStrength { 1.0f
   };` near `DlssNrReplaceDetailStrength`, with load/save (`readFloat`/`ini.SetValue`,
   `"DlssNr"`/`"DetailTransferStrength"`). -- verify: compiles; ini round-trips the key.
3. [ ] `shaders/dlssnr/precompile/dlssnr.hlsl`: implement the mode-5 branch, placed alongside
   the existing Replace decode (~`gReversibleMode == 2`/`== 4` block, before the shared
   Replace-only detail-injection block since that's Replace-specific and must stay gated to
   2/4 only):
   - `float3 modelAnswer = gPassthrough != 0 ? modelDirect : HybridDecode(modelDirect);` --
     reuses the already-captured `modelDirect` (the raw model sample from before Matched
     Residual's rewrite, same basis Replace already uses).
   - Low-pass of `original`: reuse the existing plus-shaped 4-neighbour averaging pattern
     already established by the Replace detail-injection code (`gOriginal.Load` at
     `+-radius` offsets on X and Y, averaged with the centre sample), fixed `radius = 2`.
   - Low-pass of `modelAnswer`: same shape, but reading `gModel` via `SampleLevel(gLinear,
     uvq, 0)` at UV offsets (not `.Load`) so it's correct whether `gModel` is native-sized
     (SGSR1-enlarged) or still small (bilinear-implicit) -- mirrors `EditAt()`'s own
     size-agnostic sampling. Apply the same `SrgbToLinear`/`HybridDecode` domain conversion
     to each neighbour tap as the centre sample.
   - `result = lowOriginal + gDetailTransferStrength * (modelAnswer - lowModelAnswer);`
   - Add `gDetailTransferStrength` to the cbuffer variable declarations at the top of the
     resolve function (mirrors `gReplaceDetailStrength`/`gModelWorkScale`).
   -- verify: shader compiles via dxc; mode 5 selectable without crashing.
4. [ ] `shaders/dlssnr/DlssNr_Dx12.cpp` and `dlssnr/DlssNrFeature_Vk.cpp`: wire
   `resolveParams.DetailTransferStrength = cfg.DlssNrDetailTransferStrength.value_or_default();`
   (DX12) and the equivalent `encode.DetailTransferStrength = ...` (VK, flows into `resolve`
   via the existing `DlssNrConstants resolve = encode;` copy). -- verify: compiles both
   backends.
5. [ ] `dlssnr/DlssNr_Menu.cpp`: add `"Hybrid detail transfer"` as a 6th entry in
   `reversibleNames`, update the `reversible > 4` clamp to `> 5`, add a `DetailTransferStrength`
   slider (with a `Reset##detailtransfer` button, default 1.0) gated on `reversible == 5`,
   update the "HDR mapping" HelpMarker to describe the new option. -- verify: combo shows 6
   options; slider only enabled/visible when mode 5 is selected.
6. [ ] Recompile `dlssnr.hlsl` for both DX12 (`dxc.exe` -> `DlssNr_Shader.cso`/`.h`) and Vulkan
   (existing VK build step -> `DlssNr_Shader_Vk.h`/`.spv`), matching the two-target workflow
   already used for this file's prior edits this session. -- verify: both regenerate without
   compiler errors.
7. [ ] Debug + Release x64 full build. -- verify: 0 errors, 0 new warnings vs baseline.
8. [ ] In-game check: same scene, same model resolution, cycle Hybrid Composed / Hybrid Replace
   / Hybrid detail transfer. Confirm mode 5 doesn't flicker like Replace, shows more fine detail
   than Composed, and behaves sensibly across at least one reduced-resolution case (whichever
   enlarge method is active from the other in-flight branch). Confirm existing modes 0-4 are
   visually/behaviorally unchanged. -- verify: manual play-test, screenshots if a real
   difference needs confirming (as with the SGSR1-vs-Bilinear comparison earlier this session).
9. [ ] `TASK_CLOSE`: if the idea holds up, record it in `architecture_overview.md` as a real
   alternative composition strategy (not just Composed/Replace); if it doesn't hold up, record
   why in `known_gotchas.md` so the frequency-split idea isn't re-proposed blind next time.

## Notes

This is the first mode to combine tone-from-native with detail-from-model as its *primary*
mechanism rather than a bolt-on (`ReplaceDetailStrength` is a narrow, Replace-only version of
the same underlying idea, added earlier this session). If mode 5 clearly wins over both Composed
and Replace in testing, a natural follow-up (not started here) is asking whether Replace's own
detail injection should be reimplemented in terms of this same low/high split instead of its
current fixed-radius box-blur high-pass -- deliberately not conflated with this plan.
