# Operational Playbooks

> Pre-built response playbooks for common operational situations.
> These are starting templates — the agent refines them based on project-specific experience.

---

## Playbook: New Feature Development

### When to Use
User requests a new feature, capability, or component.

### Steps
1. **Understand**: Clarify requirements. Ask about edge cases, error handling, and integration points.
2. **Context Load**: Use `CONTEXT_LOAD` to assemble relevant code, patterns, and decisions.
3. **Plan**: Create an implementation plan. Reference existing patterns from `patterns.json`.
4. **Security Check**: Review the plan against `security_policy.md` — any new attack surfaces?
5. **Implement**: Write code following discovered conventions.
6. **Test**: Generate tests using `TEST_GENERATE`. Run with `TEST_RUN`.
7. **Scan**: Run `SECURITY_SCAN_FILE` on all new/modified files.
8. **Document**: Update `project_knowledge.md` if architectural insights were gained.
9. **Log**: Record the decision in `decisions.jsonl`.

---

## Playbook: Bug Fix

### When to Use
User reports a bug, error, or unexpected behavior.

### Steps
1. **Reproduce**: Understand the exact failure condition.
2. **Diagnose**: Use `HEAL_DIAGNOSE` if it's a framework issue, or trace the code for application bugs.
3. **Root Cause**: Identify the root cause, not just the symptom.
4. **Fix**: Implement the minimal fix that addresses the root cause.
5. **Test**: Write a regression test that would have caught the bug. Run full test suite.
6. **Scan**: Security scan the fix.
7. **Log**: Record as a decision with the root cause analysis.

---

## Playbook: Dependency Update

### When to Use
Updating, adding, or removing project dependencies.

### Steps
1. **Assess**: Why is this change needed? (Security patch, new feature, performance?)
2. **Check**: Run `SECURITY_CHECK_DEPS` on the new/updated dependency.
3. **Verify**: Check for breaking changes in changelogs.
4. **Update**: Apply the change with lock file update.
5. **Test**: Run full test suite to catch regressions.
6. **Log**: Record the dependency change decision.

---

## Playbook: Incident Response

### When to Use
Something has gone wrong in production or a critical failure has occurred.

### Steps
1. **Assess Severity**: Is this data loss, security breach, service outage, or degraded performance?
2. **Contain**: If security breach → `SECURITY_LOCKDOWN`. If service outage → identify affected systems.
3. **Diagnose**: Trace the failure. Check recent changes via `decisions.jsonl`.
4. **Fix or Rollback**: Apply a fix if clear, otherwise `HEAL_ROLLBACK` to last known good state.
5. **Verify**: Confirm the fix/rollback resolved the issue.
6. **Post-Mortem**: Document: what happened, why, how it was fixed, how to prevent recurrence.
7. **Harden**: Update tests, monitoring, or rules to prevent recurrence.

---

## Playbook: Project Onboarding (New Contributor/Agent)

### When to Use
First boot on an existing project, or onboarding a new team member.

### Steps
1. **Scan**: Execute full perception scan (`INFRA_DETECT_STACK`).
2. **Read**: Load `project_knowledge.md` for institutional knowledge.
3. **Map**: Review directory structure, entry points, and architecture.
4. **Verify**: Run `INFRA_HEALTH_CHECK` to confirm the project builds and tests pass.
5. **Summarize**: Update `project_knowledge.md` with any new findings.

---

## Playbook: Self-Evolution

### When to Use
The agent identifies a way to improve its own skills, commands, or workflows.

### Steps
1. **Identify**: What's the improvement? (New skill, better pattern, optimized workflow?)
2. **Propose**: Write an evolution proposal per `evolution_policy.md` PDCA format.
3. **Check Rules**: Verify the evolution doesn't violate any governance rules.
4. **Apply**: If approval requirements met, apply the change.
5. **Verify**: Run integrity check. Test the modified component.
6. **Log**: Record in `decisions.jsonl` and update `progress.md`.

---

*These playbooks are living documents. The agent refines them based on project-specific experience.*
