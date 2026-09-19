# Task — fix/upstream-xefg-freeze-fg-port

Plan: memory/plans/2026-09-19-port-upstream-xefg-freeze-fg.md. Ledger: optiscaler-upstream:xefg-freeze-fg-fixes.

## 2026-09-19 — planned, executing

Branch cut from `main` (`3395ee57`). Seven commits selected: da427e20, 5c5e424d, 04bf0b08, d817d5b4, 3bc197c2, 34917612, 9df3ed0c. Skipped by default: 2e5a8770 (Nukem's default + MessageBox/exit), d794b18d (input system; conflicts; own candidate). Trial in a throwaway worktree: 8 of 9 applied, d794b18d conflicted.

## 2026-09-19 — ported and built

7 commits, authors kept, `Ported-from:` trailers: `2ec0ee59` da427e20, `561fc397` 5c5e424d, `e46864a6` 04bf0b08, `f3bd70d2` d817d5b4, `b2ad6d4b` 3bc197c2, `5e8fb632` 34917612, `df8879f7` 9df3ed0c. Release x64 built, exit 0. Skipped: 2e5a8770, d794b18d. Note: native DX12 `FGHooks::_resizeMutex` is only ever shared-locked upstream, so it excludes nothing; the bridge mutex is real.

Pending: push and PR (user says when); XeFG game to exercise the locks (DX12 and DX11 bridge resize / fullscreen toggle); a 3x/4x fake-frame count for the freeze detector.
