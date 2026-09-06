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
