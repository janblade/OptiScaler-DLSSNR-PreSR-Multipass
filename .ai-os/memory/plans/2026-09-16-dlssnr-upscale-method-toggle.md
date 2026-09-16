# Plan: User-selectable reduced-model-resolution enlarge filter (Bilinear vs SGSR1)
- Branch: feat/dlssnr-upscale-method-toggle
- Created: 2026-09-16
- Status: done
- Task file: memory/tasks/feat_dlssnr-upscale-method-toggle.md

## Context

Below 100% model resolution, DLSS-NR's answer and proxy are currently always enlarged back to
native with SGSR1 (`shaders/sgsr1/sgsr1.hlsl`) before the resolve reads them -- a real
edge-directed filter (Gather-tap edge vote, up to 12 `weightY`/`fastLanczos2` calls per pixel
on the edge branch), dispatched at full native output resolution twice a frame (answer +
proxy). That's real, newly-added, steady-state GPU cost with no way to opt out today (`Config.h`
currently says "always on, no separate setting"). This plan adds that setting.

The old behaviour is not gone -- it's SGSR1's own already-existing build-failure fallback: the
resolve reads `gSource`/`gModel` with a bilinear `SampleLevel` regardless of the bound
texture's size (`dlssnr.hlsl:308-311`), so simply skipping the SGSR1 dispatch and handing the
resolve the still-small `modelInput`/`finalAnswer` (exactly what already happens today when
SGSR1 fails to build) reproduces the pre-SGSR1 bilinear look with no shader changes.

**Acceptance criteria:**
- A new menu control lets the user pick Bilinear or SGSR1 for the reduced/up-leg enlarge,
  visible only when model resolution is below 100% (same visibility gate as the existing
  "Enlargement" combo).
- Default is SGSR1 -- no behavior change for anyone who doesn't touch the new control.
- Picking Bilinear means the SGSR1 passes are never constructed (no PSO compiled) for that
  session, not just skipped per-dispatch -- avoids the pass's cost entirely, not just its
  per-frame dispatch.
- Debug + Release x64 build clean, 0 new warnings.
- In-game confirmation: both settings produce a plausible image (SGSR1 sharper, Bilinear
  softer at low model resolution, as before this feature existed), and the ini
  round-trips the new setting.

**Out of scope:** the supersample leg (`workScale > 1`, `DlssNrScalingDownscaler` filter
choice) -- separate, already-configurable, not part of what was asked. Fixing "Matched
residual"'s stale-gate no-op -- Bilinear happens to un-break it as a side effect, documented,
not actively worked on here.

## Revision (2026-09-16, before step 5 was ever run)

Steps 1-4 shipped code where the SGSR1 setting enlarged *both* proxy and answer with SGSR1,
mirroring the original `feat/dlssnr-sgsr1-upscale` design. Follow-on analysis (walking through
which buffer actually reaches the displayed pixel in each reversible mode) found that's
spending half the SGSR1 cost for little to no visible return:

- **Replace modes (2/4)**: the output is `modelDirect` -- the answer buffer -- captured before
  anything touches proxy (`dlssnr.hlsl:914`, used at `:1156-1158`). Proxy has **zero** effect on
  Replace's displayed pixel.
- **Composed modes**: `upgraded` is `model` rescaled by a luminance `ratio` derived from
  comparing `proxyLuma`/`modelLuma`/`originalLuma` (`dlssnr.hlsl:1043-1067`). The answer buffer
  still carries essentially all the spatial/chroma detail on screen; proxy only supplies a
  scalar brightness-matching gate, which tolerates a softer source far better than the thing
  actually painted.

