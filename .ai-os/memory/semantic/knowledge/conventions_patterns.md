# Conventions & Patterns

> Coding standards, security rules, and testing patterns.

- **Any new ratio-based luminance edit in `dlssnr.hlsl`'s resolve pass must floor both the
  numerator and the denominator by the same small constant, not just the denominator.** The
  file's own established idiom is `kRatioFloor = 1.0 / 512.0` (declared once, reused): a bare
  ratio against near-black luminance is unbounded, and flooring only the denominator does not
  tame it -- the numerator can still swing arbitrarily negative and invert the ratio's sign,
  which `max(x, 0.0)` then clamps to flat black. Flooring both sides
  (`(numeratorTerm + kRatioFloor) / (denominatorTerm + kRatioFloor)`) leaves bright pixels
  alone and lets the ratio settle to 1 as luminance approaches zero instead. The existing
  `lumaRatio` computation uses this correctly; `feat/dlssnr-replace-detail-injection`'s first
  draft of a new `detailRatio` term didn't reuse it (used a bespoke one-sided-floor ratio
  instead) and could crush shadow pixels near contrasty edges to black -- caught by an
  independent Review Pass, not the author, and fixed by switching to the same dual-floor
  shape as `lumaRatio`. Reuse `kRatioFloor` itself (already in scope through the whole
  resolve function) rather than restating the literal.

- **A config value with a semantic ceiling (e.g. `DlssNrMaxRatio`/"Highlight guard", bounded
  to `[1.0, 8.0]`) must be clamped at every consumption site, not just enforced by the UI
  slider's `min`/`max`.** Narrowing a slider's range only bounds *future* writes through that
  control -- a value already stored beyond the new range (from before the slider was
  narrowed, or from a hand-edited `.ini`) stays exactly as out-of-range as before, and any
  consumer that reads it with `value_or_default()` and no clamp applies it unbounded
  regardless of what the menu now shows. `DlssNr_Late.inl` and `DlssNr_DeferredSr.inl`
  already did `std::clamp(cfg.DlssNrMaxRatio.value_or_default(), 1.0f, 8.0f)` at their call
  sites; `DlssNrFeature_Vk.cpp` and `DlssNr_Dx12.cpp` didn't, found by a `code-review` pass
  after a menu fix tightened the slider's range from 30x to 8x and assumed that was
  sufficient. Fixed by matching the clamp at all four sites. When a config field's UI range
  changes to enforce a new ceiling, grep every other reader of that field and clamp there
  too -- the widget's range is not the enforcement mechanism.
