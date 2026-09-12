# Plan: MV-reprojected temporal accumulator for the pre-SR NR residual (ResidualAcrossRR v2)

- Branch: feat/dlssnr-presr-residual-across-rr (continues; v1 committed first as a checkpoint)
- Created: 2026-09-10
- Status: done
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
pass — one dispatch and one texture fewer than a separate capture step.

**Shader isolation (decided during step 2).** Regenerating `dlssnr.hlsl`'s blob is off the
table: the vendored `dxc` (1.9.2602.17) produces a materially different (9–12 KB smaller) DXIL
than the committed `DlssNr_Shader.cso`, so a regen would swap the whole battle-tested NR shader
for one from a newer compiler — unverifiable here, blast radius = every NR path. Instead the two
new modes live in a **separate `dlssnr_residual.hlsl`** with its own small blob and a **second
compute PSO** in `DlssNr_Dx12` that reuses the existing root signature and descriptor-table
shape. `dlssnr.hlsl` / `DlssNr_Shader.cso` are never touched. (SPIR-V *does* regenerate
byte-identically, but the DX12 leg is the risk and both must stay in step.) The new shader's
cbuffer mirrors `DlssNrConstants` verbatim plus one appended `float gResidualBlend` — which
fits inside the struct's existing 256-byte `alignas` with no size change, so `dlssnr.hlsl` (which
simply never declares that field) is unaffected.

**Decisions taken (brainstorm):** invalid reprojection fades to no edit; v2 **replaces** v1
(its plumbing is reused, its modes 5/6 usage is dropped); **DX12 only** — native Vulkan is a
later plan (its `dlssnr_residual` SPIR-V is generated now but not wired host-side).

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

1. [x] **Commit v1 as a documented checkpoint.** — verify: `git log` shows it; tree clean.
   **DONE 2026-09-10.** Commit `fdeb06bd` "DLSS-NR: ResidualAcrossRR v1 (additive) --
   checkpoint, flickers by design" (8 files, +700/-7; includes both plan files). Working tree
   clean afterwards.

2. [x] **De-risk the shader route.** — verify: measured the regen delta; chose isolation.
   **DONE 2026-09-10.** Regenerating `DlssNr_Shader.cso` from the unmodified `dlssnr.hlsl`
   with the vendored `dxc` 1.9.2602.17: SPIR-V byte-identical, but DX12 CSO 29144 → 19960 B
   unstripped / 16744 B stripped (~3800-line `.h` diff). The committed blob is from an older
   compiler; reflection is unused (`Shader_Dx12` hand-declares the root sig) so stripping is
   safe, but the codegen genuinely differs and cannot be runtime-verified here. **Decision
   (user): separate shader + 2nd PSO** — steps 3–4 below revised. Originals restored, nothing
   committed. `create_header.py` writes CRLF on Windows Python; the repo blobs are LF —
   normalise generated `.h` with `sed -i 's/\r$//'`.

