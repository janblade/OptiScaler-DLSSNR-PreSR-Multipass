# Plan: DLSS-NR menu — Reset-button gaps, a range-coupling bug, and control naming cleanup
- Branch: fix/dlssnr-nr-menu-ux
- Created: 2026-09-17
- Status: done
- Task file: memory/tasks/fix_dlssnr-nr-menu-ux.md

## Context
Fixes 8 findings from `UX_REVIEW` against `OptiScaler/dlssnr/DlssNr_Menu.cpp`: one real bug
(an unrelated toggle silently changes another slider's range), five missing Reset buttons on
sliders that otherwise all follow the same convention, and two naming inconsistencies (an
internal codename leaking into a visible dropdown, an abbreviation used before it's ever
defined). All changes are string/UI-only except step 1, which is a one-line logic fix. Out of
scope: the lower-priority Compare-section Reset gaps and the disable-vs-hide inconsistency
noted in the review (cosmetic-only, no output effect).

No automated test suite exists for this project (`genome/project_genome.json`:
`has_tests: false`) — verification is build-clean plus manual/visual confirmation, same as
this project's own established convention.

## Steps
1. [x] Decouple "Highlight guard"'s slider max from "Lift model pass limit." `DlssNr_Menu.cpp:894`
   currently reads `unlockPasses ? (float) MaxPassCount : 8.0f` — replace with a dedicated
   constant (`8.0f`, unaffected by `unlockPasses`) so the guard's range no longer silently
   jumps to 30x. — verify: build clean; toggle "Lift model pass limit" on/off in-game, confirm
   Highlight guard's slider stays capped at 8.0x either way.
2. [x] Add Reset to the four skin/environment sliders (`:601-612`, the `slider` lambda). Each
   resets to its `Config.h` default of `1.0f`, `SameLine()`-adjacent, matching every other
   slider in the file. — verify: build clean; in-game, drag each to an extreme and confirm
   Reset returns it to 1.0.
3. [x] Add Reset to "Model resolution" (`:350-352`), resetting `DlssNrWorkingScale` to `1.0f`
   (100%) and clearing `pendingScale` immediately (not deferred, matching how the other
   instant-Reset sliders in this file behave). — verify: build clean; drag to 50%/200%,
   confirm Reset returns to 100%.
4. [x] Add Reset to "Model passes" (`:321-328`), resetting `DlssNrPasses` to `1u`. — verify:
   build clean; raise to 3+, confirm Reset returns to 1.
5. [x] Add Reset to "Paper white" — scoped to the manual-source slider only (`:882-889`,
   `wpSource==0`), resetting `DlssNrWhitePointScale` to `1.0f`. Not added to the anchor-editing
   variant (`:803-824`) since its own row-delete button already serves that purpose. — verify:
   build clean; drag to an extreme, confirm Reset returns to 1.0x.
6. [x] Rename "HDR mapping" combo options (`:472-474`) to remove the internal codename
   "Neutwo" and the "proxy" implementation term:
   - `"Neutwo proxy + composed"` -> `"Reversible curve + composed"`
   - `"Neutwo proxy + replace"` -> `"Reversible curve + replace"`
   - `"Hybrid proxy + composed"` -> `"Balanced curve + composed"`
   - `"Hybrid proxy + replace"` -> `"Balanced curve + replace"`
   ("Off (soft knee)" unchanged.) Reword the HelpMarker at `:482` to match (swap "Neutwo" ->
   "reversible curve"). "Balanced" substitutes for "Hybrid" specifically to avoid colliding
   with "Model precision"'s unrelated "Hybrid" (FP8+NVFP4). — verify: build clean; visually
   confirm the dropdown and its tooltip in-game.
7. [x] Standardize "Super Resolution" vs "SR" to the file's own established pattern (spell out
   at first mention, abbreviate after — already how `beforeSr`'s checkbox/tooltip at
   `:158-167` does it):
   - `:196` HelpMarker: `"Overrides Apply before Super Resolution."` -> `"Overrides Apply
     before SR."`
   - `:198` status text: `"Apply before Super Resolution controls NR placement."` -> `"Apply
     before SR controls NR placement."`
   - `:364` HelpMarker: both `"Super Resolution"` occurrences -> `"SR"`
   — verify: build clean; visually confirm tooltip text in-game.
8. [x] Define "FG" before using it standalone: `:204` checkbox `"NR every second frame
   (NVIDIA FG, experimental)"` -> `"NR every second frame (NVIDIA Frame Generation,
   experimental)"`; `:206` HelpMarker `"use NVIDIA FG to interpolate"` -> `"use NVIDIA Frame
   Generation (FG) to interpolate"` (so `:208`'s later standalone "FG" is now backed by a
   definition). — verify: build clean; visually confirm both tooltips in-game.

## Verification
- Debug|x64 and Release|x64 both build clean, 0 errors, `DlssNr_Menu.cpp` compiles without
  new warnings.
- Review Pass: independent-reviewer subagent dispatch failed (session rate limit) ->
  fell back to same-agent cold self-review against the standard 5-item checklist
  (correctness, convention, security, test coverage, ownership fit). Found and fixed one nit
  (a code comment above `reversibleNames[]` still referenced the pre-rename "Neutwo"/"hybrid"
  terminology); no other findings.
- **Not verified: in-game visual confirmation for any of the 8 steps.** No running game
  session was available this session. Every step's code change is mechanically identical to
  an existing, already-shipped pattern elsewhere in the same file (SmallButton("Reset##...")
  + SameLine(), or a plain string swap), so risk is low, but this is disclosed rather than
  claimed as checked.
