# Task — feat/nr-auto-exposure-trim-port

Plan: memory/plans/2026-09-20-port-wilsjo2-pr77-auto-exposure-trim.md. Ledger: wilsjo2-fork:pr-77-auto-exposure-trim.

## 2026-09-20 — planned

Studied wilsjo2 PR #77 (mattjaas, `59855487`): automatic exposure from the HDR frame (source 3), Trim range to 50x, Trim Anchor Points for Game and Automatic exposure, D3D12 and Vulkan. It is written against v0.8.4's split DX12/Vulkan files, which `main` does not have, so it is a hand adaptation onto `DlssNr_Dx12.cpp`, `DlssNrFeature_Vk.cpp`, `DlssNr_Menu.cpp`. Four commits planned: Trim/Anchors, D3D12 auto exposure, Vulkan auto exposure, regenerated shader binaries.

Pending: the user's go-ahead to execute; branch not cut yet.

## 2026-09-20 — executed and built

Branch `feat/nr-auto-exposure-trim-port`, six commits after the plan commit (`f1e7ca9b`, `8515b5f4`, `3ebd6f50`, `66c1beca`, `69fcf9e4`, `0a4e85d4`), not pushed. Release x64 built (exit 0); `x64/Release/a/OptiScaler.dll` 00:51. Vulkan shader smoke tests pass on the new SPIR-V, including a new automatic-exposure test. Details and the deliberate differences from the PR are in the plan's execution log.

Pending: the user looks at it in a game; push and PR when they say. To check in a game: pick "Automatic exposure from HDR frame" as the White point source on a linear-HDR NR game (D3D12, and Vulkan if there is one), watch the "Automatic exposure ... -> white point" line follow the lighting, Add/Preview Trim anchors, and toggle NR at a fixed spot to see the exposure does not chase its own output. Game exposure and Scanned exposure should look as before.
