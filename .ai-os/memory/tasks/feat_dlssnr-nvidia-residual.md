# Task Memory — feat/dlssnr-nvidia-residual

Branch working notes for the NVIDIA residual transfer path.

## Branch state
- Active branch: `feat/dlssnr-nvidia-residual`
- Current working tree: dirty, with AI-OS memory files and `.vscode/` modified/untracked
- Do not force-clean or force-push without explicit approval

## Feature goal
Implement and validate the OkLab residual enlarge path for below-frame-size model output.

## Relevant code
- `OptiScaler/shaders/dlssnr/precompile/dlssnr.hlsl`
  - `NvidiaResidualModel(float3 fullProxy, float2 uv)`
  - `gTransfer == 2` path for NVIDIA residual
- `OptiScaler/shaders/dlssnr/DlssNr_Common.h`
  - transfer-mode contract and mode definitions
- `OptiScaler/dlssnr/DlssNr_Menu.cpp`
  - Upcale Mode enum and UI labels (`Classic`, `Matched residual`, `NVIDIA residual`)

## Current status
- NVIDIA residual model math already appears implemented in the shader and the menu/defaults are set to expose it.
- This branch should be treated as a validation-and-polish pass, not a blank slate.
- The remaining work is to confirm build correctness, output behavior, and any edge-case regressions before calling it ready.

## Ready checklist
1. Confirm the branch contains the expected transfer-mode logic and no incomplete debug scaffolding.
2. Rebuild the project and ensure the DLSSNR shader path compiles cleanly in the active configuration.
3. Check the `DlssNrTransfer` logic around `gTransfer == 2` for any missing default or fallback cases.
4. Validate the NVIDIA residual mode against the output path that uses the reduced-size model output and ensure it is not overweighting the native frame.
5. Confirm the menu/default selections remain coherent with the underlying transfer-mode semantics.
6. If the validation passes, prepare a concise summary and decide whether to commit or continue refining.

## Guardrails
- Keep scope tight to the residual enlarge path.
- No broad refactors before validation.
- Keep the residual formulas as written; do not silently diverge from them.
- This branch is not ready to merge or push without a final validation pass and explicit user approval.