3. [x] **New shader `precompile/dlssnr_residual.hlsl`.** Self-contained compute shader:
   the `Params` cbuffer block copied verbatim from `dlssnr.hlsl` **plus** a trailing
   `float gResidualBlend;`, the `Texture2D` t0–t3 + `RWTexture2D` u0 + `SamplerState` s0
   bindings (same registers as `dlssnr.hlsl`), the `SanitizeFinite3` helper, and
   `[numthreads(8,8,1)] void CSMain`. Its own local mode numbering:
   - `gMode == 0` (Accumulate): `d = SanitizeFinite3(gModel.Load(int3(id.xy,0)).rgb −
     gSource.Load(int3(id.xy,0)).rgb, 0)`; `mv = gMotion.Load(int3(id.xy,0))`;
     `prevUV = uv + mv.xy`;
     `valid = mv.a > 0.999 && all(isfinite(mv.xy)) && all(prevUV >= 0) && all(prevUV <= 1)`;
     `history = valid ? gOriginal.SampleLevel(gLinear, prevUV, 0).rgb : 0`;
     `a = valid ? clamp(gResidualBlend, 0.0, 1.0) : 1.0`;
     `gTarget[id.xy] = float4(lerp(history, d, a), 1)`.
   - `gMode == 1` (Apply): `base = gSource.Load(int3(id.xy,0))`;
     `d = SanitizeFinite3(gModel.Load(int3(id.xy,0)).rgb, 0)`;
     `gTarget[id.xy] = float4(max(base.rgb + d * gTransferStrength, 0.0), base.a)`.
   In `DlssNr_Common.h`: append `float ResidualBlend;` to `DlssNrConstants` (fits the existing
   256-byte `alignas`, no size change — verify `sizeof` stays 256), and add
   `enum DlssNrResidualMode { DlssNrResidualMode_Accumulate = 0, DlssNrResidualMode_Apply = 1 };`.
   — verify: struct field order matches the new cbuffer offset-for-offset; `sizeof(DlssNrConstants)`
   unchanged.
   **DONE 2026-09-10.** `precompile/dlssnr_residual.hlsl` written: `Params` cbuffer verbatim
   from `dlssnr.hlsl` + trailing `float gResidualBlend`, all 8 bindings declared (t4/u1 unused,
   kept for descriptor-table parity), `SanitizeFinite3` copied, `CSMain` modes 0/1 per the
   spec above (invalid reprojection → blend 1.0 so the layer is present and averages down next
   frames). `DlssNr_Common.h`: `float ResidualBlend;` appended to `DlssNrConstants` (29→30
   scalars = 120 B, still rounds to 256 under `alignas(256)` — size unchanged; no
   `static_assert` exists), `enum DlssNrResidualMode` added.

4. [x] **Build `dlssnr_residual` blobs.** — verify: 4 artifacts; `.h` LF.
   **DONE 2026-09-10.** From `precompile/`: dxc `cs_6_0 -O3 -Qstrip_debug -Qstrip_reflect` →
   `dlssnr_residual_Shader.cso` (3152 B) + `.h` (20302 B); dxc `-spirv -D VK_MODE` →
   `dlssnr_residual_Shader_Vk.spv` (6844 B) + `.h` (43994 B). No dxc diagnostics.
   `sed -i 's/\r$//'` applied — both `.h` confirmed LF. **vcxproj NOT edited**: it has no glob,
   and `dlssnr.hlsl` / `DlssNr_Shader.{cso,h}` / `DlssNr_Common.h` / `DlssNr_Dx12.h` are
   likewise absent from it — headers compile via `#include` from the listed `DlssNr_Dx12.cpp`;
   adding entries would be cosmetic and inconsistent with the existing dlssnr layout.

5. [x] **Config knob.** — verify: round-trips through ini save→load.
   **DONE 2026-09-10.** `Config.h:271` `DlssNrResidualAcrossRrBlend { 0.08f }` (+ comment);
   `Config.cpp` read (`readFloat("DlssNr","ResidualAcrossRRBlend")`) and write; `OptiScaler.ini`
   `ResidualAcrossRRBlend=auto` + 3-line comment after `ResidualAcrossRR`. Clamped
   `[0.01,1.0]` at both use sites (accumulate blend, and the engage log). Also softened the v1
   `ResidualAcrossRR` comments in all three files to describe the v2 accumulator.

5a. [x] **Second compute PSO.** In `DlssNr_Dx12` add `ID3D12PipelineState* _residualPipelineState`,
   built in the ctor via `CreateComputePipeline(InDevice, &_residualPipelineState,
   dlssnr_residual_cso, sizeof(dlssnr_residual_cso), nullptr)` (reuses the class root
   signature), and a `DispatchResidualPass(cmdList, const DlssNrConstants&, InSource, InModel,
   InOriginal, InMotion, OutTarget)` that binds `_residualPipelineState` + the same SRV/UAV/
   sampler/CBV table shape `DispatchPass` uses (unused SRV slots get the existing stand-in).
   Released in the dtor next to `_pipelineState`. — verify: builds.
   **DONE 2026-09-10.** `DlssNr_Dx12.h`: `_residualPipelineState` member + public
   `DispatchResidualPass(cmdList, const DlssNrConstants&, InSource, InModel, InOriginal,
   InMotion, OutTarget)`. `DlssNr_Dx12.cpp`: `#include "precompile/dlssnr_residual_Shader.h"`;
   ctor builds the 2nd PSO from `dlssnr_residual_cso` (a failure is `LOG_WARN` + null, not
   fatal); dtor releases it; `DispatchResidualPass` mirrors `DispatchPass`'s table setup,
   binding `_residualPipelineState` and standing `InSource` into the t4/u1 slots.

