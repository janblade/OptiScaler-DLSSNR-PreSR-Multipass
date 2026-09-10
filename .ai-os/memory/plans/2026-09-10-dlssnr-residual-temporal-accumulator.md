# Plan: MV-reprojected temporal accumulator for the pre-SR NR residual (ResidualAcrossRR v2)

- Branch: feat/dlssnr-presr-residual-across-rr (continues; v1 committed first as a checkpoint)
- Created: 2026-09-10
- Status: in-progress
- Task file: .ai-os/memory/tasks/dlss-neural-rendering.md
- Supersedes: `.ai-os/memory/plans/2026-09-10-dlssnr-presr-residual-across-rr.md` (v1, additive — tested, flickers)

## Context

**Why v1 failed, precisely.** With `noisy_t = clean + n_t` and NR denoising well,
`NR(noisy_t) ≈ clean + enhancement_t`, so the captured residual is

```
Δ_t = NR(noisy_t) − noisy_t ≈ enhancement_t − n_t
```

`Δ_t` is **dominated by `−n_t`** — "subtract this frame's ray-trace noise". Adding that onto
RR's already-denoised output re-injects exactly the noise RR removed. That is the observed
grain flicker, and it is arithmetic, not a bug (v1 is byte-identical at `TransferStrength=0`,
so the wiring is correct).

**v2 fix.** `n_t` is temporally uncorrelated and averages to zero; `enhancement_t` follows
geometry and persists. Separate them with a motion-vector-reprojected temporal accumulator:
keep a persistent render-resolution **enhancement layer**, reproject it by the game's motion
vectors each frame, and blend the current `Δ_t` in at a low rate α (~0.08). The running mean
converges to `E[Δ] = enhancement_mean − 0` — the noise cancels, the enhancement survives, and
the layer is now as temporally stable as RR's own output. That accumulated layer is upscaled
and added after RR+SR.

**Carrier is a raw signed FP16 delta, not the mode-5 compressed carrier.** The `0.5 + 0.5·d/(1+|d|)`
compression exists only because ResidualFG pushes its carrier through a private DLSS SR feature
that needs `[0,1]`. Our path has no DLSS in between, and averaging in that non-linear space
would bias the mean — defeating the noise cancellation. Raw delta also removes the
`1/(1−|x|)` pole blow-up. v1's modes 5/6 are therefore dropped from this path.

**Pipeline.** Pre-SR seam (v1 plumbing retained: two-seam gate, `DLSSD.Color` left untouched,
resolve redirected to `g_nr.residualEdited`, `FinishColor(false)`):

1. encode + model + resolve → edited frame in `g_nr.residualEdited` *(already built in v1)*
2. **mode 8 `NormalizeMotion`** *(exists)*: raw MV → `g_nr.residualMotion` (normalized, validity in `.a`)
3. **NEW mode 13 `AccumulateDelta`**: reads `gSource`=`hdrCopy` (original), `gModel`=`residualEdited`,
   `gOriginal`=`residualHistory[prev]`, `gMotion`=`residualMotion`. Computes `Δ = edited − original`
   inline, reprojects history to `uv + motion` with `gLinear`, and writes
   `lerp(history, Δ, gResidualBlend)` into `residualHistory[cur]`. Invalid reprojection
   (bad `.a`, non-finite, out of bounds) → **history treated as 0** (fade to no edit; it
   rebuilds over the next frames). `frame.Reset` clears both history textures.

Post-SR seam:

4. `OS_Dx12` upscale `residualHistory[cur]` → `g_nr.residualDeltaHi` (output resolution)
5. **NEW mode 14 `ApplyDelta`**: `gTarget = gSource + gModel · gTransferStrength`, guarded, into
   `g_nr.residualComposed`; `CopyResource` back over `Output`.

Four SRV slots (t0–t3) are free, which is what lets the delta be computed inside the accumulate
pass — one dispatch and one texture fewer than a separate capture step. **2 new HLSL modes + one
new constant (`gResidualBlend`)**, then regenerate the DX12 and Vulkan shader blobs with the
vendored `shader_tools` (dxc cs_6_0 + `create_header.py`; the Dx11/fxc leg is not used by DLSS-NR).

**Decisions taken (brainstorm):** invalid reprojection fades to no edit; v2 **replaces** v1
(its plumbing is reused, its modes 5/6 usage is dropped); **DX12 only** — native Vulkan is a
later plan, though the shader regen already emits the SPIR-V.

**Expectations.** The ceiling is "complementary to post-RR NR", not "beats it" — pre-SR NR and
RR are overlapping neural reconstructors. Known artifacts by construction: enhancement lags on
fast motion, fades briefly at disocclusions, and view-dependent detail (moving speculars) is
smeared by the temporal mean. Merge bar is unchanged from v1: **correct wiring, user judges
visuals**; lands experimental/draft.

