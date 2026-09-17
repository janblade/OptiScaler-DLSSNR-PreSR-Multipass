# Task Memory — fix/dlssnr-nr-menu-ux

Active plan: memory/plans/2026-09-17-dlssnr-v0.1.4-release.md (done)

Shipped as **v0.1.4-optimized-defaults**, published GitHub Release, built from `main` at
`9308d01a`: https://github.com/janblade/OptiScaler-DLSSNR-PreSR-Multipass/releases/tag/v0.1.4-optimized-defaults

Full chain:
- PR #13 (menu-UX-polish 8/8 + dlssnr-optimized-defaults 4/4) -- merged `a042bbb3`.
- User reported Model resolution slider not committing after PR #13 (regression: the new
  Reset button became ImGui's "last item," breaking the deferred-commit check).
- `code-review` skill on PR #13/#14 found two more issues: Highlight guard's UI cap didn't
  clamp the underlying value at 2 of 4 shader call sites; ApplyOptimizedDefaults duplicated
  an exposure-check ternary. Both fixed.
- PR #14 (regression fix + both review findings) -- merged `9308d01a`.
- User completed in-game testing themselves before authorizing the release.

Branch is fully shipped. Not yet TASK_CLOSE'd (no explicit close signal from user) -- run
TASK_CLOSE when asked, to promote anything durable to semantic memory and archive this file.
