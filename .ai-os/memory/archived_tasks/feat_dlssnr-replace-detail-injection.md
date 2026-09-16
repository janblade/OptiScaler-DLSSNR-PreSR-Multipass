# Task Memory — feat/dlssnr-replace-detail-injection

Branched off `feat/dlssnr-sgsr1-upscale` (already merged/closed work, clean tree), 2026-09-16.

Active plan: memory/plans/2026-09-16-dlssnr-replace-detail-injection.md (done, 7/7)

## Context

Follow-on from `feat/dlssnr-sgsr1-upscale`'s closing investigation, which concluded Replace
mode's softness below 100% model resolution is structural (no native-resolution fallback,
unlike Composed) rather than a bug -- confirmed via in-game A/B (Composed fine, Replace still
soft, same scene/resolution). User asked to plan a fix rather than accept the ceiling as
final. Chosen approach (via `AskUserQuestion`, both answered "Recommended"): inject real
native high-frequency detail into Replace's resolve output, gated to below-100%-model-res
only, applied to both Neutwo Replace and Hybrid Replace (same structural cause, same fix).

See the plan file for the full step list and design-alternatives note (guided/joint upsample
and auto-fallback-to-Composed were considered and explicitly deferred, not built).

## Closing note (TASK_CLOSE)

Shipped and confirmed in-game (Hybrid Replace, 80% model resolution: meaningfully less
blurry, no ghosting; 100% unchanged). Two real bugs surfaced and fixed during execution, not
just tuning: (1) the injection's gate initially reused the stale `modelRanSmall` shader check
(reads false once SGSR1's enlarge hands the resolve a native-sized buffer -- same root cause
already on record for "Matched residual"), fixed with an explicit C++-computed
`ModelWorkScale`; (2) independent Review Pass caught an unbounded ratio that could crush dark
pixels near contrasty edges to black, fixed by reusing the file's own `kRatioFloor` dual-floor
idiom. Three semantic-memory writes promoted from this task: corrected the prior
"not fixable by further shader work" claim in `architecture_overview.md`, generalized the
stale-gate gotcha in `known_gotchas.md` (now hit twice independently), and seeded
`conventions_patterns.md` with the dual-floor ratio convention (previously empty).