6. [x] **Host, pre-SR seam (DX12).** Replace v1's mode-5 capture in `DlssNr_Dx12::Dispatch`
   with: `DispatchPass` mode `DlssNrMode_NormalizeMotion` (raw MV → `g_nr.residualMotion`,
   carrying `MvScaleX/Y` + guide sizes the way the resolve does), then `DispatchResidualPass`
   `DlssNrResidualMode_Accumulate` reading (`g_nr.hdrCopy`, `g_nr.residualEdited`,
   `g_nr.residualHistory[prev]`, `g_nr.residualMotion`) → `g_nr.residualHistory[cur]`, with
   `ResidualBlend` from config; flip the ping-pong index. Clear both history textures when
   `frame.Reset`. Stamp `residualStoreFrame` / `residualStoreValid` as v1 did. Drop
   `g_nr.residualStore`. — verify (runtime, deferred to the user): model off ⟹ history → 0;
   `DLSSD.Color` == input; `ResidualBlend=1` reproduces v1's flicker.
   **DONE 2026-09-10.** In the resolve block: `residualEdited` UAV→SRV; mode-8
   `NormalizeMotion` on `motionIn` (`MvScaleX/Y = frame.MvScaleX/Y / width|height`, matching
   the ResidualFG half-rate path) → `residualMotion`; `prev/cur` from
   `residualHistoryIndex & 1`; `DispatchResidualPass(Accumulate, hdrCopy, residualEdited,
   residualHistory[prev], residualMotion) → residualHistory[cur]` with
   `ResidualBlend = residualHistoryPrimed ? clamp(cfg,0.01,1) : 1.0`; index flipped,
   `residualHistoryPrimed=true`, stamp set. `frame.Reset` clears `residualHistoryPrimed` +
   `residualStoreValid`. `FinishColor(... && !residualAcrossRr)` unchanged.

7. [x] **Host, post-SR seam (DX12).** In `ApplyResidualAcrossRr`, upscale
   `g_nr.residualHistory[cur]` → `g_nr.residualDeltaHi` via `g_nr.residualUp`, then
   `DispatchResidualPass` `DlssNrResidualMode_Apply` (`InSource`=`output`,
   `InModel`=`residualDeltaHi`) → `g_nr.residualComposed`, `CopyResource` back to `output`,
   same arrival-state handling as v1. Drop the mode-6 path. — verify (runtime, deferred):
   `TransferStrength=0` → `Output` byte-identical to RR-only; stale/missing history → no-op.
   **DONE 2026-09-10.** `ApplyResidualAcrossRr`: guard on `residualStoreValid` +
   `residualHistory[idx]`/`residualDeltaHi`/`residualComposed` + one-frame freshness;
   `residualUp->Dispatch(residualHistory[idx] → residualDeltaHi)`;
   `g_compose->DispatchResidualPass(Apply, output, residualDeltaHi) → residualComposed` with
   `TransferStrength = clamp(cfg.DlssNrTransferStrength,0,1)`; `CopyResource` back to `output`
   with the `OutputResourceBarrier`-derived arrival state. `residualExposurePreMul` /
   `DlssNrMode_ApplyResidual` dropped.

