# Task — fix/upstream-shared-stability-port

Plan: memory/plans/2026-09-19-port-upstream-shared-stability.md. Ledger: optiscaler-upstream:shared-stability-fixes.

## 2026-09-19 — guards ported, not built

Branch cut from `main` (`98dc3d78`). `7dbc379d` already in HEAD (empty cherry-pick, skipped). 4 commits: `e650e07f` f740a763 (D3D11 feature-level array scope), `183465d9` 4bd61744 (GpuTime_Dx12 null device), `1a3bb535` 7168655f (shader cost clears), `552d1519` 1ec11b8c (FSR FG callback device).

Left undecided/untouched: DXGI exports group (b4924e30, 32f5f3db, 42dc02af, b0dd6b02), bd6407da, b52cf663. Pending: Release x64 build (user says when); FSR FG game to exercise 1ec11b8c.
