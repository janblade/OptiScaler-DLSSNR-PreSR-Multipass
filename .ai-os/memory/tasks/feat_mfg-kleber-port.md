# Task — feat/mfg-kleber-port

Plan: memory/plans/2026-09-20-port-klebermotta-mfg-unlock.md. Ledger: klebermotta-mfg:builtin-mfg-unlock.

## 2026-09-20 — planned and approved

Studied KleberMotta's built-in RTX 40 MFG unlock (`74c3bac`, MIT, from the RenoDX addon by Dreamt / mavismmg, technique from dashdogy) against our `MfgUnlock.cpp`. Selective port in five stories: provider discovery by marker and OTA path, `sl.dlss_g` ceiling patch, telemetry, a selectable temporal fix (`AdaTemporalFix`), and an opt-in flip-metering plugin patch (`AdaFlipMeteringPatch`). Not taken: gate patches, forced multiplier and GetState raise (already ours), `RestoreAll`, `ForceOTAPlugins`, `raiseCeiling`, the `[MfgUnlock]` section.

UI placement follows `core.ux-review.sk`; the decisions are in the plan's "UI placement" section (a new collapsing header under the Ada checkbox for startup options, results directly under each control, live telemetry under "Override DLSSG Ratio").

Constraints to remember: no RTX 40 hardware here, so nothing is claimed working in game; building after edits is allowed (see the build-command memory); patches must keep unique-match, bounds and Ada-only guards.

Pending: execution. Branch cut, step 1 in progress.

## 2026-09-20 — executed and built

Branch `feat/mfg-kleber-port`, eight commits after `main` (`2cde10be` .. `809c5fc0`), not pushed. All five stories done: provider discovery (OTA path, module walk), Streamline plugin ceiling patch, overlay telemetry, selectable temporal fix (`AdaTemporalFix`, PTX rewrite added), opt-in software frame pacing (`AdaFlipMeteringPatch`). UI placement followed `core.ux-review.sk`. Release x64 built; five smoke tests pass; the finders were also run read-only against real `nvngx_dlssg.dll` 310.8/310.9 and `sl.dlss_g.dll` 2.13.0.0 that the user pointed at (all found their targets). Details, deviations (marker changed to `DLSSG.MultiFrameCountMax`; no `off` item; ceiling patch applied whenever the unlock lands) and the review are in the plan's execution log.

Pending: the user's confirmation to close (`PORT_CLOSE`); push and PR when they say; any RTX 40 test. Nothing has been run in a game.
