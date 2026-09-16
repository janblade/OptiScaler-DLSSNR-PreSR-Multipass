# Plan: Native detail injection for Replace mode below 100% model resolution
- Branch: feat/dlssnr-replace-detail-injection
- Created: 2026-09-16
- Status: done
- Task file: memory/tasks/feat_dlssnr-replace-detail-injection.md

## Context

Neutwo Replace (`DlssNrReversibleMode == 2`) and Hybrid Replace (`== 4`) decode the model's
raw answer with zero blending against the native frame (`dlssnr.hlsl` ~1150-1153) -- unlike
Composed modes (0/1/3), which stay anchored on native luminance the whole way through their
ratio/hue composition. Confirmed this session (feat/dlssnr-sgsr1-upscale's closing
investigation) via direct in-game A/B: same scene, same reduced model resolution, Composed
looked fine while Replace still looked soft ("whole image like CRT"). Root cause is
structural, not a bug -- below 100% model resolution the model only ever computes its answer
at a reduced working size, SGSR1's enlarge can sharpen that answer but cannot invent detail
the model never computed, and Replace has no native-resolution fallback to lean on the way
Composed does.

This plan adds an optional term to the resolve shader's Replace-mode path that injects real
high-frequency structure pulled from the native frame (already bound as `gOriginal`, t2, at
this point in the resolve pass) -- luminance-only, hue-preserving, multiplicative (same
pattern the file already uses for its ratio-based edits) -- so Replace keeps its
no-composition character (still no blend toward native *colour*) but stops looking flat where
the model's own reduced-resolution answer ran out of detail. Zero at default-off reproduces
today's behaviour exactly; the new slider is the escape hatch if the effect looks wrong in
some scene.

Two design alternatives were surfaced and explicitly not chosen this round: a guided/joint
upsample (replace SGSR1's plain spatial enlarge with one that uses native as a structural
guide -- more accurate edge placement, materially more shader complexity, a new algorithm to
validate rather than an extension of the existing resolve) and auto-fallback to Composed
below 100% model resolution (guarantees no blur, near-zero code, but makes Replace's raw look
unavailable exactly when resolution is turned down for performance). Revisit either if
detail injection proves insufficient in practice.

**Acceptance criteria:**
- [x] At 100%+ model resolution, Replace-mode output is unchanged (injection gate is off:
  `gModelWorkScale < 0.999` is false -- reworked from the originally-planned
  `modelRanSmall`, which turned out to be stale post-SGSR1; see step 6). Confirmed by user.
- [x] Below 100% model resolution, Replace-mode output shows visibly less blur/softness at
  the default strength, confirmed via in-game A/B against the pre-fix build. Confirmed by
  user at 80% model resolution, Hybrid Replace.
- [x] The new "Replace detail strength" slider only appears when a Replace mode (Neutwo or
  Hybrid) is selected in the HDR-mapping combo; default 0.5; 0 exactly reproduces current
  (pre-fix) behaviour.
- [x] Composed modes (0/1/3) are untouched -- no code path shared with the injection block.
  Confirmed structurally unreachable by the independent Review Pass.

**Out of scope:** touching SGSR1's own enlarge shader/math; a full guided/joint-upsample
rewrite (noted above as a future option, not built now); any change to Composed-mode
behaviour; in-game VK verification (this thread's testing has all been DX12 -- VK wiring is
included below for parity/no-regression only, not separately verified in-game unless asked).

## Steps