8. [x] **Resource lifetime.** In `NrState`: add `residualMotion`, `residualHistory[2]`,
   `residualDeltaHi`, `residualHistoryIndex`; remove `residualStore`, `residualStoreHi`.
   Add all to `ReleaseSurfacesIfFormatChanged`, the resolution/placement park block, and
   `Shutdown`. Allocate in `Dispatch` under `residualAcrossRr` (render-res: `residualEdited`,
   `residualMotion`, `residualHistory[2]`; output-res: `residualDeltaHi`, `residualComposed`),
   keeping v1's alloc-failure downgrade. — verify: builds; toggling the mode / changing
   resolution never binds a freed resource.
   **DONE 2026-09-10.** `NrState`: `residualEdited` kept; `residualStore`/`residualStoreHi`
   removed; `residualMotion`, `residualHistory[2]`, `residualDeltaHi`, `residualComposed`,
   `residualHistoryIndex`, `residualHistoryPrimed` added; `residualExposurePreMul` dropped.
   All present in the three park lists + `Shutdown`; the format/placement resets also zero
   `residualHistoryIndex` + `residualHistoryPrimed`. Allocated in `Dispatch`: the motion +
   history + deltaHi textures are `R16G16B16A16_FLOAT` (signed deltas need FP16); `residualEdited`
   / `residualComposed` follow `desc.Format`. Alloc-failure downgrade updated for the new set.

9. [x] **Logging.** — verify: forced run shows the line.
   **DONE 2026-09-10.** Engage line now `DLSS-NR: residual-across-RR active -- MV-reprojected
   accumulator, blend {:.2f}`. The reset-clears-history path is silent (it is per-frame common
   on some games; the accumulator self-heals in a frame or two and a log line would spam);
   `residualHistoryPrimed=false` is the observable effect.

10. [x] **Overlay.** — verify: slider drives the value; disabled tracks the checkbox.
    **DONE 2026-09-10.** `DlssNr_Menu.cpp`: "Detail accumulation rate" `SliderFloat` 0.01–1.00
    `%.2f` under the checkbox, `BeginDisabled(deferredActive || !beforeSr || !residualAcrossRr)`,
    clamped `[0.01,1.0]` on set, HelpMarker. The checkbox HelpMarker reworded for the v2 accumulator.

11. [x] **Build.** — verify: 0 errors, warning delta 0 vs baseline.
    **DONE 2026-09-10.** Release x64 warm 26w/0e; Debug x64 **full `-t:Rebuild` 63w/0e** (exact
    baseline). 0 warnings from any `dlssnr` / `dlssnr_residual` TU. `DlssNr_Shader.{cso,h}` and
    `DlssNr_Shader_Vk.{spv,h}` unchanged (`git status` clean of them).

12. [x] **Docs.** — verify: text matches shipped key names + defaults.
    **DONE 2026-09-10.** `OptiScaler/dlssnr/design/pre-sr-multipass.md`: new "Across-RR residual"
    section (the `Δ ≈ enhancement − n_t` derivation, the accumulator, fade-to-zero, the separate
    blob/PSO rationale, the three known limits) + the stale "forced to remain post-upscale" intro
    line corrected. `docs/NR-COMPATIBILITY.md` is Onimusha/skin-scoped — not the right home, left
    alone. `docs/NR-REQUIREMENTS-MATRIX.md` is not on this branch (its own PR is unmerged) —
    matrix §2 note deferred to that merge.

