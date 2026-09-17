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

---

## Evolution Proposal: EP-4
- **Date**: 2026-09-17
- **Type**: skill_create
- **Target**: `registry/core.ux-review.sk/SKILL.md` (new), `UX_REVIEW` command
- **What**: New skill reviewing UI/UX for this project's actual surface -- a live, ImGui-based
  in-game tuning overlay (`OptiScaler/dlssnr/DlssNr_Menu.cpp` and `OptiScaler/menu/`), not a web
  or mobile app. Ten principles adapted from established usability heuristics (Nielsen Norman
  Group's 10, Fitts's Law, progressive disclosure, consistency) filtered to what actually
  transfers to a real-time overlay tuned mid-gameplay, each cross-referenced against a concrete
  convention already present in this codebase (`HelpMarker`, `Reset##<id>` buttons, conditional
  disclosure by mode, the White-point-source panel's state-specific messages). One command,
  `UX_REVIEW [--file] [--scope=new-control|full-menu]`, producing file/line findings the same
  shape as this framework's other review-type outputs -- not generic "consider improving UX"
  notes.
- **Why**: Requested directly by the user ("study UI/UX universal best practices and make that
  into our own skill"), after first surfacing and getting a decision on a different, narrower
  reading (`EVOLVE_BENCHMARK`, which is explicitly scoped to this framework's own dev repo, not
  an installed project -- see that command's own doc). Genuinely useful here: this session's own
  DLSS-NR work repeatedly touched `DlssNr_Menu.cpp` (HelpMarker text, tooltips, slider ranges),
  and `known_gotchas.md` already records a real instance of this exact failure class (a menu
  panel's status text going stale relative to what the code path actually did -- the "Game
  exposure" fallback investigation earlier this session) that a standing UX-review pass would
  have named on sight.
- **Risk**: medium (new skill -- per `core.evolution.sk`'s own risk table, new skills are
  medium regardless of subject matter)
- **Rollback Plan**: remove `registry/core.ux-review.sk/` entirely; remove its entry from
  `registry/index.json`'s `skills` array and `UX_REVIEW` from `commands/index.json`'s
  `skill_commands`; revert `manifest.json`'s `installed_skills` array and
  `evolution_history` counters to their pre-EP-4 values (all four files are git-tracked,
  restorable via `git diff`/`git checkout` against the commit before this one).
- **Rules Check**: Target is user space (`registry/` — not `BOOT.md`/kernel), no override
  needed. Doesn't touch `security.sk`. Doesn't weaken any existing gate, doesn't touch
  `evolution_policy.md`/`ultimate_rules.md`. `EVOLVE_BENCHMARK` was explicitly considered and
  rejected as the applicable command for the user's actual request, per its own "project-only,
  MaiKS's own dev repo" scoping -- surfaced to the user before proceeding rather than run anyway.
  No violation found.
- **Status**: APPLIED
