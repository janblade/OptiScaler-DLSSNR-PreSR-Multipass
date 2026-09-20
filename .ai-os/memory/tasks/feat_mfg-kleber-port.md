# Task — feat/mfg-kleber-port

Plan: memory/plans/2026-09-20-port-klebermotta-mfg-unlock.md. Ledger: klebermotta-mfg:builtin-mfg-unlock.

## 2026-09-20 — planned and approved

Studied KleberMotta's built-in RTX 40 MFG unlock (`74c3bac`, MIT, from the RenoDX addon by Dreamt / mavismmg, technique from dashdogy) against our `MfgUnlock.cpp`. Selective port in five stories: provider discovery by marker and OTA path, `sl.dlss_g` ceiling patch, telemetry, a selectable temporal fix (`AdaTemporalFix`), and an opt-in flip-metering plugin patch (`AdaFlipMeteringPatch`). Not taken: gate patches, forced multiplier and GetState raise (already ours), `RestoreAll`, `ForceOTAPlugins`, `raiseCeiling`, the `[MfgUnlock]` section.

UI placement follows `core.ux-review.sk`; the decisions are in the plan's "UI placement" section (a new collapsing header under the Ada checkbox for startup options, results directly under each control, live telemetry under "Override DLSSG Ratio").

Constraints to remember: no RTX 40 hardware here, so nothing is claimed working in game; building after edits is allowed (see the build-command memory); patches must keep unique-match, bounds and Ada-only guards.

Pending: execution. Branch cut, step 1 in progress.
