# Progress — Evolution Log

> Living dashboard of proposed/applied AI-OS self-evolutions (`EVOLVE_PROPOSE`/`EVOLVE_APPLY`,
> `rules/evolution_policy.md`). This file did not exist before EP-3 — `manifest.json`'s
> `evolution_history` shows `total_evolutions: 2`, `last_evolution: "EP-2"`, but no record of
> EP-1/EP-2 was found in `decisions.jsonl` or `decisions.archive.jsonl` either. Not
> reconstructed/guessed at (R21) — numbering below continues from the manifest's counter as the
> least-bad option, with this gap noted rather than silently papered over.

---

## Evolution Proposal: EP-3
- **Date**: 2026-09-16
- **Type**: skill_update
- **Target**: `registry/core.memory.sk/SKILL.md` — `TASK_CLOSE` procedure
- **What**: Add a new step (5a, after Archive and before Prune) that, when the resolved
  branch has uncommitted changes, summarizes what would be committed (files, a proposed
  message drawn from the task/plan file) and the push target, then asks for one explicit
  confirmation covering both commit and push before doing either. Declining, or nothing to
  commit, skips silently. Never `--force`/`--no-verify`, never bypasses the existing
  protected-branch confirmation requirement.
- **Why**: `TASK_CLOSE` currently closes out memory tracking (archive the task file, promote
  verified facts) but says nothing about the code changes themselves — twice this session
  (`feat/dlssnr-postrr-simplify-v2` and `feat/dlssnr-sgsr1-upscale`) a task reached
  `TASK_CLOSE` with the underlying work still fully uncommitted, leaving "the task is closed"
  and "the work is durably saved" silently out of sync. User requested this directly
  (`EVOLVE propose task close should include commit and push with confirmation`).
- **Risk**: medium (workflow structural change to an existing, frequently-used command;
  git push touches shared/remote state, though the change adds a confirmation gate rather
  than removing one)
- **Rollback Plan**: revert the `SKILL.md` edit to its pre-EP-3 content (file is
  git-tracked; restore via `git diff`/`git checkout` against the commit before this one, or
  from this proposal's own before/after if preserved at apply time).
- **Rules Check**: Target is user space (`registry/` — not `BOOT.md`/kernel), no override
  needed. Doesn't touch `security.sk`. Consistent with R20's spirit (confirm before an
  action with real external effect) without being R20 itself (push isn't listed there, but
  the top-level system git-safety guidance already treats "pushing code" as needing
  confirmation by default — this proposal encodes that into the skill text rather than
  relying on it being remembered ad hoc). Doesn't weaken any existing gate, doesn't touch
  `evolution_policy.md`/`ultimate_rules.md`. No violation found.
- **Status**: APPLIED
