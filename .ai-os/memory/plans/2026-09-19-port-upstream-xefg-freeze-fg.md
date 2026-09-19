# Plan: Port upstream's XeFG resize/present locks, freeze detection and fake-frame-count limits

- Branch: `fix/upstream-xefg-freeze-fg-port`
- Created: 2026-09-19
- Status: in progress (executing 2026-09-19)
- Task file: memory/tasks/fix_upstream-xefg-freeze-fg-port.md
- Ledger: `optiscaler-upstream:xefg-freeze-fg-fixes`
- Source: optiscaler/OptiScaler `master` (`6f0d1fdd`), GPL-3.0, authors cdozdil and FakeMichau. Merge-base with this fork `4f17a05d`.

## Study

**What it is.** Nine upstream commits from Sep 11-18 in the frame-generation code this fork ships (`hooks/FG_Hooks.cpp`, the bridge swapchain, `IFeature`, the menu). Seven are XeFG or frame-count fixes and go together. Two are not XeFG.

| Commit | What it changes | Kind | Decision |
|---|---|---|---|
| `da427e20` (cdozdil) | `Config.cpp`: drops the `> 3` cap on `[XeFG] InterpolationCount`. `XeFG_Dx12.cpp` already clamps to `_maxInterpolationCount`, which comes from `props.maxSupportedInterpolations` (libxess_fg.dll), so the config cap was the only fixed limit. | Fix, 1 line | **Port** |
| `5c5e424d` (FakeMichau) | `menu_common.cpp`: builds the "2X/3X/…" labels from the count instead of fixed 5-entry arrays, in three combos. Needed once the limit can exceed 6X, otherwise the array is indexed out of range. | Fix, 42 lines | **Port** (with `da427e20`) |
| `04bf0b08` (FakeMichau) | `IFeature::TickFrozenCheck` takes `presentPerEval`; the frozen threshold becomes `10 * presentPerEval`. `wrapped_swapchain.cpp` passes `currentFG->GetInterpolatedFrameCount()`. With no FG the argument is 1, so nothing changes. | Fix | **Port** |
| `d817d5b4` (FakeMichau) | Same call in `Vulkan_Hooks.cpp`. | Fix | **Port** |
| `3bc197c2` (cdozdil) | New `Dx11wDx12Sync::PresentResizeMutex()`. When XeFG is active on the DX11-with-DX12 bridge, `Present` takes a shared lock and the equivalent-resize path takes the unique lock, deactivates FG and waits for the queue. Also adds `WaitForQueueIdle` and a `_real3 == nullptr` early return in `ResizeBuffers1`. Only active when `activeFgOutput == XeFG` and interop is `Dx11wDx12`. | Race fix, 110 lines | **Port** |
| `34917612` (cdozdil) | Comments out the XeFG "release swapchain backbuffers" block in `hkResizeBuffers` / `hkResizeBuffers1`. The block already ran only for XeFG. | Behaviour change, XeFG only | **Port** |
| `9df3ed0c` (cdozdil) | `FG_Hooks.cpp`: `WaitForQueueIdle` (5 s wait, logs failures) replaces four inline Signal + wait blocks; the same present/resize shared mutex for XeFG on native DX12. | Race fix, 78 lines | **Port** (with `3bc197c2`, `34917612`) |
| `2e5a8770` (FakeMichau) | Makes Nukem's the default `FGNvngxReplacement` and adds a `MessageBoxW` plus `std::exit(1)` in the Streamline init path when real DLSSG is unsupported. Not XeFG. | **Default change** for every user | **Skip** |
| `d794b18d` (cdozdil) | Input-system deadlock guards. Not XeFG. | Separate candidate | **Skip** (conflicts in 3 files; already tracked as `optiscaler-upstream:input-system-fixes`) |

**Trial.** Throwaway worktree at HEAD, cherry-picked in upstream order: 8 of 9 applied, `d794b18d` conflicted in `input_system.cpp`, `input_system_directinput.cpp` and `input_system_internal.h` (this fork's overlay-input work). Checks the text applies, not that it compiles. The worktree was removed. Every `TickFrozenCheck` override in the tree already takes the new signature after `04bf0b08`.

**Not in this plan.** Upstream `e8c9834d` (removes `JustTrackCmdList`, resource tracking) touches the same directory later but belongs to the hudfix cluster.

**Evidence.** Upstream commit messages only. No reproduction here and no report on the fork. The 3 XeFG locks fire only when XeFG is the active output, so a non-XeFG game does not exercise them at all.

**Licence.** GPL-3.0 both sides.

**Recommendation.** Port the seven. Skip `2e5a8770` because it changes a shipped default and adds a process exit for people who never use XeFG; skip `d794b18d` for its own candidate.

## Steps

1. [x] **Branch** `fix/upstream-xefg-freeze-fg-port` from `main` (`3395ee57`) on a clean tree (R9).
2. [ ] **Count limits:** `da427e20`, then `5c5e424d`.
3. [ ] **Freeze detection:** `04bf0b08`, then `d817d5b4`.
4. [ ] **XeFG locks, in order:** `3bc197c2`, `34917612`, `9df3ed0c`. One commit each. R23: `FG_Hooks.cpp` and `wrapped_swapchain.cpp` run on every FG game, so read the diff against HEAD before committing and confirm each new lock is gated on XeFG.
5. [ ] **Provenance.** `git cherry-pick -x`, rewritten to a `Ported-from: optiscaler/OptiScaler@<sha>` trailer; keep the upstream authors.
6. [ ] **Build (Release x64).** Read the log for warnings in the touched files.
7. [ ] **Decide `2e5a8770` and `d794b18d`.** Default skip, recorded in the ledger.

## Verification

- **No game needed:** Release x64 compile.
- **Needs a game, not claimed until run:** an XeFG game (Intel Arc or XeSS FG capable) resizing the window and toggling fullscreen on DX12 and, separately, DX11 through the bridge; a 3x/4x fake-frame count with an upscaler active, to see that the freeze detector no longer trips; a non-XeFG game to confirm nothing changed.

## Risks

- The new shared mutex can deadlock if a lock is taken on a thread that already holds it. Upstream added a second mutex for the bridge and a `defer_lock` guard for native DX12; check both are gated on XeFG so other FG outputs never touch them.
- `WaitForQueueIdle` returns false on a 5 s timeout and callers only log it, matching upstream.
- The freeze threshold now scales with the FG multiplier, so a genuinely frozen upscaler is reported later at 4x. Accepted upstream.
- The cherry-picks sit next to code wilsjo2 also changed, so future syncs may collide.

## Out of scope

`2e5a8770`, `d794b18d`, `e8c9834d`, and the hudfix and resource-tracking work.

## Rollback

Work on the branch; each group is its own commit and reverts alone.

## Execution log

- **2026-09-19:** studied; trial in a throwaway worktree; 7 of 9 selected. Started execution.