So the design changes to: **SGSR1 enlarges the answer only, always; proxy always uses the
implicit bilinear tap (`dlssnr.hlsl:308-311`/`:905-910`), under both settings.** This is not the
original `colorCopy`-vs-`modelInput` bug reappearing -- proxy still correctly reads from
`modelInput` (the real downsampled source the model saw), just via an implicit sampler read
instead of a dedicated pass, exactly like the existing all-Bilinear fallback already does and
already builds/runs clean. The only thing being removed is ever running SGSR1 *on the proxy
specifically*. Side effect: `modelRanSmall` (`dlssnr.hlsl:1001`, `proxyW != gWidth`) will now
always read `true` under reduced resolution regardless of the enlarge setting, since `gSource`
never gets promoted to native size any more -- this incidentally fixes "Matched residual"
being a silent no-op whenever SGSR1 was engaged (`known_gotchas.md`), not something this
revision set out to fix on its own.

Chosen via `AskUserQuestion`: continue on this same branch/plan (steps 1-4's code gets revised
in place, not forked into a new task) since nothing has shipped or been tested yet.

## Steps

1. [x] `Config.h`/`Config.cpp`: add `CustomOptional<uint32_t> DlssNrReducedUpscaleMethod { 1 };`
   near `DlssNrWorkingScale` (0 = Bilinear, 1 = SGSR1, default 1 = today's behaviour), with a
   comment explaining the tradeoff. Wire `readUInt`/`ini.SetValue` load/save lines alongside
   the other `DlssNr*` fields. — verify: Debug build compiles; `OptiScaler.ini` gains the key
   on next save.
2. [x] `DlssNr_Dx12.cpp`: add `cfg.DlssNrReducedUpscaleMethod.value_or_default() == 1` to the
   outer condition already guarding the SGSR1 construct+dispatch block (the
   `if (reduced && workScale < 1.0f && g_nr.outputNative != nullptr && g_nr.proxyNative !=
   nullptr)` block, currently ~line 2955) so Bilinear skips SGSR1 entirely -- no lazy `new`,
   no dispatch, `sgsrAnswerOk`/`sgsrProxyOk` stay false, existing fallback (`resolveProxy =
   modelInput`, `resolveAnswer = finalAnswer`) takes over unchanged. Also add the same check
   to the earlier `g_nr.proxyNative` scratch allocation (~line 2011) so that buffer isn't
   allocated for a session that will never use it. — verify: with Bilinear selected, no
   `SGSR1_Dx12` instance exists at all (log/breakpoint check); with SGSR1 selected, behavior
   is byte-identical to before this change.
3. [x] `DlssNr_Menu.cpp`: add a `Combo("Enlarge filter", ...)` with options `{"Bilinear
   (fast)", "SGSR1 (sharper)"}` inside the existing `reduced`-gated block (~line 391-407,
   right alongside the "Enlargement" combo), writing `DlssNrReducedUpscaleMethod`. HelpMarker:
   explain the perf/quality tradeoff plainly (SGSR1 costs more GPU time, sharper below 100%
   model resolution; Bilinear is the old cheap default). — verify: visible only when model
   resolution < 100%, matches the existing combo's disabled/grey behavior otherwise.
4. [x] Debug + Release x64 full build. — verify: 0 errors, 0 new warnings vs baseline. DONE:
   both configs built clean via local MSBuild (`D:\DEV\VS2022\MSBuild\...`); only pre-existing
   warnings (XeSS dominance, LNK4098, C4744), none in the touched files.
5. [x] `DlssNr_Dx12.cpp` `NrState`: remove the `proxyNative` and `sgsr1UpProxy` fields
   entirely (with their explanatory comments) -- proxy never gets its own enlarge pass under
   either setting any more. — verify: compiles once call sites are updated in the following
   steps.
6. [x] Remove all `g_nr.proxyNative` handling: drop it from `ReleaseSurfacesIfFormatChanged`'s
   park-list, delete its scratch-allocation block entirely (the one gated on
   `DlssNrReducedUpscaleMethod == 1`, added in step 2 above), and drop its cleanup in
   `Shutdown()`. — verify: no remaining references (`grep proxyNative`).
7. [x] Rewrite the SGSR1 up-leg block: only construct/dispatch `g_nr.sgsr1UpAnswer` (drop
   `sgsr1UpProxy`'s construction, dispatch, and barrier entirely); simplify the success flag to
   just `sgsrAnswerOk` (drop `sgsrProxyOk`/the `&&` combine); update the `LOG_INFO` to report
   only the answer pass's engagement/init state. Drop `sgsr1UpProxy` from `Shutdown()`. —
   verify: builds; log line no longer mentions a proxy instance.
8. [x] Update `resolveProxy`/`resolveAnswer`: `resolveProxy = superDownOk ? g_nr.colorCopy :
   modelInput;` (always `modelInput` when reduced -- `proxyNative` no longer exists);
   `resolveAnswer = (superDownOk || sgsrAnswerOk) ? g_nr.outputNative : finalAnswer;`. — verify:
   code review, matches the acceptance criteria above.
9. [x] `Config.h`: update `DlssNrReducedUpscaleMethod`'s doc-comment to describe the new
   asymmetric behaviour (SGSR1 enlarges the answer only; proxy always uses the bilinear tap
   either way). — verify: comment accuracy only.
10. [x] `DlssNr_Menu.cpp`: update the "Enlarge filter" combo's `HelpMarker` to describe the new
    behaviour, and relabel the control "Enlarge filter (model output)" for accuracy now that it
    no longer affects the input side. — verify: visible text matches actual behavior.
11. [x] Debug + Release x64 full build. — verify: 0 errors, 0 new warnings vs baseline. DONE:
    both configs linked clean (`OptiScaler.dll` produced), no errors.
## Revision 2 (2026-09-16, before step 12 was ever run)

In-game testing (not yet the formal step-12 check, just the user trying the answer-only build)
found SGSR1-answer-only still visibly blurrier than the original both-sides behaviour --
contradicting the theoretical read above (proxy is "just a scalar gate"/"unused by Replace").
Rather than debate the theory further, added a third method value instead of replacing the
second: `DlssNrReducedUpscaleMethod` is now 0 = Bilinear (both implicit), 1 = SGSR1 answer only
(default, unchanged), 2 = SGSR1 both sides (restores the original all-SGSR1 behaviour from
before this plan, as an explicit costlier choice). This re-adds `sgsr1UpProxy`/`proxyNative`
(removed by Revision 1) but only constructs/dispatches them when method == 2, so method 1 still
gets the halved GPU cost Revision 1 was for. Menu combo relabeled to a 3-item list ("Bilinear
(fast)" / "SGSR1 (output only)" / "SGSR1 (input + output, sharpest)"); `Config.h` doc-comment
and the resolve-side `resolveProxy` ternary updated accordingly. Debug + Release x64 rebuilt
clean after this change.

13. [x] In-game check: set model resolution to e.g. 70%, cycle through all three options.
    Confirm the log shows Bilinear as "answer bilinear, proxy bilinear" (no SGSR1 instance),
    method 1 as "answer engaged, proxy bilinear" (one pass), and method 2 as "answer engaged,
    proxy engaged" (two passes) -- i.e. method 1 costs strictly less than method 2, not the
    same. Confirm method 2 looks at least as sharp as the original pre-toggle behaviour, and
    that method 1 sits visibly between Bilinear and method 2 (the actual open question --
    whether "output only" earns its keep as a middle option or should just be dropped in favor
    of always going straight from Bilinear to "both"). If Matched residual is also enabled,
    confirm it now visibly does something under methods 1/2 too (previously a silent no-op
    whenever SGSR1 engaged on any side). Confirm the ini round-trips all three values. —
    verify: manual play-test, log inspection, ini inspection.
    **Partially confirmed, disclosed rather than claimed.** User's actual in-game pass focused
    on the edge-threshold slider rather than the full three-method/log/ini checklist above: at
    the slider's top end (0.300), the SGSR1 enlarge visibly reads as "almost the same as
    bilinear" -- consistent with the tunable's own theory (higher threshold votes fewer pixels
    onto the edge-reconstruction branch, converging toward the plain-bilinear fallback as it
    rises) and a real, if informal, confirmation that the threshold does what its design says.
    Not separately re-confirmed in this pass: the per-method log wording, method 1 vs method 2
    GPU-cost/sharpness delta, Matched residual's un-broken behaviour, or ini round-trip -- these
    were established by code inspection (Revision 2's own log-line implementation, `readFloat`/
    `SetValue` wiring) rather than a fresh in-game check this session.

## Revision 4 (2026-09-16, after the edge-threshold in-game check)

User's in-game read on the edge-threshold slider: 0.300 (the slider's UI maximum) looks "almost
the same as bilinear," and was confirmed as the value to ship as the default -- a deliberate
retune away from upstream's own 8/255 (~0.031), not a bug fix. Changed
`DlssNrSgsr1EdgeThreshold`'s default (`Config.h`) and its menu Reset button's target
(`DlssNr_Menu.cpp`) from `8.0f / 255.0f` to `0.3f`; HelpMarker text updated to explain the new
default came from in-game testing rather than citing upstream's unmodified constant. Slider
range (0.0-0.3) and `EdgeSharpness`'s default (2.0, untouched) were not part of this retune.
Both Debug and Release x64 rebuilt clean (exit 0, no new errors) after the change.

Also folded in this session: working-tree changes for this task had been accidentally left
uncommitted and briefly stashed under `feat/dlssnr-upscale-method-toggle`'s description but
applied to the sibling `feat/dlssnr-detail-transfer` branch (a separate, never-implemented
idea) after a stash pop. Both branches pointed at the same commit (`2e49472a`, no divergent
history), so `git checkout feat/dlssnr-upscale-method-toggle` safely carried the uncommitted
changes back to the correct branch with no merge or loss of work.

## Revision 3 (2026-09-16, before step 13 was ever run)

In-game screenshots (same scenes, Bilinear vs SGSR1-answer-only vs SGSR1-both) confirmed both
SGSR1 options visibly smooth fine, noisy texture (skin pores, individual hair/dreadlock strands,
fabric weave) compared to Bilinear -- not a silent-fallback bug (log confirmed `answer engaged`).
User pushed back on defaulting to Bilinear without first checking whether this is a genuine
implementation problem or just untuned parameters. Found `sgsr1.hlsl`'s `kEdgeThreshold` (8/255)
and `kEdgeSharpness` (2.0) were hardcoded compile-time constants, never exposed -- upstream's own
defaults, "tuned against ordinary gamma-encoded content," almost certainly too sensitive for
photoreal skin/hair (a 3% local luma difference, well within normal skin/pore/hair variation,
is enough to make the vote fire and engage the smoothing 12-tap reconstruction on nearly every
pixel in such regions).

Made both live-tunable instead of guessing at new fixed values: added `EdgeThreshold`/
`EdgeSharpness` to `Sgsr1Constants` (`SGSR1_Dx12.cpp`) and the HLSL cbuffer (`sgsr1.hlsl`),
threaded through `SGSR1_Dx12::Dispatch()`'s signature, new `Config` fields
(`DlssNrSgsr1EdgeThreshold`/`DlssNrSgsr1EdgeSharpness`, defaulting to upstream's original 8/255
and 2.0 so behavior is unchanged until touched), and two new menu sliders gated on SGSR1 being
active for at least one side. Recompiled `sgsr1.hlsl` (dxc + create_header.py), rebuilt Debug +
Release x64 clean. Not yet in-game tested -- next step is the user raising the threshold live to
see whether it restores texture detail while keeping real edges (headband silhouette, hairline)
clean, which would confirm this was a tuning gap rather than something structurally wrong with
the algorithm or its port.

## Notes

If the user later reports the general stutter persists with Bilinear selected, that rules out
the SGSR1 pass specifically and points elsewhere -- this toggle doubles as a clean diagnostic
isolation tool for that open question, not just a settings feature.