### Acceptance criteria
- [ ] `ResidualAcrossRR=false` (default): every existing path byte-for-byte unchanged, including
      DeferredDLSS/ResidualFG, which keep sole ownership of modes 5/6.
- [ ] `TransferStrength=0` with the mode on → `Output` byte-identical to RR-only.
- [ ] With the mode on: the accumulated layer is temporally stable — on a static scene with RR on,
      consecutive frames' `residualHistory` converge (no per-frame grain), and the composite shows
      no flicker at the strength the user selects.
- [ ] Camera cut / `frame.Reset` clears the history; no smear carried across the cut.
- [ ] Reprojection-invalid pixels contribute zero edit (no localised grain at disocclusions).
- [ ] `gResidualBlend` reaches the shader and changes accumulation speed (α=1 reproduces v1's
      per-frame behaviour, i.e. flickers — a usable self-check).
- [ ] Release x64 + Debug x64 build, 0 errors, dlssnr-TU warning count == baseline (26 / 63).
      Regenerated `precompile/DlssNr_Shader.{h,cso}` and `DlssNr_Shader_Vk.{h,spv}` committed.

### Out of scope
- Native Vulkan host wiring (shader blobs are regenerated, host path is a later plan).
- Neighbourhood clamping / variance-based history rectification (TAA-grade rejection) — the
  fade-to-zero policy is v2's answer; clamping is a possible v3.