13. [x] **Cold self-review** (dev-loop Path B — no independent reviewer available in-session).
    **DONE 2026-09-10.** Walked the 8-file diff + `dlssnr_residual.hlsl` against every
    acceptance criterion. Two defects found and fixed:
    - **Criterion 5 (disocclusion → zero edit):** the shader took the current delta *whole*
      at invalid reprojection (`a = valid ? blend : 1.0`), which re-adds the noise term at
      silhouettes — the opposite of the chosen "fade to no edit" policy. Fixed:
      `a = clamp(gResidualBlend)` unconditionally; `history = 0` at invalid pixels means they
      fade in from no edit at the normal rate. Cold start (first frame / post-cut) is still
      handled by the host passing blend = 1 for that one frame. Blobs regenerated.
    - **Criterion 2 (`TransferStrength=0` byte-identical):** `max(base + d*s, 0)` is not
      bit-exact with `base` when `base` has negative components, and the apply ran regardless.
      Fixed: `ApplyResidualAcrossRr` early-returns (no dispatch, no copy) when the clamped
      strength is `<= 0` — Output is provably untouched.
    Also verified: `ResidualAcrossRR=false` leaves every path untouched (predicate gates the
    resolve-block ternaries, the alloc, and both new dispatches; the 2nd PSO is built but
    unused; `DlssNrConstants` grew inside its 256-byte pad so `dlssnr.hlsl` is unaffected;
    modes 5/6 remain solely `DlssNr_DeferredSr.inl`'s); camera cut clears
    `residualHistoryPrimed` + `residualStoreValid` so the cut frame applies nothing and the
    next frame rebuilds with blend 1 (no reprojected smear); `ResidualBlend=1` makes the
    accumulator a pass-through = v1's per-frame delta = the known flicker (usable self-check);
    all four `g_nr.residual*` name sets updated across the three park lists + `Shutdown`;
    `DispatchResidualPass` mirrors `DispatchPass`'s descriptor-table shape exactly.
    **Not verifiable in-session:** every runtime/visual criterion (no DLSS-D + RR title, no
    debug layer) — disclosed, deferred to the user's in-game test.

14. [x] **Review Pass** (`core.dev-loop.sk`). — branch taken: **self-review fallback (Path B)**.
    No independent reviewer available in-session and the standing "don't spawn agents unless
    asked" constraint applies, so the Review Pass collapses to the labelled cold self-review in
    step 13 (two defects found + fixed). A true second opinion is available on request via
    `/code-review` on this branch.

## Outcome (Verification-Before-Completion)

**Verified in-session**
- Release x64 (warm) 26w/0e and Debug x64 (`-t:Rebuild`) 63w/0e — both exact baseline; 0
  warnings from any `dlssnr`/`dlssnr_residual` TU.
- `DlssNr_Shader.{cso,h}` / `DlssNr_Shader_Vk.{spv,h}` **untouched** — the shared NR shader is
  never regenerated; the two new modes live in `dlssnr_residual.hlsl` + a 2nd PSO.
- `DlssNrConstants` size unchanged (new `float ResidualBlend` lands in the `alignas(256)` pad).
- `ResidualAcrossRR=false` path unchanged by code reading (predicate gates every new branch;
  modes 5/6 stay solely `DlssNr_DeferredSr.inl`'s).
- Two self-review defects (disocclusion policy; strength-0 byte-identity) found and fixed;
  blobs regenerated; rebuilt clean.
- Diff: 8 tracked files +269/-73 vs the v1 checkpoint `fdeb06bd`, + 5 new
  `precompile/dlssnr_residual*` files. No `.vcxproj` change (consistent with the existing
  dlssnr layout).

**NOT verified — draft**
- Every runtime/visual criterion: the accumulator converging on a static scene, no flicker at
  the user's chosen blend, `TransferStrength=0` byte-identity on real GPU output, camera-cut
  behaviour, disocclusion fade-in, `ResidualBlend=1` reproducing v1's flicker, and no
  resource-state / debug-layer errors. No DLSS-D + RR title and no D3D12 debug layer in this
  environment. Keep as a DRAFT / experimental toggle until the user measures it in Crimson
  Desert (or another RR title): `[DlssNr] Enabled=true, RunBeforeSR=true, ResidualAcrossRR=true`,
  RR on; sweep `ResidualAcrossRRBlend` 0.03 → 0.15; check `TransferStrength=0` is a no-op and
  `ResidualAcrossRRBlend=1` flickers like v1.
- Native Vulkan host path (SPIR-V blob is built; wiring is a later plan).

## Status: DONE (branch `feat/dlssnr-presr-residual-across-rr`, not committed past `fdeb06bd` — v2 working tree pending the user's go-ahead to commit).

## Progress log
- 2026-09-10: plan drafted. Recon established: mode 9 has the reprojection pattern to copy
  (`prevUV = uv + motion`, `SampleLevel(gLinear, ...)`, validity via `.a`/finite/bounds) but
  composes motion vectors, not colour; mode 8 produces the normalized MV field and is reused
  as-is; SRV slots t0–t3 are all free, so the delta is computed inside the accumulate pass;
  `DlssNrConstants` is `alignas(256)` so one added float is free; shader toolchain
  (`dxc.exe`, `create_header.py`, Python 3.13.14) verified present. Awaiting approval.
