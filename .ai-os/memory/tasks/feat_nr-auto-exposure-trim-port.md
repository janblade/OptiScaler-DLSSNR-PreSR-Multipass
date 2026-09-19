# Task — feat/nr-auto-exposure-trim-port

Plan: memory/plans/2026-09-20-port-wilsjo2-pr77-auto-exposure-trim.md. Ledger: wilsjo2-fork:pr-77-auto-exposure-trim.

## 2026-09-20 — planned

Studied wilsjo2 PR #77 (mattjaas, `59855487`): automatic exposure from the HDR frame (source 3), Trim range to 50x, Trim Anchor Points for Game and Automatic exposure, D3D12 and Vulkan. It is written against v0.8.4's split DX12/Vulkan files, which `main` does not have, so it is a hand adaptation onto `DlssNr_Dx12.cpp`, `DlssNrFeature_Vk.cpp`, `DlssNr_Menu.cpp`. Four commits planned: Trim/Anchors, D3D12 auto exposure, Vulkan auto exposure, regenerated shader binaries.

Pending: the user's go-ahead to execute; branch not cut yet.
