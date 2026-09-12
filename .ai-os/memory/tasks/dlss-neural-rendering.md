# Task Memory — dlss-neural-rendering

Branch working notes. Closed via `TASK_CLOSE`, not `MEMORY_CONSOLIDATE` (feature branch).

## 2026-09-06 — AI OS Framework installed
- AI OS Framework installed on this branch via the Agentic Installer.

## 2026-09-12 — Multi-pass boiling-on-motion fix
- Two independent findings, two separate plans, prioritized:
  1. (priority 1) Pass N's raw model output is fed to pass N+1 with no clamp, unlike the
     once-per-frame resolve which already needed one for an identical real bug (Nioh 3
     flicker). Matches the reported symptom without needing any resolution change.
     Active plan: memory/plans/2026-09-12-dlssnr-multipass-interpass-clamp.md (in-progress, 4/5 —
     code done, both configs build clean; step 4's in-game visual check still needed)
  2. (priority 2, on hold pending DRS confirmation) A DRS resolution tick tears down every
     extra-pass NGX feature and rebuilds them one per frame, ramping visible processing
     strength over several frames. Only fires if Dynamic Resolution Scaling is active.
     Active plan: memory/plans/2026-09-12-dlssnr-multipass-resolution-rebuild-ramp.md (approved, 0/4)
