# Task Memory — feat/dlssnr-sgsr1-upscale

Branched off `main` `30093a4e`, 2026-09-16. Unrelated to `feat/dlssnr-postrr-simplify-v2`
(committed/pushed separately, e8e89480/bc2d0185) — kept on its own branch on purpose so the
two features don't end up conflated in one diff.

Active plan: memory/plans/2026-09-16-dlssnr-sgsr1-upscale.md (done, 10/10)

## Context

User asked for SGSR1 (Qualcomm's Snapdragon Game Super Resolution, single-pass, BSD-3-Clause)
to enlarge DLSS-NR's model answer back to native whenever `DlssNrWorkingScale`/
`ModelResolutionAuto` ("model resolution") is below 100% — replacing today's implicit
HW-bilinear tap in the resolve pass with a real edge-directed upscale, the same way the
`workScale > 1.0` case already gets a real filter via `superDown`. Always-on when reduced (no
new UI), DX12 only, written from scratch (no local SGSR1 source existed in this repo before
this plan — the actual shader math was pulled from Qualcomm's official repo during
`PLAN_BRAINSTORM` and is cited in the plan file).

See the plan file for the full step list and the Step 1 audit findings once that runs.