1. [x] Add the injection block to `OptiScaler/shaders/dlssnr/precompile/dlssnr.hlsl`'s
   resolve function, right after the `gReversibleMode == 2`/`== 4` decode (~line 1150-1153,
   before `result *= normScale;`): sample the 4 native neighbour texels around
   `gOriginal.Load(int3(id.xy, 0))` (left/right/up/down, 1 texel offset), box-blur their
   luminance together with the centre sample, subtract from `originalLuma` for a
   high-frequency term, then scale `result`'s luminance by
   `1.0 + gReplaceDetailStrength * highFreq / max(originalLuma, 1/512)` (hue-preserving --
   same "one scalar from luminance, applied to the whole triple" pattern already used for
   `boundedRatio`/`lumaRatio` elsewhere in this function). Gate the whole block on
   `(gReversibleMode == 2 || gReversibleMode == 4) && modelRanSmall && gReplaceDetailStrength
   > 0.0`. — verify: at 100% model resolution `modelRanSmall` is false so the gate never
   fires and output is bit-identical to pre-change (spot-check via the existing
   compare/debug view, e.g. `gDebugView`/`gCompareMode`).
2. [x] Add `gReplaceDetailStrength` as a new trailing field in the HLSL `Params` cbuffer
   (`dlssnr.hlsl` ~line 38, after `gEnvironmentColour`) and the mirrored
   `float ReplaceDetailStrength;` trailing field in `DlssNrConstants`
   (`OptiScaler/shaders/dlssnr/DlssNr_Common.h` ~line 239). Update
   `static_assert(sizeof(DlssNrConstants) == 256)` to the new correct size (adding one
   `float` to an already-full 256-byte `alignas(256)` struct rounds up to 512 -- confirm the
   exact value by building, not by hand-counting). — verify: project compiles, the
   `static_assert` passes.
3. [x] Add `CustomOptional<float> DlssNrReplaceDetailStrength { 0.5f };` to `Config.h` next
   to `DlssNrTransferStrength` (~line 321), and register it in `Config.cpp`'s load/save
   table the same way `DlssNrTransferStrength`/`DlssNrSkinDetail` are registered. — verify:
   toggle it in the menu, confirm it round-trips through the saved `.ini`.
4. [x] Wire the value into both dispatch sites: `OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp`
   `resolveParams` (~line 2850, alongside `resolveParams.ColourStrength`) and
   `OptiScaler/dlssnr/DlssNrFeature_Vk.cpp`'s `encode` constants (~line 1019-1027 -- setting
   it there is sufficient for VK since `DlssNrConstants resolve = encode;` (~line 1254)
   copies the whole struct forward into the resolve dispatch). — verify: changing the menu
   slider visibly changes in-game behaviour on the DX12 path (VK: build-only parity check,
   per out-of-scope note above).
