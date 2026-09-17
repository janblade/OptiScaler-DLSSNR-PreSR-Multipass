---
name: ux-review
description: >-
  Universal UI/UX heuristics adapted for this project's actual surface — a live,
  ImGui-based in-game tuning overlay, not a web or mobile app. Reviews new or
  existing menu controls against established usability principles (Nielsen,
  Fitts's Law, progressive disclosure, consistency) and this codebase's own
  already-converged conventions (HelpMarker, Reset buttons, conditional
  disclosure), rather than generic advice that doesn't fit a real-time debug
  overlay.
---

# UI/UX Review — Universal Heuristics, Applied to This Project's Actual Surface

## Overview

This project's only UI is `OptiScaler/dlssnr/DlssNr_Menu.cpp` and its siblings under
`OptiScaler/menu/` — an ImGui overlay rendered every frame over a running 3D game, tuned live
while the picture is moving, by a user who is usually also trying to play the game. That is a
narrow, specific context most generic "UI/UX best practices" writing doesn't address (it defaults
to web forms or mobile apps: page loads, taps, small fixed viewports). This skill exists to filter
universal principles down to the ones that actually transfer to a real-time overlay, and to name
the ones this codebase has already converged on by repeated iteration (see `HelpMarker`, `Reset`
buttons, conditional disclosure below) so new controls follow them by default instead of by luck.

Genesis: EP-4, requested directly — "study UI/UX universal best practices and make that into our
own skill" — not a benchmark-against-other-frameworks gap-fill.

## Dependencies

- `core.simplicity.sk` — a new control earns its place the same way any other code does; a slider
  for a setting nobody will ever move off its default is a complexity smell before it's a UX one.
- `core.observability.sk` — logging findings/decisions for review-type invocations.

---

## The principles, filtered to what transfers to a live in-game overlay

Sourced from established, well-attested usability literature (Nielsen Norman Group's 10 usability
heuristics, Fitts's Law, and long-standing progressive-disclosure/consistency practice) — not
reinvented, but each one is kept only if it survives contact with "the user is tuning a slider
while dodging an enemy attack, in a panel that shares screen space with the game itself":

1. **Visibility of system state.** Every control that affects the rendered image should make its
   effect visible immediately — this overlay already gets this for free (it renders over a live
   game), so the failure mode here isn't invisibility, it's *misleading* state: a value shown in
   the UI that doesn't match what the shader is actually doing (see `known_gotchas.md`'s
   "Game exposure" panel entry — a real instance of the UI claiming a fallback that had actually
   engaged a different, undocumented tier). Treat a UI string that can drift from the code path it
   describes as a bug class, not a copy-editing nit.
2. **Match between system and the real world.** Labels use the domain's own vocabulary (the user's
   — "Highlight guard", "White point", "Model resolution" — not internal variable names like
   `gMaxRatio`/`DlssNrWorkingScale`). Where a term is genuinely technical and unavoidable (Neutwo,
   SGSR1, HDR mapping modes), a `HelpMarker` carries the plain-language explanation; the label
   itself stays short.
3. **User control and freedom — every destructive or hard-to-reverse slider gets an escape hatch.**
   This codebase's own convention, already consistent across most (not yet all) tunables: a
   `SmallButton("Reset##<id>")` beside the slider, restoring a documented default. When adding a
   new tunable, add its `Reset` in the same commit, not as a follow-up — an un-resettable control
   a user has driven to an extreme value (chasing a bug, as this session's own Highlight-guard
   troubleshooting did) is a support burden the very next session inherits.
4. **Consistency and standards.** Same shape, same behaviour, across the whole menu: slider format
   strings match their unit (`"%.2f"` for a ratio, `"%.1fx"` for a multiplier, `"%.0f%%"` for a
   percentage — check the neighbouring control before inventing a new one), `HelpMarker` tone
   stays factual and short (no marketing language, no "simply" / "just"), Reset buttons always sit
   `ImGui::SameLine()` immediately after their slider, never in a different place per-control.
