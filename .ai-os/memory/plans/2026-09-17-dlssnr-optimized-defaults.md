# Plan: "Optimized Defaults" preset button for the DLSS-NR menu
- Branch: fix/dlssnr-nr-menu-ux (existing, built on top of the menu-UX-polish work per user
  choice rather than a separate branch)
- Created: 2026-09-17
- Status: done
- Task file: memory/tasks/fix_dlssnr-nr-menu-ux.md

## Context
Adds a one-click "Optimized Defaults" button to the top of the DLSS Neural Rendering panel
that sets ~25 specific controls to a curated configuration, per the user's spec. Resolution
check uses total pixel count (width x height vs 1920x1080); no active feature falls back to
the <=1080p behavior (disable Auto, 100%); the preset touches only the listed settings --
everything else in the panel (Pass 2/3 overrides, skin/environment sliders, Compare, Debug
view, Hold frame, Downscaler, exposure-scan settings, etc.) is left exactly as the user had
it. Button placed at the top of the panel per user's explicit choice.

## Steps

1. [x] Move `static int pendingScale = -1;` from its current spot (just above the
   Model-resolution slider) to the top of `RenderMenu`, right after `ImGui::Spacing();`.
   Pure relocation -- a function-local `static` has the same lifetime and value either way;
   this makes it reachable by the new button's click handler, which sits earlier in the
   function. -- verify: build clean; Model-resolution slider's deferred-commit behavior
   unchanged.

2. [x] Add `static void ApplyOptimizedDefaults(Config* config, int& pendingScale)` near the
   other menu-local helpers (after `InheritedProfileCombo`, before `RenderMenu`). Sets:
   ```
   DlssNrEnabled                = true
   DlssNrFinishedPicture        = false
   DlssNrRunBeforeSr            = false
   DlssNrPrecision              = 0u                    // NVIDIA (FP8)
   DlssNrDeferredDlss           = false
   DlssNrResidualFg             = false
   DlssNrResidualFgApproxCamera = false
   DlssNrApplyModel             = true
   DlssNrUnlockPasses           = false
   DlssNrPasses                 = 1u

   // Model resolution: total-pixel-count check against 1920x1080 (2,073,600px).
   // No currentFeature (no game running) -> treated as <=1080p.
   const auto feature = State::Instance().currentFeature;
   const unsigned long long outputPixels =
       feature ? (unsigned long long) feature->TargetWidth() * feature->TargetHeight() : 0ull;
   if (outputPixels > 1920ull * 1080ull)
   {
       DlssNrModelResolutionAuto = true;
   }
   else
   {
       DlssNrModelResolutionAuto = false;
       DlssNrWorkingScale        = 1.0f;               // 100%
       pendingScale              = -1;                  // clear any in-flight drag
   }

   DlssNrTransfer                = 1u                   // Enlargement: Matched residual
   DlssNrReducedUpscaleMethod    = 2u                   // Enlarge filter: SGSR1 (input + output, sharpest)
   DlssNrSgsr1EdgeThreshold      = 0.3f
   DlssNrSgsr1EdgeSharpness      = 0.9f
   DlssNrTransferStrength        = 1.5f                  // Detail strength
   DlssNrColourStrength          = 1.0f
   DlssNrReversibleMode          = 2u                    // HDR mapping: Reversible curve + replace
   DlssNrReplaceDetailStrength   = 2.0f
   DlssNrStyle                   = 0u                    // Pass 1 Style: Standard
   DlssNrIntensity               = 0.98f                 // Pass 1 Intensity
   DlssNrLocalStructure          = 0.98f                 // Pass 1 Local structure
   DlssNrLocalTone               = 1.75f                 // Pass 1 Local tone
   DlssNrSkinStructure           = -1.0f                 // Pass 1 Skin structure
   DlssNrAutoMask                = true                  // Pass 1 Auto skin mask
   DlssNrWhitePointSource        = 1u                    // White point source: Game exposure
   DlssNrWhitePointTrim          = 1.0f                  // Trim (x the game's exposure)

   // Highlight guard: 2.0x normally, 1.3x if this game has never offered a Game-exposure
   // value (same "have we ever seen an exposure" check the White-point-source panel's own
   // readout uses).
   const bool haveExposure = DlssNr::IsRunningVk() ? DlssNr::ExposureOfferedVk()
                                                    : DlssNr::GameExposureStatus().everOffered;
   DlssNrMaxRatio                = haveExposure ? 2.0f : 1.3f
   ```

   **2026-09-17 update:** SGSR1 edge sharpness 2.0->0.9, Replace detail strength 1.75->2.0,
   Pass 1 Intensity 1.5->0.98, Pass 1 Local structure 1.25->0.98, Pass 1 Local tone
   1.25->1.75, and the Highlight-guard exposure-dependent branch above, all per user request
   after the plan's original completion. Implemented as a direct edit (values + one small
   conditional in an already-reviewed, already-implemented function) rather than a new
   PLAN_WRITE cycle -- granular tweak to existing code, not a new unit of work.
   Every field/value checked against this file's existing combo-index and default mappings.
   -- verify: build clean; each assignment matches `Config.h` field types (no implicit-
   conversion warnings).

3. [x] Add the button right after `ScopedIndent indent {};` / `ImGui::Spacing();` at the top
   of the panel, before "Enable Neural Rendering":
   ```cpp
   ImGui::SeparatorText("Presets");
   if (ImGui::Button("Optimized Defaults"))
       ApplyOptimizedDefaults(config, pendingScale);
   HelpMarker("Set this fork's recommended starting point: NR after Super Resolution, FP8 precision, "
              "1 pass, Matched residual + SGSR1 enlargement, Reversible curve + replace HDR mapping, "
              "and game-exposure white point. Overwrites the settings below; anything not listed here "
              "is left as you have it.");
   ```
   A plain `Button` (not `SmallButton`), no confirmation dialog -- matches this file's
   existing convention of instant, unconfirmed Reset actions, sized for a more deliberate,
   less-frequent action. -- verify: build clean; click in-game, confirm every listed field
   lands on its target value via the panel's own readouts.

4. [x] Build verify Debug|x64 and Release|x64, 0 errors, no new warnings in `DlssNr_Menu.cpp`.

## Verification
- Debug|x64 and Release|x64 both build clean, 0 errors, no new warnings in `DlssNr_Menu.cpp`.
- Review Pass: independent reviewer subagent — PASS on all 5 checklist items (correctness,
  convention, security n/a, test coverage n/a-noted, ownership fit). No findings.
- **Not verified: in-game confirmation.** No running game session was available this
  session. The reviewer specifically flagged clicking the button once with no game running
  and once with a >1080p output feature active as the manual check worth doing before
  merge, to confirm the two branches of the resolution check land correctly — neither has
  been exercised yet.
