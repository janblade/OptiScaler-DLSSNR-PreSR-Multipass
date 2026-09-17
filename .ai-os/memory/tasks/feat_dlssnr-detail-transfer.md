# Task Memory — feat/dlssnr-detail-transfer

Branched off `main` (`2e49472a`, clean tree), 2026-09-16. `feat/dlssnr-upscale-method-toggle`'s
in-progress work (steps 1-12 done, awaiting the user's in-game test) is stashed
(`stash@{0}`, "wip: dlssnr-upscale-method-toggle, SGSR1 edge threshold/sharpness tunables,
awaiting in-game test") rather than mixed into this branch -- resume it with
`git checkout feat/dlssnr-upscale-method-toggle && git stash pop`.

Active plan: memory/plans/2026-09-16-dlssnr-detail-transfer.md (approved, 0/N)

## Context

User asked, exploratory: "do you have your own idea [for] the best way to merge [the NR edit]
back?" Prior investigation this session established NVIDIA provides no spec for this -- the
whole SoftKnee/Neutwo/Hybrid curve + Composed/Replace composition system in `dlssnr.hlsl` is
this project's own invention, arrived at through in-game A/B testing.

Proposed idea: split the merge into frequency bands instead of one blend mode for the whole
image. The model's actual value-add is high-frequency (it's a denoiser); the thing needing
grounding against native is low-frequency (overall exposure/tone -- native is always correct
there). Composed already does a crude single-scalar version of this for luma only; Replace does
none of it. A real split: low-frequency always from `original` (native), high-frequency always
from the model's own resolved answer -- dissolves the Composed-vs-Replace binary into one
"detail transfer strength" dial, and should reduce both flicker and colour-shift simultaneously
since both are symptoms of letting the model's low-frequency output drift from native.

Brainstormed via `AskUserQuestion` (all three answered "Recommended"):
- **New mode, not a replacement.** Lands as `DlssNrReversibleMode == 5`, existing 0-4 untouched
  -- zero risk to Composed/Hybrid, which the user already confirmed work well.
- **Detail source: the model's own high-frequency**, not the edit/residual (`model - proxy`).
  Simpler, reuses the same box-blur high-pass shape as the existing `ReplaceDetailStrength`
  feature; risk (accepted) is it also transfers anything the model hallucinated, not just
  genuine denoise detail.
- **Simple 2-band split for v1**, not a Laplacian pyramid. One low-pass (fixed small radius,
  not tunable in v1), high = source minus its own low-pass.

Design decision made unilaterally (not asked, to avoid a third brainstorm round; flagged for
review at plan approval): mode 5 hardcodes the Hybrid curve for its decode, rather than trying
to make the new composition strategy combine with all three curves (SoftKnee/Neutwo/Hybrid) --
that would need `DlssNrReversibleMode` to become a 2-axis (curve x strategy) setting, a bigger
structural change than a first version needs. Mirrors how mode 3/4 are already "Hybrid"-specific.
