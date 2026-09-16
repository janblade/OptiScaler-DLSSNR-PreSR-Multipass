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
