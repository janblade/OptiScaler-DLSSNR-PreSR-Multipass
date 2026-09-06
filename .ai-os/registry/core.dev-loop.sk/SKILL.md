---
name: dev-loop
description: >-
  Implementation with independent peer review. Dispatches a separate reviewer
  when the host genuinely supports spawning subagents; falls back to a
  structured same-agent self-review, honestly labeled, when it doesn't.
  Never claims independent review happened on a host that can't do it.
---

# Dev Loop — Implementation with Peer Review

## Overview

A single agent that both writes code and assesses its own work has a structural blind
spot: it's grading its own reasoning. This skill adds a second, independent check —
*where the host genuinely supports it*.

**Realism constraint (read this before invoking):** spawning an independent subagent is a
host tool capability, not something every agentic AI can do — some hosts (Claude Code's
`Agent`/`Task` tool, or an equivalent) support it, many don't. This skill checks for that
capability at the start of every invocation and takes an honestly-labeled fallback path
when it's absent. It never claims a second, independent agent reviewed the work unless one
actually did — that would be exactly the kind of overclaim `core.self-healing.sk`'s
Response Credibility Protocol exists to catch.

## Dependencies

- `core.security.sk` — security review is part of what any review pass checks
- `core.self-healing.sk` — Verification-Before-Completion Protocol gates the final report
- `core.observability.sk` — logging which path was used and why
- `core.planning.sk` (optional) — consumes a `PLAN_WRITE` plan as the task spec when one
  exists for the current work

## Commands

### DEV_IMPLEMENT_REVIEWED

Implement a task with a review pass — independent when possible, self-review when not.

```
> OS_COMMAND DEV_IMPLEMENT_REVIEWED --task="<description>"
```

**Procedure:**

1. **Capability check.** Does the current host expose a genuine subagent-dispatch tool
   (e.g. an `Agent`/`Task`-style tool actually present in this session's tool list)? This
   is a real introspection check — confirm the tool is actually available this session,
   don't assume based on host name alone. Available → Path A. Not available → Path B.

2. **Path A — two-stage subagent review** (host supports subagent dispatch):
   a. Dispatch an implementer subagent with the task description, the approved plan from
      `PLAN_WRITE` if one exists for this task, and relevant conventions from
      `memory/semantic/knowledge/conventions_patterns.md`.

      **A subagent does not inherit this session's boot state.** Session-start context is
      parent-thread only — on Claude Code, `SessionStart` and `SubagentStart` are separate
      hook events, so `scripts/session-start-hook.sh` (EP-56) injects BOOT.md into this
      thread and into no dispatched agent. A subagent spawned without governance context is
      an ungoverned writer: no R1 pre-mutation security review, no R21 claim verification,
      no `AIOS-DEBT:` marker convention, no decision logging — and its output arrives here
      looking exactly like governed work. Every dispatch prompt therefore carries, inline:
      the Rules Digest (BOOT.md §3), the decision-log schema (§9), and the conventions
      above. Cost is real (repeated per dispatch) and is the price of the check being worth
      anything.

      On Claude Code specifically, EP-60 automates this: a `SubagentStart` hook entry runs
      `scripts/session-start-hook.sh SubagentStart`, injecting BOOT.md and `ultimate_rules.md`
      into every dispatched agent. **Verify it before relying on it** — the hook lives in the
      user's `.claude/settings.json`, not in this repo's payload, so it may be absent,
      matcher-narrowed to exclude the agent type being dispatched, or the host may not be
      Claude Code at all. Not confirmed present and in scope for this dispatch → carry the
      context inline as above. Never assume the hook fired.
   b. Once implementation returns, dispatch a **second, independent** reviewer subagent —
      give it only the diff/changed files and the original task description, **not** the
      implementer's own reasoning or self-assessment (feeding the reviewer the
      implementer's justification biases it toward agreement instead of independent
      judgment). Ask it to check: correctness against the task description, convention
      adherence, security (cross-reference `SECURITY_SCAN_FILE`), and test coverage.
   c. Report the reviewer's findings to the user as-is, including disagreements with the
      implementer — don't silently auto-resolve a disagreement by picking a side; that
      defeats the point of having an independent check.

3. **Path B — same-agent structured fallback** (no subagent capability):
   a. Implement the task normally.
   b. Re-read the diff cold, as a **separate step** from writing it, against an explicit
      checklist: correctness against the task description, convention adherence, a
      security pass (`SECURITY_SCAN_FILE`), test coverage.
   c. State plainly in the report that this was a same-agent fallback review, not
      independent review — never word it as if a second agent checked the work.

4. Either path ends with `core.self-healing.sk`'s Verification-Before-Completion Protocol
   before reporting the task done.
5. Log which path was used and why in `memory/episodic/decisions.jsonl`.

---

## Common Mistakes

1. **Claiming independent review on a host without subagent capability** — the single
   most important failure mode this skill is built to prevent. If Path B ran, say Path B
   ran.
2. **Feeding the reviewer subagent the implementer's own reasoning trace** — biases the
   review toward rubber-stamping instead of judging the diff on its own merits.
3. **Auto-resolving reviewer disagreements** — surface them to the user; don't have the
   orchestrating agent unilaterally decide who was right.
4. **Dispatching a subagent without governance context** — it inherits none of this
   session's boot state (step 2a). Work comes back looking governed while having skipped
   R1, R21, and R13 entirely.