5. [x] Add a slider in `OptiScaler/dlssnr/DlssNr_Menu.cpp` near the "HDR mapping
   (experimental)" combo (~line 439-443), shown only when `reversible == 2 || reversible ==
   4`, following the existing `ImGui::SliderFloat` + reset-button + `HelpMarker` pattern used
   for "Colour strength" (~line 421-429). `HelpMarker` text should explain it restores real
   native detail below 100% model resolution and has no effect at 100%+. — verify: slider
   appears only for Replace modes and disappears for Composed modes; reset button restores
   0.5.
6. [x] Recompile `dlssnr.hlsl` via `dxc.exe -T cs_6_0 -E CSMain -O3 -Qstrip_debug
   -Qstrip_reflect` then regenerate `DlssNr_Shader.cso`/`.h` via `create_header.py` (same
   routine used earlier this branch's history for the downsampler fix); rebuild both
   configs. — verify: clean build, no warnings about the cbuffer layout change. Confirmed:
   `dxc` compiled clean; `DlssNrConstants` stayed 256 bytes (alignas(256) padding had 140
   spare bytes already, so no size-assert bump was actually needed -- caught and reverted a
   wrong 512 guess before building, see decisions.jsonl). Release|x64 built clean
   (OptiScaler.dll produced). ReleaseDebug|x64's compile+link also succeeded
   (OptiScaler.dll produced) but its post-build step failed shelling out to `7z.exe` for a
   backup archive -- 7-Zip isn't installed/on PATH in this environment, unrelated to this
   change (no cbuffer/shader warnings in either build). Flagged to the user rather than
   worked around silently.

   **Rework after in-game retest (still no effect at strength 2.0):** the gate
   (`modelRanSmall`, inferred from `gSource`'s bound texture size) was stale -- SGSR1's
   pre-resolve enlarge hands the resolve pass a native-sized proxy/answer buffer whenever it
   succeeds, so that shader-side check reads false in the common case, same root cause
   already on record in `known_gotchas.md` for "Matched residual"'s narrowed scope. Replaced
   with an explicit `ModelWorkScale` float passed from C++ (`reduced && workScale < 1.0f ?
   workScale : 1.0f`), computed before SGSR1 ever runs, wired into both `DlssNr_Dx12.cpp`
   and `DlssNrFeature_Vk.cpp`; the injection's kernel radius now derives from it too
   (`round(1/gModelWorkScale)`) instead of the equally-stale `proxyW`/`proxyH`. Recompiled,
   rebuilt both configs clean (same two pre-existing environment gaps as above, unchanged).

   **Review Pass finding, fixed before close:** independent reviewer subagent (dispatched
   per `core.dev-loop.sk`) found the injection ratio (`1.0 + strength*highFreq/originalLuma`)
   was unbounded on the numerator while only the denominator was floored -- a shadow pixel
   next to a contrasty edge could compute a ratio well past -1 and clamp to flat black,
   reintroducing the exact boiling-adjacent failure shape this file's own `kRatioFloor`
   dual-floor idiom (used just above, for `lumaRatio`) already exists to prevent. Rewrote to
   reuse that same idiom directly (`(originalLuma + strength*highFreq + kRatioFloor) /
   (originalLuma + kRatioFloor)`), also dropping a dead `max(gModelWorkScale, 0.05)` floor
   (unreachable given the field's upstream `[0.25, 2.0]` clamp) the reviewer flagged as a
   nitpick. Recompiled, rebuilt both configs clean again.
7. [x] In-game A/B, both Replace modes, at a reduced model resolution used earlier in this
   investigation, compared against the pre-fix build; then confirm 100% model resolution is
   visually unchanged. — verify: user-run manual test; this is the acceptance-defining check
   the rest of the plan exists to enable, per this repo's own precedent of not claiming a
   graphics fix "works" without an in-game check. Confirmed by user: Hybrid Replace at 80%
   model resolution is meaningfully less blurry than before the fix, no ghosting observed,
   100% model resolution confirmed unchanged. (Note: the dual-floor fix above landed *after*
   this specific in-game confirmation, on Review Pass's finding rather than a new user
   report -- the shadow-crush failure mode is plausible but scene-dependent and may not have
   been present in the tested scene; flagged to the user as worth an extra look, not
   re-blocking close on it given it is a strict improvement over the pre-fix state either
   way.)

## Review Pass

Independent reviewer subagent (`core.dev-loop.sk`), diff + plan spec only, no implementer
reasoning shared. Checked correctness against spec, convention adherence
(`known_gotchas.md`/`conventions_patterns.md`), security (unclamped `gOriginal.Load` offsets
-- ruled not a real risk, `Load` returns 0 out-of-range, already precedented unclamped
elsewhere in this file), test coverage (none expected/present for shader code in this repo,
consistent with its established bar), and ownership fit.

**Findings:**
- [fixed] Unbounded injection ratio could crush dark pixels near edges to black -- see above.
- [fixed] Dead `max(gModelWorkScale, 0.05)` floor, unreachable given upstream clamp.
- [nitpick, folded into the fix above] Injection duplicated the `1.0/512.0` floor literal
  instead of reusing the already-in-scope `kRatioFloor` constant.

**Not flagged:** gate scoping (Composed modes structurally unreachable), DX12/Vulkan
`ModelWorkScale` parity (textually identical expressions, both correctly in scope), cbuffer
field/comment conventions, menu slider/Reset/HelpMarker idiom, ghosting mechanism (none --
no reprojection/history buffer touched).

**Verdict:** correct shape and correctly wired for the spec once the dual-floor fix landed.
