# Plan: Port upstream's D3D11-on-D3D12 bridge fixes

- Branch: `fix/upstream-dx11wdx12-bridge-port`
- Created: 2026-09-19
- Status: done 2026-09-19 (9 commits on the branch; Release x64 compiled before the last rename commit). Closed by the user as ported, untested in BG3.
- Task file: memory/archived_tasks/fix_upstream-dx11wdx12-bridge-port.md (archived at close)
- Ledger: `optiscaler-upstream:dx11wdx12-bridge-fixes`
- Source: optiscaler/OptiScaler `master` (`6f0d1fdd`), GPL-3.0, authors cdozdil and FakeMichau. Merge-base with this fork `4f17a05d` (2026-08-31).

## Study

**What it is.** Ten upstream commits from Sep 2-15 that change the bridge DX11 NR runs through (`IFeature_Dx11wDx12`, `Dx11WithDx12`, the DXGI swapchain hooks). BG3 in `docs/HYBRID-V072-VALIDATION.md` is a DX11 game on this path.

**Two of the ten do not apply. Skip them:**
- `c4c57a91` (DX11 resource management). HEAD already has the same rewrite: `IFeature_Dx11::Evaluate` saves and restores GPU state with `ComPtr` ([IFeature_Dx11.cpp:46](OptiScaler/upscalers/IFeature_Dx11.cpp#L46)), which is what this commit does. Its patch conflicts because the code is already there.
- `ef83042c` (hook `ResTrack_Dx11` only on the bridge swapchain). It deletes calls to `ResTrack_Dx11::HookDevice`, and `ResTrack_Dx11` does not exist in HEAD. It belongs to the hudfix cluster.

**The eight that remain:**

| Commit | What it changes | Kind |
|---|---|---|
| `3a5c2073` | Stops `Shader_Dx11`'s destructor releasing `_currentInResource`. `InitializeSRV` stores the game's texture without `AddRef` ([Shader_Dx11.cpp:132](OptiScaler/shaders/Shader_Dx11.cpp#L132)), so the release drops a reference the shader never held. | Real bug fix, 1 line |
| `fe279327` | Splits the bridge's command-buffer count (3) from its cached-frame count (2), and replaces an infinite fence wait with a 5 s wait that logs when it blocks. | Safety fix, 49 lines |
| `79850d93` | Records the FG swapchain and its description when the bridge creates it (`currentFGSwapchain` already exists in `State.h`). | Bookkeeping |
| `f6bba067` | FSR FG reads its swapchain description from the FG swapchain when it has one. | Bookkeeping, 2 lines |
| `4c682650` | `IsChanged` check so the FG swapchain is not resized needlessly. | Fix, 42 lines |
| `5ee53e38` | Better adaptation of the DX11 swapchain description to DX12 (179 lines in `DxgiFactory_Hooks.cpp`). **It introduces a stray `)` on a `LOG_ERROR` line and does not compile without `dc7489fb`.** | Behaviour, large |
| `dc7489fb` | Removes that stray `)`. Listed under the stability candidate upstream but only exists to repair `5ee53e38`, so it goes here. | Compile fix, 1 line |
| `ffb87936` | Comments out two `Flush()` calls in the bridge's DX11 to DX12 sync ("games flush should be enough, for now"). | **Behaviour change, not a bug fix** |
| `4af2417b` | Adds the interop cost to the upscaler time shown for DX11 with DX12. Symbols it needs (`GpuTime_Dx11`, `ScopedGpuTime_Dx11`, `includedInUpscalerTime`) exist in HEAD. Nothing in our NR or menu code calls the API it changes. | Display only |

**Trial.** In a throwaway worktree at HEAD, cherry-picking the ten in upstream order applied 8 cleanly and conflicted on the two above. That checks the text applies, not that it compiles or works. Nothing has been built.

**Related loose end.** `3a5c2073` fixes the input side only. The destructor still releases `_currentOutResource`, and `InitializeUAV` does not `AddRef` either, so the same over-release may remain for the output texture. Not verified; look at it before closing.

**Evidence.** Upstream's own commit messages only. No reproduction here, and no report of any of this on the fork. Your Cyberpunk logs are a native DX12 game, so they say nothing about the bridge.

**Licence.** GPL-3.0 both sides.

**Recommendation.** Port the safety fixes (`3a5c2073`, `fe279327`), the swapchain bookkeeping and the pair `5ee53e38` + `dc7489fb` as one unit. Treat `ffb87936` as a separate decision: it changes GPU synchronisation on the path your DX11 validation game uses and nobody has reported a stall. `4af2417b` is optional display polish.

## Steps

1. [x] **Branch** `fix/upstream-dx11wdx12-bridge-port` from `main`. Clean tree first (R9): commit or set aside `tests/nr_interpass_clamp_smoke.cpp` and the ai-os port skill.
2. [x] **`3a5c2073`.** One line in `Shader_Dx11.cpp`. Then read `InitializeUAV` and decide whether `_currentOutResource` has the same problem; if yes, fix it in its own commit.
3. [x] **`fe279327`.** Including the `Util.h` include and the two new counts in the header. Check `Util::MillisecondsNow` is used the way HEAD defines it. *(Declared in `Util.h:36`; the header defines `DX11WDX12_COMMAND_BUFFER_COUNT 3` and no use of the old `DX11WDX12_NUM_OF_BUFFERS` remains.)*
4. [x] **Swapchain group, in order:** `79850d93`, `f6bba067`, `4c682650`.
5. [x] **Pair: `5ee53e38` then `dc7489fb`.** Never land the first without the second. This is 179 lines in the swapchain creation hooks, which every DX11-with-DX12 game goes through, so give it its own commit and its own review pass (R23, wide blast radius).
6. [x] **Decide `ffb87936` and `4af2417b` separately.** Default: skip `ffb87936` unless a DX11 stall is reported; take `4af2417b` only if the DX11 upscaler-time display matters to you.
7. [x] **Provenance.** Trailer per commit `Ported-from: optiscaler/OptiScaler@<sha>`, keep cdozdil and FakeMichau as authors where a cherry-pick keeps them, and add the upstream authors' line to `docs/CREDITS.md` if not there.
8. [~] **Build (Release x64)** *(built 2026-09-19 20:27, exit 0, 0 errors, no warnings in the ported files; game test still to do)* when told, and read the log for the new `Dx11wDx12 allocator wait` warnings.

## Verification

- **No game needed:** Release x64 compile. Nothing here has a game-free test.
- **Needs a game, not claimed until run:** a DX11 game through the bridge (BG3 per `docs/HYBRID-V072-VALIDATION.md`) with NR on, including a window resize, a fullscreen toggle and an option change. Check that the swapchain still presents and that the log shows no new errors.

## Risks

- `5ee53e38` changes swapchain creation for every bridge game.
- `fe279327` replaces an infinite wait with a 5 s one. If a wait ever legitimately exceeds that, behaviour changes from a stall to an error path.
- The cherry-picks conflict with nothing today but sit next to code wilsjo2 also changed, so future syncs with his branch may collide.

## Out of scope

`c4c57a91`, `ef83042c`, the DX11 hudfix and resource tracking, and anything in the hudfix cluster.

## Rollback

Work on the branch; each group is its own commit and reverts alone.

## Execution log

- **2026-09-19:** cut `fix/upstream-dx11wdx12-bridge-port` from `main`; 8 commits (see `memory/tasks/fix_upstream-dx11wdx12-bridge-port.md`). Step 2's second half found the same over-release on `_currentOutResource` (set by `InitializeUAV` without `AddRef`, released in the destructor) and fixed it in its own commit, `7fca1b40`. That one is our change, not upstream's. Step 6 decision: skipped `ffb87936` and `4af2417b`. Step 8: Release x64 build passed the same evening (exit 0, 0 errors); the game test is still to do.
- **2026-09-19, game test 1 (Assetto Corsa, DX11 through the bridge, FSR FG, NR before SR, resizing with Simple Runtime Window Editor):** the branch build crashed on an SRWE resize. Debug log: `ResizeBuffers results: real 887A0001, fg 887A0005` (`DXGI_ERROR_INVALID_CALL` from the game's real swapchain; the FG value is just the default because FG was not attempted). Four earlier resizes in the same session with identical arguments went through the new `Skipping FG ResizeBuffers` path and succeeded. **A/B on `main` (3255bc02, no port):** crashed the same way. Custom Shaders Patch's own `dx_hooks::dx_resize_swap_chain(width, height)` failed with `0x887a0001` and it shows a crash dialog. So the crash is not caused by the port. Likely cause (not traced): something still holds a back-buffer reference at resize. Left as a known issue. **User decision:** treat the port as fine for this scenario. Still not tested in a native DX11 game such as BG3, and the `ab\` DLLs are kept in `x64\Releaseb\`.
- **2026-09-19:** added upstream `26aca741` (`IsChanged` to `IsSame`, 3 lines) as `24806f15`. Verified against upstream master `6f0d1fdd` that the ported code is still present there; only that rename followed `4c682650`. Not rebuilt.
- **2026-09-19, PORT_CLOSE / TASK_CLOSE:** user closed the task. Ledger entry `optiscaler-upstream:dx11wdx12-bridge-fixes` set to `ported`, untested in BG3. Task file archived to `memory/archived_tasks/`. Branch not merged, not pushed. Nothing promoted to semantic memory.
