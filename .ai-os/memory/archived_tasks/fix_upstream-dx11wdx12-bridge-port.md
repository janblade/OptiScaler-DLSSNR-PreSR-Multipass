# Task — fix/upstream-dx11wdx12-bridge-port

Plan: memory/plans/2026-09-19-port-upstream-dx11wdx12-bridge.md. Ledger: optiscaler-upstream:dx11wdx12-bridge-fixes.

## 2026-09-19 — executed, built (Release x64, exit 0), not yet run in a game

Branch cut from `main` (`3255bc02`). 8 commits, oldest first:
`9f4d22c9` 3a5c2073 input-side over-release; `7fca1b40` our own output-side twin (not upstream);
`b34d3b52` fe279327 allocation freeze; `08110ae5` 79850d93, `eca48124` f6bba067, `c373301f` 4c682650
swapchain group; `e50702bf` 5ee53e38 + `35668f79` dc7489fb (must stay together, the first breaks the build).

Skipped on purpose: ffb87936 (removes two DX11 Flush calls, behaviour change, no reported stall),
4af2417b (display only). Not applicable: c4c57a91 (already in HEAD), ef83042c (needs ResTrack_Dx11).

Pending: Release x64 build (user says when), then a DX11 bridge game (BG3) with NR on incl. resize,
fullscreen toggle, option change. Uncommitted ai-os files ride along on this branch unstaged.

## 2026-09-19 — game test 1 (Assetto Corsa + CSP), A/B against main

SRWE resize crashed on the branch AND on main (3255bc02): the game's real swapchain ResizeBuffers returns DXGI_ERROR_INVALID_CALL (0x887a0001); CSP's dx_resize_swap_chain treats it as fatal. Not caused by the port; four drag-resizes passed through the new skip path. User: treat the port as fine. Known issue left open: what holds the back buffer at SRWE resize (not traced). Not yet tested in BG3. DLLs: x64/Release/ab/ (main + branch). AC folder currently holds the MAIN dxgi.dll.

## 2026-09-19 — rename ported

`24806f15` = upstream 26aca741 (IsChanged -> IsSame; the function returns true when nothing changed). Branch is now 9 commits. Checked that upstream master still contains the other ported code unchanged; 3bc197c2 (XeFG resize mutex) left to the xefg-freeze-fg-fixes candidate. Not rebuilt after the rename.

## Closing note (TASK_CLOSE, 2026-09-19)

Closed by the user. Port recorded as `ported`, untested in a native DX11 game (BG3); the only game run was Assetto Corsa. Branch `fix/upstream-dx11wdx12-bridge-port` is 9 commits, not merged and not pushed; Release x64 was built before the last rename commit (`24806f15`). Nothing promoted to semantic memory: the only candidate facts carry a known-issue marker (what holds the back buffer at SRWE resize is untraced) and nothing has been accepted beyond "the port did not cause that crash". Open items left in the plan and ledger: BG3 test, the untraced back-buffer holder, the 4c682650 0x0-args skip semantics, and the skipped ffb87936 / 4af2417b.