- The multiplicative-ratio carrier (v1's deferred follow-up) — obsoleted by the raw-delta form.
- Any change to DeferredDLSS/ResidualFG behaviour, the white-point sources, or the HDR
  colour-space decision.
- Making the effect look good — the user's call in-game.

## Steps

1. [ ] **Commit v1 as a documented checkpoint** on `feat/dlssnr-presr-residual-across-rr`:
   the DX12 additive path + config key + overlay toggle as tested, with a commit message
   recording that it flickers and why. Keeps the v2 diff reviewable. — verify: `git log`
   shows the checkpoint; working tree clean.

2. [ ] **De-risk the shader regen before depending on it.** Run
   `shader_tools/build_precompiled_shader.bat DlssNr` (dxc leg only) and
   `build_precompiled_shader_vk.bat DlssNr` from `shaders/dlssnr/precompile/` against the
   **unmodified** HLSL; confirm the regenerated `DlssNr_Shader.{cso,h}` /
   `DlssNr_Shader_Vk.{spv,h}` still build and run identically (byte-identical is ideal;
   a compiler-version delta is acceptable if the solution builds and the diff is only the
   blob). Do **not** run the fxc/Dx11 leg — DLSS-NR has no Dx11 blob. — verify: solution
   builds Release x64 0 errors with freshly generated blobs; `git diff --stat` on the blobs
   inspected and understood before proceeding.

3. [ ] **HLSL: new constant + two modes.** In `precompile/dlssnr.hlsl` add
   `float gResidualBlend;` to the `Params` cbuffer (append at the end, mirroring order), and:
   - `gMode == 13` (AccumulateDelta): `Δ = SanitizeFinite3(gModel.Load − gSource.Load, 0)`;
     `mv = gMotion.Load(int3(id.xy,0))`; `prevUV = uv + mv.xy`;
     `valid = mv.a > 0.999 && all(isfinite(mv.xy)) && all(prevUV >= 0) && all(prevUV <= 1)`;
     `history = valid ? gOriginal.SampleLevel(gLinear, prevUV, 0).rgb : 0`;
     `a = valid ? saturate(gResidualBlend) : 1.0`; `gTarget = float4(lerp(history, Δ, a), 1)`.
   - `gMode == 14` (ApplyDelta): `base = gSource.Load`; `d = SanitizeFinite3(gModel.Load, 0)`;
     `gTarget = float4(max(base.rgb + d * gTransferStrength, 0.0), base.a)`.
   Mirror `gResidualBlend` as `float ResidualBlend;` in `DlssNrConstants`
   (`DlssNr_Common.h:153`, `alignas(256)`) at the matching offset, and add
   `DlssNrMode_AccumulateDelta = 13` / `DlssNrMode_ApplyDelta = 14` to the enum. — verify:
   C++ struct field order matches the cbuffer exactly (offset-by-offset read of both).

4. [ ] **Regenerate the blobs** (DX12 + Vk legs, per step 2's procedure). — verify: four
   artifacts updated; solution builds Release x64 0 errors.

5. [ ] **Config knob.** `CustomOptional<float> DlssNrResidualAcrossRrBlend { 0.08f }` in
   `Config.h` beside `DlssNrResidualAcrossRr`; read/write in `Config.cpp`;
   `ResidualAcrossRRBlend=auto` + comment in `OptiScaler.ini`. Clamp to `[0.01, 1.0]` at use.
   — verify: round-trips through ini save→load.

6. [ ] **Host, pre-SR seam (DX12).** Replace v1's mode-5 capture in `DlssNr_Dx12::Dispatch`
   with: mode-8 `NormalizeMotion` (raw MV → `g_nr.residualMotion`), then mode-13
   `AccumulateDelta` reading (`g_nr.hdrCopy`, `g_nr.residualEdited`,
   `g_nr.residualHistory[prev]`, `g_nr.residualMotion`) → `g_nr.residualHistory[cur]`;
   flip the ping-pong index; carry `MvScaleX/Y` and guide sizes into the mode-8 constants the
   way the resolve already does. Clear both history textures when `frame.Reset`. Stamp
   `residualStoreFrame` / `residualStoreValid` as v1 did. Drop `g_nr.residualStore`. — verify:
   with the model forced off, `residualHistory` converges to 0; `DLSSD.Color` readback ==
   input; α=1 reproduces v1 behaviour.

7. [ ] **Host, post-SR seam (DX12).** In `ApplyResidualAcrossRr`, upscale
   `g_nr.residualHistory[cur]` → `g_nr.residualDeltaHi` via `g_nr.residualUp`, then mode-14
   `ApplyDelta` (`InSource`=`output`, `InModel`=`residualDeltaHi`) → `g_nr.residualComposed`,
   `CopyResource` back to `output`, same arrival-state handling as v1. Drop the mode-6 path.
   — verify: `TransferStrength=0` → `Output` byte-identical to RR-only; stale/missing history
   → no-op.

8. [ ] **Resource lifetime.** In `NrState`: add `residualMotion`, `residualHistory[2]`,
   `residualDeltaHi`, `residualHistoryIndex`; remove `residualStore`, `residualStoreHi`.
   Add all to `ReleaseSurfacesIfFormatChanged`, the resolution/placement park block, and
   `Shutdown`. Allocate in `Dispatch` under `residualAcrossRr` (render-res: `residualEdited`,
   `residualMotion`, `residualHistory[2]`; output-res: `residualDeltaHi`, `residualComposed`),
   keeping v1's alloc-failure downgrade. — verify: builds; toggling the mode and changing
   resolution leaks nothing and never binds a freed resource.

9. [ ] **Logging.** Update the one-time engage line to name v2 and the blend rate
   (`residual-across-RR active -- MV-reprojected accumulator, blend {:.2f}`); one-time line
   when history is cleared by a reset. — verify: a forced run shows both.

10. [ ] **Overlay.** Add a "Detail accumulation rate" slider under the existing
    `ResidualAcrossRR` checkbox (range 0.01–1.00, default 0.08), disabled with the checkbox,
    with a HelpMarker explaining low = stabler but slower to appear, 1.0 = no accumulation.
    — verify: slider drives the config value; disabled state tracks the checkbox.

11. [ ] **Build.** Release x64 then Debug x64 with the 64-bit MSBuild. — verify: "Build
    succeeded", 0 errors, dlssnr-TU warning delta 0 vs baseline (26 / 63).

12. [ ] **Docs.** Rewrite the "Across-RR residual" subsection in
    `OptiScaler/dlssnr/design/pre-sr-multipass.md` for v2 (the `Δ ≈ enhancement − n_t`
    derivation, the accumulator, fade-to-zero policy, the three known artifacts, and that v1
    additive was tested and flickers). Note the key + blend knob in
    `docs/NR-COMPATIBILITY.md` and `docs/NR-REQUIREMENTS-MATRIX.md` §2. — verify: text matches
    shipped key names and defaults.

13. [ ] **Cold self-review** of the full v2 diff against the acceptance criteria; labelled
    note (dev-loop Path B). — verify: note written.

14. [ ] **Independent Review Pass** (`core.dev-loop.sk`) on the accumulated diff with this
    `## Context` as the spec; one line naming the branch taken; findings surfaced,
    non-blocking. — verify: report relayed.

## Progress log
- 2026-09-10: plan drafted. Recon established: mode 9 has the reprojection pattern to copy
  (`prevUV = uv + motion`, `SampleLevel(gLinear, ...)`, validity via `.a`/finite/bounds) but
  composes motion vectors, not colour; mode 8 produces the normalized MV field and is reused
  as-is; SRV slots t0–t3 are all free, so the delta is computed inside the accumulate pass;
  `DlssNrConstants` is `alignas(256)` so one added float is free; shader toolchain
  (`dxc.exe`, `create_header.py`, Python 3.13.14) verified present. Awaiting approval.
