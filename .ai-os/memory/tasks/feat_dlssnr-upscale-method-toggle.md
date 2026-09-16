# Task Memory — feat/dlssnr-upscale-method-toggle

Branched off `main` (`2e49472a`, clean tree), 2026-09-16.

Active plan: memory/plans/2026-09-16-dlssnr-upscale-method-toggle.md (approved, 0/5)

## Context

Follow-on from investigating a general in-game stuttering report. `feat/dlssnr-sgsr1-upscale`'s
new enlarge pass (`sgsr1.hlsl`) is a real edge-directed upscale (Gather taps + up to 12
`weightY`/`fastLanczos2` calls per pixel when the edge vote fires), dispatched at full native
output resolution twice a frame (answer + proxy) whenever model resolution is below 100% --
a genuine steady-state GPU cost that didn't exist before that feature (the old path was an
implicit bilinear tap inside the resolve shader itself, still there and still used as SGSR1's
own build-failure fallback). User asked to make this user-selectable rather than removing it,
so people who don't need the extra sharpness (e.g. Composed mode, where the ratio/hue blend
already covers most of the softness) can opt back into the cheaper path.

Implementation is small because the fallback already exists and is correct: `gSource`/`gModel`
in `dlssnr.hlsl`'s resolve are read via `SampleLevel` with a bilinear sampler regardless of the
bound texture's actual size (`EditAt()`, `dlssnr.hlsl:308-311`), so simply not running SGSR1
and handing the resolve the still-small `modelInput`/`finalAnswer` reproduces the old bilinear
behaviour exactly, with zero shader changes needed.

Chosen scope/defaults (via `AskUserQuestion`, both "Recommended"): default stays SGSR1 (no
visual regression for existing users); toggle covers only the reduced/up-leg (`workScale < 1`),
not the pre-existing supersample leg's `DlssNrScalingDownscaler` filter choice, which is a
separate, already-configurable thing.

Side effect worth knowing (not something this task needs to fix): picking Bilinear also
un-breaks "Matched residual" (`DlssNrTransfer == 1`), which is silently a no-op today whenever
SGSR1 engages -- see `known_gotchas.md`'s stale-gate entry (`modelRanSmall` reads false once
SGSR1 hands the resolve a native-sized buffer). Worth a cross-reference note at close, not a
fix in this task.

## Closing note (TASK_CLOSE)

Shipped: a 3-way `DlssNrReducedUpscaleMethod` (Bilinear / SGSR1 output-only / SGSR1 both sides)
plus live-tunable `DlssNrSgsr1EdgeThreshold`/`DlssNrSgsr1EdgeSharpness`, after two in-game-driven
revisions changed the original 2-way answer-only design (see the plan file's Revision 1-3 notes
for the full trail: answer-only alone tested visibly blurrier than expected, prompting the third
method; the residual blur was then traced to upstream's fixed edge-vote threshold being tuned
for ordinary content, not photoreal skin/hair noise, prompting the tunables).

In-game A/B on the new threshold slider confirmed it behaves as designed -- raising it votes
fewer pixels onto SGSR1's edge-reconstruction branch, converging toward plain bilinear as it
approaches the slider's 0.3 ceiling. User picked 0.300 (not upstream's 8/255, about 0.031) as
the value to ship as the default, a deliberate retune rather than a bug fix; `EdgeSharpness`'s
default (2.0) was left untouched. Both configs rebuilt clean after the change.

Not independently re-confirmed this session (established by earlier code inspection instead,
disclosed rather than claimed as freshly verified): the per-method engagement log wording,
method-1-vs-method-2 GPU cost/sharpness delta, Matched residual's un-broken behaviour under
SGSR1, and ini round-trip of the new keys.

Also resolved: this task's working-tree changes had been sitting stashed and were accidentally
popped onto the sibling `feat/dlssnr-detail-transfer` branch instead of this one. Both branches
pointed at the same commit with no divergent history, so switching back to
`feat/dlssnr-upscale-method-toggle` safely carried the changes over without any merge or loss
of work.
