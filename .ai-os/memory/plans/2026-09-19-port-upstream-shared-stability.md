# Plan: Port upstream's small crash and safety fixes

- Branch: `fix/upstream-shared-stability-port`
- Created: 2026-09-19
- Status: done 2026-09-19 (4 guards merged as PR #24, `2083a1dd`; built, untested in a game; exports group, `bd6407da`, `b52cf663` skipped on purpose)
- Task file: memory/archived_tasks/fix_upstream-shared-stability-port.md
- Ledger: `optiscaler-upstream:shared-stability-fixes`
- Source: optiscaler/OptiScaler `master` (`6f0d1fdd`), GPL-3.0. Merge-base with this fork `4f17a05d`.

## Study

**What it is.** Twelve small upstream commits from Sep 1-16 in code this fork shares. One is a latent use-after-free, several are guards, one is a set of DXGI export changes, and a couple are not really stability fixes.

| Commit | What it changes | Kind | Notes |
|---|---|---|---|
| `7dbc379d` (FakeMichau) | `HC_Dx12.cpp` and `RUI_Dx12.cpp`: `scBuffer->Release()` runs, then `scBuffer` is used for the copy and barriers. Switched to `ComPtr` so it is released at the end. | **Latent use-after-free** (upstream calls it "potential") | The buffer stays valid only while the swapchain holds its own reference, which is normally true but not guaranteed, for example during a resize on another thread. Plain `git apply` fails on whitespace, so check line endings when applying by hand. |
| `f740a763` | `D3D11_Hooks.cpp`: moves a `static const D3D_FEATURE_LEVEL levels[]` out of the `if` it was used after. | Small bug fix | Trivial. |
| `4bd61744` | `GpuTime_Dx12` constructor returns early with a log when the device is null. Our NR timing uses this class. | Guard | 6 lines. |
| `7168655f` | `GpuTime_Dx12::ReadGpuTime` clears `_trigger[previousFrameIndex]` so a shader cost stops showing after it stops running. | Fix | 1 line. |
| `1ec11b8c` | `FSRFG_Dx12::DispatchCallback` gets the D3D12 device from the command list or swapchain instead of trusting the cached `_device` when creating the HUD helper shaders. | Guard | 28 lines. |
| `bd6407da` | `Util::FindFilePath`: when a file sits under a `Win64` folder, checks one and two levels up for `Binaries` before picking the game root, for old UE games. | **Behaviour change** | Our Streamline proxy uses `FindFilePath` for the `nvngx_*.dll` search paths. |
| `b4924e30`, `32f5f3db`, `42dc02af`, `b0dd6b02` | DXGI exports: HEAD has `_ApplyCompatResolutionQuirking`, `_CompatString` and `_CompatValue` only as empty stubs in [dxgi.h](OptiScaler/exports/dxgi.h#L72). These commits add them to `Source.def`, implement them, and finally remove `LOG_FUNC()` from the exports ("to prevent crashes at W11"). | **Group of four, interdependent** | `b0dd6b02` fails alone; it needs `42dc02af`. Roughly 230 lines, process-wide. |
| `b52cf663` | Input system: uses `KernelBaseProxy::GetProcAddress_()` instead of `GetProcAddress` in 5 files. | Not a crash fix | Optional. |
| `dc7489fb` | Stray `)` typo fix. | Compile fix | **Goes with the bridge plan**, because it repairs `5ee53e38`. Do not apply it here alone. |

**Trial.** In a throwaway worktree at HEAD, 11 of the 12 applied in upstream order (the twelfth is `dc7489fb`, above). That checks the text applies, not that it compiles. Nothing has been built.

**Evidence.** Upstream commit messages. No reproduction here and no report on the fork. `7dbc379d` is the only one I can confirm as a code defect by reading it, and it is a latent one.

**Licence.** GPL-3.0 both sides.

**Recommendation.** Stage it and stop where the value stops:
1. `7dbc379d` on its own. It is a defect visible in the source, though I have no evidence it has crashed anyone.
2. The small guards (`f740a763`, `4bd61744`, `7168655f`, `1ec11b8c`). Low risk.
3. The DXGI exports group only if you want it. It is the largest and riskiest piece and the one with the least evidence for your builds.
4. Skip `bd6407da` unless a UE game is failing to find its NGX DLLs, and skip `b52cf663`.

## Steps

1. [x] **Branch** `fix/upstream-shared-stability-port` from `main`, on a clean tree (R9).
2. [x] **`7dbc379d`.** *(Already in HEAD as `ComPtr`, cherry-pick was empty and was skipped. The Study's 'applied in trial' was wrong.)* Apply by hand if patching fails on whitespace. Both files. Confirm no other use of `scBuffer` after the block.
3. [x] **Guards:** `f740a763`, `4bd61744`, `7168655f`, `1ec11b8c`, one commit each.
4. [ ] **Decide the DXGI exports group.** If yes, apply `b4924e30`, `32f5f3db`, `42dc02af`, `b0dd6b02` in that order as one commit series, and read how `exports/dxgi.h` and `Source.def` differ in HEAD first (R23: this is process-wide).
5. [ ] **Decide `bd6407da` and `b52cf663` separately.** Default skip.
6. [x] **Provenance.** `Ported-from: optiscaler/OptiScaler@<sha>` trailers, upstream authors credited.
7. [x] **Build (Release x64).** *(2026-09-19 22:40, exit 0, 0 errors, no warnings in the ported files; game test still to do)*

## Verification

- **No game needed:** Release x64 compile. For the exports group, confirm the built DLL still exports what `Source.def` lists.
- **Needs a game:** any DX12 title with hudless compare or the UI render option on for `7dbc379d`; an FSR FG game for `1ec11b8c`; a load test of the renamed `dxgi.dll` proxy on Windows 11 for the exports group. None of these is checked here.

## Risks

- The exports group affects every game that loads OptiScaler as `dxgi.dll`.
- `bd6407da` changes which folder is treated as the game root.
- Cherry-picks may not match line endings in our files.

## Out of scope

`dc7489fb` (bridge plan), the input-system and hudfix work, and anything not in this candidate.

## Rollback

Work on the branch; each step is its own commit and reverts alone.

## Execution log

- **2026-09-19:** cut `fix/upstream-shared-stability-port` from `main` (`98dc3d78`). `7dbc379d` is already in HEAD (both files use `ComPtr<ID3D12Resource> scBuffer`, from `3d083723`); `git cherry-pick` came out empty and was skipped. Ported one commit each, authors kept, `Ported-from:` trailers: `e650e07f` f740a763, `183465d9` 4bd61744, `1a3bb535` 7168655f, `552d1519` 1ec11b8c. Read the diffs: `GpuTime_Dx12` methods already check `_init`, so the early return is safe; `1ec11b8c` also resets `_hudCopy` / `_hudlessCompareCompute` in `ReleaseObjects`. Steps 4-5 left at default (skip) pending the user's call. Not built.
- **2026-09-19, build:** user lifted the no-auto-build rule. First Release x64 attempt hung (orphaned cl.exe, killed); rebuild exit 0, 0 errors, only pre-existing C4250 warnings, none in FSRFG_Dx12 / GpuTime_Dx12 / D3D11_Hooks. `x64/Release/a/OptiScaler.dll` 22:40:51. No game run.
- **2026-09-19, PORT_CLOSE:** PR #24 merged to main (`2083a1dd`). Ledger `optiscaler-upstream:shared-stability-fixes` set to `ported`, untested in a game. Steps 4-5 closed as skip. Task file archived to `memory/archived_tasks/`.