5. **Error prevention over error messages.** `ImGui::SliderFloat`'s own min/max already prevents
   most invalid states by construction — prefer that over a runtime check that produces a warning
   after the fact. Where an invalid *combination* of otherwise-valid settings is possible (e.g. a
   mode whose control has no effect given another control's current value), say so inline, in
   colour, at the point of the conflicting control — this codebase already does this well for the
   White-point-source panel (`DlssNr_Menu.cpp` — "No game exposure available" / "Waiting for a
   frame..." / etc., each state distinct) and for `RunBeforeSr` combinations elsewhere in the
   menu. Don't let a control silently do nothing; say so where the user is looking.
6. **Recognition over recall.** The overlay is opened intermittently, often after a break of
   sessions — don't require the user to remember what an abbreviation meant. A live-updated
   readout beside a control (e.g. "Game exposure %.4f -> white point %.2f") beats a control whose
   effect the user has to infer or remember from a `HelpMarker` they read once weeks ago.
7. **Aesthetic and minimalist design / progressive disclosure.** Every control visible costs the
   user attention, every frame, whether they touch it or not. This codebase already gates
   detail/advanced controls behind their parent mode being selected (`if (reversible == 2 ||
   reversible == 4) { /* Replace-only controls */ }`) — keep doing that for anything whose value
   is meaningless outside a specific mode, rather than showing it always and disabling/greying it
   (greyed controls still cost visual scan time and the "why is this here" question).
8. **Help and documentation, in place, not in a separate manual.** `HelpMarker` on hover is the
   only realistic documentation surface for a control tuned mid-game — there is no manual anyone
   reads at 2am chasing a flicker. Every control whose name alone doesn't make its effect and
   range obvious gets one. A `HelpMarker` that only restates the label ("White point: sets the
   white point") is worse than none — it trains the user to stop reading them. State the effect,
   the range's meaning at its ends, and — where relevant — when to touch it at all (mirroring this
   codebase's own better examples: the Highlight-guard `HelpMarker`, the SGSR1-threshold one).
9. **Fitts's Law, applied to the one axis that matters here: hit-target size vs. adjacent-control
   risk, not travel distance** (the overlay isn't mouse-distance-constrained the way a toolbar is).
   A `SmallButton` next to a `SliderFloat` is the right call specifically because a Reset action is
   rare and destructive-*ish* (loses a tuned value) — making it small and deliberate is the
   correct trade, not an oversight. Don't enlarge it "for accessibility" without also reconsidering
   whether it should require confirmation instead; the two trade off against each other.
10. **Consistency with the game's own conventions where they're visible through the overlay** —
    this overlay is a guest on someone else's screen. Respect the host: don't claim colours/tones
    that clash violently across common game palettes, keep the overlay's own accent colours
    (warning amber, success green — already established in `DlssNr_Menu.cpp`'s status text)
    consistent with each other above consistency with any one game.

## Command: UX_REVIEW

```
> OS_COMMAND UX_REVIEW [--file=<path>] [--scope=new-control|full-menu]
```

**Procedure:**
1. Read the target: a specific diff/control (`--scope=new-control`, the common case — reviewing
   what's about to be added or was just added) or the whole menu file (`--scope=full-menu`, rare,
   expensive, only on explicit request).
2. Check each of the 10 principles above against the target. Skip a principle only when it
   genuinely doesn't apply to what's being reviewed (e.g. principle 9 rarely applies to a
   checkbox) — say so, don't silently omit it.
3. For `new-control` scope specifically, also check the mechanical checklist every existing
   control in this file already follows: format string matches unit; `Reset` present and
   `SameLine()`-adjacent; `HelpMarker` present if the label alone doesn't convey effect+range;
   gated behind its parent mode if meaningless outside it; any state-dependent message (like the
   White-point-source panel's) kept in sync with what the code path actually does, not just what
   it did when the message was written — this is the specific failure class principle 1 above
   names, and it is empirically the one most likely to have already happened here.
4. Report findings the same way `code-review`-style skills do in this framework: file, line,
   concrete finding, why it matters — not a generic "consider improving UX" note. A finding
   without a specific fix isn't a finding, it's a mood.
5. Findings that are genuine, non-trivial UI/UX defects (not style preference) → log via
   `LOG_DECISION` if a design call was made resolving one; fold into the normal review/commit flow
   otherwise. This skill does not itself gate a commit — it informs the same human/agent review
   every other change already goes through.

## Common Mistakes

1. **Treating this as generic web/mobile UX advice.** A real-time overlay tuned mid-gameplay has
   different constraints (no page loads, no navigation, shared screen real estate, a user whose
   attention is split) than a form or an app. Apply principle 1-10 above, not a checklist copied
   from a web-design source that assumes a browser tab the user's full attention is on.
2. **Flagging a control for lacking a Reset button without checking whether it's a rare,
   deliberately-simple toggle** (a boolean checkbox with only two states doesn't need a Reset the
   way a continuous slider driven to an extreme does) — the convention exists for sliders/continuous
   values where a user can drift far from a sane default while troubleshooting, which is exactly
   what happened in this session's own DLSS-NR Highlight-guard investigation.
3. **Proposing a new dependency (an ImGui extension, a design library) for something the existing
   widget set already covers.** `core.simplicity.sk`'s ladder applies here as much as anywhere —
   this project's entire UI surface is hand-rolled `imgui.h` calls, on purpose, and stays that way.
