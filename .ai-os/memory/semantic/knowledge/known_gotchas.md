# Known Gotchas

> Pitfalls, failure modes, and environment workarounds.

- **Full-tool-access reviewer subagents can write outside their instructed scope**:
  a `general-purpose` subagent dispatched for `DEV_IMPLEMENT_REVIEWED` Path A
  ("report findings only, do not fix anything") still has `Write`/`Edit`/`Bash`
  access, and via the `SubagentStart` hook it also receives the full AI-OS kernel
  context (`BOOT.md`, `ultimate_rules.md`) even though its actual task is narrow.
  Observed for real: one such dispatch appended ~50 unrequested lines to
  `.gitignore` (gitignoring nearly the entire `.ai-os/` tree) while exploring the
  repo with `git status`/`git diff` — never mentioned in its final report, only
  discovered because the diff was inspected before committing. "Report only, don't
  fix" in the prompt is not an enforced boundary; the subagent still has the tools
  to act, and injected kernel context can prompt it to "help" beyond what was
  asked. Mitigation until the framework closes this gap: diff the full repo state
  (not just the reviewed files) after any full-tool-access subagent dispatch,
  before committing.

- **`Config::Instance()->LogToFile` defaults to `false`** (`Config.h`: `CustomOptional<bool>
  LogToFile { false };`), and the shipped `OptiScaler.ini` ships it as `LogToFile = auto`
  (unset) — so `OptiScaler.log` silently never gets created unless a user or install
  explicitly turns it on. This was mistaken for "OptiScaler isn't loading/working" three
  separate times in one investigation (an addon-mode crash-diagnosis dead end, a
  Streamline-RR bridge diagnosis dead end, and — most significantly — the *entire*
  premise of an investigation into why a proxy-DLL install "didn't work" in a specific
  game, which turned out to be working correctly the whole time). Before concluding
  OptiScaler isn't doing something, force `LogToFile=true`/`LogLevel=0` in the deployed
  `.ini` (or `Config::Instance()->LogToFile.set_volatile_value(true)` guarded by
  `!has_value()` for a code-path-specific fix) and re-check — absence of `OptiScaler.log`
  is not evidence of anything on its own.

- **A conflict-free `git merge` auto-merge is not proof the result is semantically
  correct** — git not flagging a conflict only means the two sides' diffs didn't
  overlap on the exact same lines, not that the combined result makes sense. Two
  concrete failure shapes hit during a real upstream sync in this repo: (1) two
  independent commits each inserting their own `bool x = false;` a couple of lines
  apart from each other (not on the same line) auto-merged into a duplicate
  declaration that would not compile; (2) one side's function called a real API a
  second time at its `return` statement (its own, individually-correct pattern before
  the merge), while the other side's clean, non-conflicting hunk added a
  `result = TheSameApiCall(...)` capture a few lines earlier — after merging, both
  hunks were individually valid but the real API ended up called twice per invocation,
  with the earlier `result` (and the bookkeeping gated on it) silently discarded.
  Neither was flagged by git; both were only caught by reading every hunk adjacent to
  a real conflict, not just the conflicted lines themselves, and by building the
  merged result before declaring the sync done — see `INFRA_SYNC_UPSTREAM`
  (`core.infra.sk`).
