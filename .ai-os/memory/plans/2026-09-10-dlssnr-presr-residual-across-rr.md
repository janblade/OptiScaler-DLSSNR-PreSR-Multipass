# Plan: pre-SR NR residual carried across Ray Reconstruction

- Branch: feat/dlssnr-presr-residual-across-rr (off `main` `e340f521`)
- Created: 2026-09-10
- Status: in-progress
- Task file: .ai-os/memory/tasks/dlss-neural-rendering.md

## Context

New opt-in `[DlssNr] ResidualAcrossRR` (bool, default `false`). Takes effect **only** when
`RunBeforeSR=true` **and** Ray Reconstruction is auto-detected (`frame.RayReconstruction`)
**and** the pre-SR Color subrect is compatible (`preSrCompatible`). Otherwise inert.

When active, the NR model runs at the pre-SR seam as today, producing the edited frame into
a scratch — but it is **not** copied back into `DLSSD.Color`, so RR+SR consume the untouched
input. Instead the pass captures the edit as a **signed, reversibly-compressed residual**
into a persistent render-res texture `g_nr.residualStore`, reusing the existing
`DlssNrMode_EncodeResidual` (mode 5) math: `store = 0.5 + 0.5·d/(1+|d|)` where
`d = (edited − original) / ExposurePreMul`. The post-SR seam upscales that residual to
display resolution and adds it onto RR's `Output` via the existing `DlssNrMode_ApplyResidual`
(mode 6): `Output = base + signedEdit/(1−|signedEdit|)·ExposurePreMul`. `ExposurePreMul` is
resolved once per evaluate and handed to both seams (they are two calls in one
`EvaluateInternal` pair). At `TransferStrength=0` the resolve yields `edited == original`, so
`d = 0`, `store = 0.5`, `edit = 0` and `Output` is byte-identical to RR-only.

**v1 = additive reuse (this plan).** Modes 5/6 already exist and are compiled into the
shipping `DlssNr_Shader*` blobs (used today only by the DeferredDLSS/ResidualFG path in
`DlssNr_DeferredSr.inl`), so v1 needs **no HLSL change / no shader regen**. It is additive in
linear light: correctness rides on the pre-SR original and the post-RR output being in the
same linear scale and on one shared `ExposurePreMul` — acceptable for a prototype whose bar
is "correct wiring, user judges visuals". A **follow-up plan** swaps in a scale-invariant
multiplicative ratio (new HLSL modes + regenerate the 4 precompiled blobs) if the A/B shows
the feature is worth keeping.

Both backends in v1: DX12 (`DlssNr_Dx12.cpp`) and native Vulkan (`DlssNrFeature_Vk.cpp` /
`DlssNr_Vk.cpp`). The mode-5/6 HLSL already has a `VK_MODE` guard, so it is available to both.

**Rationale / expectations.** This exposes the pre-SR (noisy-input) NR character across RR
for A/B against post-RR NR. It is expected to converge toward — not clearly beat — the
`RunBeforeSR=false` result: the residual is upscaled (softening the high-frequency detail NR
adds), v1 does no temporal smoothing (per-frame residual over a temporally-stable RR output
may shimmer), and additive recombination is sensitive to any pre-SR↔post-RR scale drift.
All accepted for v1. There is **no visual-quality gate for merge** — it lands
experimental/draft like the other NR placements; the user judges it in-game.

### Acceptance criteria
- [ ] `ResidualAcrossRR=false` (default): every existing path is byte-for-byte unchanged —
      pre-SR non-RR multipass, pre-SR + RR (current washed-out behaviour), plain post-SR,
      DeferredDLSS/ResidualFG (which own modes 5/6 today).
- [ ] `ResidualAcrossRR=true` + `RunBeforeSR=true` + RR active + `preSrCompatible`: the NR
      model runs at the pre-SR seam, `DLSSD.Color` is **not** modified before RR (readback
      vs model-off is identical), the residual is captured to `g_nr.residualStore`, and RR's
      `Output` is modified by the upscaled residual at the post-SR seam.
- [ ] `TransferStrength=0` with the mode on → `Output` byte-identical to RR-only
      (`edited == original` ⟹ residual encodes 0.5 ⟹ mode-6 `edit == 0`).
- [ ] Mode is inert unless all conditions hold: `ResidualAcrossRR=true` with
      `RunBeforeSR=false`, or with RR absent, changes nothing on any path.
- [ ] `preSrCompatible == false` while the mode is requested → falls back to plain post-RR
      NR, with a one-time log line.
- [ ] Release x64 + Debug x64 build, 0 errors, dlssnr-TU warning count == baseline
      (Release 26 whole-solution), **no change to any `precompile/DlssNr_Shader*` blob**.
      Overlay toggle + tooltip + INI read/write present. DX12 and native Vulkan paths both
      wired and exercised in a log.

### Out of scope
- The scale-invariant multiplicative-ratio form — deferred to a follow-up plan, contingent
  on the A/B result. v1 is additive reuse of modes 5/6.
- Temporal smoothing / reprojection of the residual (shimmer mitigation). `residualStore` is
  persistent so an EMA is a cheap follow-up; v1 ships without it.
- Motion-vector / guide-aware residual upscale — a plain filtered upscale in v1.
- Any change to the non-RR pre-SR multipass, DeferredDLSS, ResidualFG behaviour, the
  white-point sources, or the HDR colour-space decision (that is
  `fix/dlssnr-presr-hdr-authority`; this branch is off `main`, independent of it).
- Any edit to `precompile/dlssnr.hlsl` or regeneration of the precompiled shader blobs.
- Making the effect look good — explicitly the user's call in-game.

## Steps

1. [x] **Confirm the two-seam contract.** Verify `EvaluateBeforeSeam` and `EvaluateAfterSeam`
   (DX12, `DlssNr_Dx12.cpp` ~3253/3261) and `EvaluateAtSeamVk(..., before=true)` /
   `(..., false)` (Vk, `DlssNrFeature_Vk.cpp` :1340/:1350) are **both** invoked per game
   evaluate, and that `if (configuredBefore != beforeUpscale) return;` (`DlssNr_Dx12.cpp`
   ~2986) plus the Vk equivalent are the only things making one seam a no-op today. Identify
   where a persistent per-feature render-res texture attaches (the pre-SR staging / scratch
   set with deferred release). — verify: call sites + gate read; findings written to the
   task file before any edit.
   **DONE 2026-09-10.** Entry points are `EvaluateBeforeUpscale` / `EvaluateAfterUpscale`
   (DX12, defs `DlssNr_Dx12.cpp:3216/3209` → `EvaluateInternal(..., true/false, ...)`) and
   `EvaluateBeforeUpscaleVk` / `EvaluateAfterUpscaleVk` (Vk, `DlssNrFeature_Vk.cpp:1332/1344`
   → `EvaluateAtSeamVk(..., true/false, ...)`). **Both are called unconditionally** per
   evaluate by every caller: `NVNGX_DLSS_Dx12.cpp:1161/1174` (native passthrough) and
   `:1198/1206` (Opti path), `IFeature_Dx11wDx12.cpp:460/480`, `IFeature_VkwDx12.cpp:2159/2175`,
   `NVNGX_DLSS_Vk.cpp:1081/1116`. "After" is skipped only on a failed evaluate or
   `feature == FrameGeneration`. The single-seam behaviour is entirely
   `if (configuredBefore != beforeUpscale) return;` (`DlssNr_Dx12.cpp:2986`) — a localized
   gate change enables both seams. Persistent scratch lives in `struct NrState` (`g_nr`,
   `DlssNr_Dx12.cpp:180`); retirement via `ParkNrFeature` + the resource-pointer list at
   `~788` (`&g_nr.output, &g_nr.passScratch, &g_nr.colorCopy, &g_nr.hdrCopy, &g_nr.colorSmall,
   &g_nr.outputNative, &g_nr.activeColor`). Adding `g_nr.residualStore` there fits.
   **Modes 5/6 (`EncodeResidual`/`ApplyResidual`) already exist in `precompile/dlssnr.hlsl`
   (`CSMain` `gMode==5`/`gMode==6`, additive around 0.5 via `ExposurePreMul`), used today only
   by `DlssNr_DeferredSr.inl`. Steps 2–14 rewritten to reuse them — see Progress log 2026-09-10.**

2. [x] **Residual-mode reuse recon.** — verify: findings appended; no code yet.
   **DONE 2026-09-10.**
   - `DispatchPass(cmdList, const DlssNrConstants&, InSource, InModel, InOriginal, InMotion,
     InPrevEdit, OutTarget, OutKeep)` (`DlssNr_Dx12.h:81`). Callable standalone from
     `EvaluateInternal` — the resolve (`:2742`) already does. Mode 5 HLSL uses
     `gModel - gSource` → `InModel` = edited, `InSource` = original, `OutTarget` = store;
     reads `ExposurePreMul`, `gWidth/gHeight`. Mode 6 HLSL: `base = gSource`,
     `encoded = gModel` (unfiltered `.Load` — residual **must** be pre-upscaled to display
     res), `OutTarget` = out; same constants. `.inl:493` and `:655` confirm the arg mapping.
   - **`ExposurePreMul`** is per-evaluate (`DlssNr_Dx12.cpp:2228-2235`,
     `g_nr.gamePreExposure * trim`). Pre-SR and post-SR are separate `EvaluateInternal`
     calls, so stash the capture value in `g_nr.residualExposurePreMul` and reuse at apply.
   - **Upscale** = `OS_Dx12` (`output_scaling/OS_Dx12.h`), ctor `(name, device, isUp, Scaler)`,
     `Dispatch(cmdList, src, dst)` — already used for `g_nr.superUp`/`superDown`. Add
     `g_nr.residualUp` (isUp=true). Mode-6 point-sample is not viable (out-of-bounds `.Load`
     → 0 → garbage edit).
   - **Keeping `DLSSD.Color` pristine:** crop path — `target` is `g_nr.activeColor` (scratch),
     `FinishColor(false)` already skips the copy to `gameColor`; capture from `g_nr.colorCopy`
     (original) + `g_nr.activeColor` (edited). Non-crop path — resolve writes into `target`
     (== Color) in place, so in `residualAcrossRr` mode force `resolveTarget = g_nr.hdrCopy`
     (the existing `!targetSupportsUav` scratch) and skip the copy-back; capture from
     `g_nr.colorCopy` + `g_nr.hdrCopy`. Detail for step 5.

3. [x] **Config key.** Add `CustomOptional<bool> DlssNrResidualAcrossRr { false }` to
   `Config.h` next to `DlssNrRunBeforeSr` (:262), comment mirroring the DeferredDlss
   "opt-in; only with RR" note. Wire `Config.cpp` read (~:323) and write (~:1245). Add
   `ResidualAcrossRR=false` to `OptiScaler.ini` `[DlssNr]` with a comment. — verify: build;
   value round-trips through ini save→load.
   **DONE 2026-09-10.** `Config.h:265` (`DlssNrResidualAcrossRr { false }` + 4-line comment
   after `DlssNrDeferredDlss`); `Config.cpp:324` read (`readBool("DlssNr","ResidualAcrossRR")`),
   `:1249` write (`ini.SetValue("DlssNr","ResidualAcrossRR", ...)`); `OptiScaler.ini:1601`
   (`ResidualAcrossRR=auto` + comment after `DeferredDLSS`). Compile verified at step 12.

4. [x] **Shared activation predicate + two-seam gate (DX12).** — verify: predicate false ⟹
   old control flow exactly (build unchanged); both seams reachable in the mode.
   **DONE 2026-09-10.** `EvaluateInternal` (`DlssNr_Dx12.cpp`): after `configuredBefore`,
   `residualAcrossRr = cfg.DlssNrResidualAcrossRr && configuredBefore && rayReconstruction`
   (so `preSrCompatible` false ⟹ falls to the normal gate ⟹ plain post-SR NR fallback). New
   early block `if (residualAcrossRr && !beforeUpscale) { ApplyResidualAcrossRr(...); one-time
   LOG_INFO; return; }` sits before the unchanged `if (configuredBefore != beforeUpscale)
   return;`. `frame.ResidualAcrossRr` plumbed via `DlssNrFrameInfo` (`DlssNr_Common.h`);
   `DlssNr_Dx12::Dispatch` reads it into a mutable local `residualAcrossRr`
   (`&& frame.BeforeUpscale`), downgraded on scratch-alloc failure.

5. [x] **Pre-SR capture (DX12).** — verify: byte-identical `DLSSD.Color` (redirected resolve,
   no copy-back); `TransferStrength=0` ⟹ carrier encodes 0.5.
   **DONE 2026-09-10.** In `Dispatch`'s resolve block: when `residualAcrossRr`,
   `resolveOriginal = g_nr.hdrCopy` (untouched pristine copy from the encode's OutKeep),
   `resolveTarget = g_nr.residualEdited` (owned scratch — Color never written). After the
   resolve, `DispatchPass(DlssNrMode_EncodeResidual, InSource=hdrCopy, InModel=residualEdited)
   → g_nr.residualStore` with `ExposurePreMul = exposurePreMul`; then stamp
   `g_nr.residualExposurePreMul`, `residualStoreFrame = State::Instance().frameCount`,
   `residualStoreValid = true`. Final `FinishColor(... && !residualAcrossRr)` skips the
   copy-back. Barriers around `residualEdited` / `residualStore` SRV↔UAV.

6. [x] **Post-SR apply (DX12).** — verify: `TransferStrength=0` (edit=0) ⟹ `Output`
   byte-identical; stale/missing carrier ⟹ no-op.
   **DONE 2026-09-10.** `ApplyResidualAcrossRr(cmdList, params)` (free fn in `namespace
   DlssNr`, before `EvaluateInternal`): guards on `residualStoreValid` + one-frame freshness
   (`frameCount - residualStoreFrame <= 1`); `g_nr.residualUp->Dispatch(residualStore →
   residualStoreHi)`; `g_compose->DispatchPass(DlssNrMode_ApplyResidual, InSource=output,
   InModel=residualStoreHi) → g_nr.residualComposed`; `CopyResource(output, residualComposed)`.
   `output` arrival state mirrors the existing post-SR path
   (`Config::OutputResourceBarrier` ?: `UNORDERED_ACCESS`). `residualStoreValid` cleared
   (one-shot).

7. [x] **Persistent-resource lifetime (DX12).** — verify: builds; retire lists updated;
   alloc-failure path downgrades to plain pre-SR.
   **DONE 2026-09-10.** `NrState`: `residualEdited`, `residualStore`, `residualStoreHi`,
   `residualComposed` (+ `OS_Dx12* residualUp`, `residualStoreFrame`, `residualStoreValid`,
   `residualExposurePreMul`). Added to `ReleaseSurfacesIfFormatChanged` list, the
   resolution/placement park block, and `Shutdown`. Allocated in `Dispatch` (gated on
   `residualAcrossRr`, sizes: render `width×height` ×2, output `frame.OutputWidth×Height` ×2,
   `desc.Format`); any failure logs once and downgrades the local `residualAcrossRr` to false
   for the frame. `residualStoreValid` reset wherever the set is parked.
   **Build: Release x64 26w/0e, Debug x64 63w/0e (both == baseline); no `precompile/` blob
   changed; diff +234/-7 across `Config.{h,cpp}`, `OptiScaler.ini`, `DlssNr_Common.h`,
   `DlssNr_Dx12.cpp`.** Runtime resource-state / visual verification not possible in this
   environment — disclosed, deferred to the user's in-game test.

   **DX12-testable checkpoint reached 2026-09-10** (user asked to test DX12 before Vk). Steps
   4–7 + 10 done and built; steps 8 (Vk), 11 (docs), 13–14 (reviews) pending. Test artifact:
   `x64/Release/a/OptiScaler.dll`.

8. [ ] **Vulkan parity.** (DEFERRED — after the DX12 in-game test.) Mirror steps 4/5/6/7 in `EvaluateAtSeamVk`
   (`DlssNrFeature_Vk.cpp` / `DlssNr_Vk.cpp`): same predicate, mode-5 capture at `before`,
   upscale + mode-6 apply over the Vk `Output` image at `after`, same lifetime rules. Modes
   5/6 already carry a `VK_MODE` guard in the HLSL, so `DlssNr_Shader_Vk.spv` needs no
   change. — verify: Vk build links; a Vk+RR title (or the Vk-over-DX12 bridge) shows both
   seams firing for one frame in the log.

9. [ ] **Logging.** One-time line when the mode engages
   (`DLSS-NR: residual-across-RR active — model before SR, edit re-applied after RR+SR
   (additive v1)`); confirm the existing "running before/after RR+SR" lines both fire for one
   frame in this mode; one-time fallback line when `preSrCompatible` is false. — verify: a
   forced run shows the engage line + the before/after pair.

10. [x] **Overlay.** Add the `ResidualAcrossRR` checkbox to the DLSS-NR section of the
   overlay (`OptiScaler/dlssnr/DlssNr_Menu.cpp`), grouped under `RunBeforeSR`, greyed when
   `RunBeforeSR` is off. Tooltip: applies only with the game's Ray Reconstruction on;
   experimental, additive re-apply, trades some sharpness for surviving RR. — verify:
   checkbox toggles the config value; disabled state tracks `RunBeforeSR`.
   **DONE 2026-09-10.** `DlssNr_Menu.cpp` after the "Apply before Super Resolution" checkbox:
   "Carry the pre-SR edit across RR (experimental)", `BeginDisabled(deferredActive || !beforeSr)`,
   + HelpMarker. Release x64 rebuilt: 26w/0e.

11. [ ] **Docs.** Short "Across-RR residual (experimental, additive v1)" subsection in
   `OptiScaler/dlssnr/design/pre-sr-multipass.md` (what it does; softening + no temporal
   smoothing + additive-scale-drift limitations; multiplicative follow-up noted) and a
   row/note in `docs/NR-COMPATIBILITY.md`. Note the key in `docs/NR-REQUIREMENTS-MATRIX.md`
   §2. — verify: doc text matches the shipped key name + default.

12. [ ] **Build.** Release x64 then Debug x64 with the 64-bit MSBuild; confirm no
   `precompile/DlssNr_Shader*` blob changed (`git status`). — verify: "Build succeeded", 0
   errors, dlssnr-TU warning delta 0 vs baseline, no shader-blob diff.

13. [ ] **Cold self-review** of the full diff against the acceptance criteria; labelled
   note (dev-loop Path B). — verify: note written.

14. [ ] **Independent Review Pass** (`core.dev-loop.sk`) on the accumulated diff with this
   `## Context` as the spec; one line naming the branch taken (independent reviewer /
   self-review fallback); findings surfaced, non-blocking. — verify: report relayed.

## Progress log
- 2026-09-10: plan drafted; branch `feat/dlssnr-presr-residual-across-rr` created off `main` `e340f521`. Awaiting approval.
- 2026-09-10: PLAN_EXECUTE started. Step 1 done (see step note). **Step 1 surfaced two things the
  plan under-specified — stopped per PLAN_EXECUTE step 5:**
  1. **The compose shader is precompiled via a toolchain step.** `dlssnr.hlsl` (one CS,
     `CSMain`, mode-switched by `DlssNrConstants.Mode`) is built by
     `shaders/shader_tools/build_precompiled_shader*.bat` — vendored `dxc.exe` (cs_6_0) +
     `fxc.exe` (cs_5_0) + `create_header.py` — into checked-in
     `precompile/DlssNr_Shader.{h,cso}` and `DlssNr_Shader_Vk.{h,spv}`. Tools are in-repo so
     it's doable here, but any HLSL change means regenerating 4 binary artifacts that land in
     the commit. Plan steps 4–5–8 assumed shader edits were cheaper than they are.
  2. **A residual capture/apply mechanism already exists.** `DlssNrMode_EncodeResidual=5`
     ("NR-composed minus original; signed difference encoded around 0.5"),
     `DlssNrMode_ApplyResidual=6` ("decode private DLSS result and add to clean SR output"),
     `DlssNrMode_ApplyInterpolatedResidual=10` — built for the ResidualFG prototype. Same
     *shape* as this feature (capture NR's edit, re-apply downstream) but **additive**, in the
     encoded proxy space, currently wired only to ResidualFG. Separately, the main
     `DlssNrMode_Resolve` already supports a multiplicative "matched residual" transfer
     (`Transfer` param: matched-residual ratio vs classic additive) — but it composes colour,
     it does not emit the ratio to a UAV.
  **Decision needed (see conversation):** additive-reuse (near-zero shader work, but shares
  the white-point-alignment sensitivity the multiplicative choice was meant to avoid) vs.
  add multiplicative ratio modes (HLSL edit + 4 precompiled-blob regen, scale-invariant).
- 2026-09-10: **user chose "prototype additive first, upgrade later".** Context / acceptance
  criteria / out-of-scope / Steps 2–14 rewritten to reuse `DlssNrMode_EncodeResidual` (5) and
  `DlssNrMode_ApplyResidual` (6) with **no HLSL change / no shader regen**. Mode math read
  from `precompile/dlssnr.hlsl:504-528`: mode 5 stores `0.5 + 0.5·d/(1+|d|)`,
  `d=(edited−original)/ExposurePreMul`; mode 6 does `base + signedEdit/(1−|signedEdit|)·ExposurePreMul`.
  Multiplicative ratio is now a contingent follow-up plan. Resuming at step 2.
- 2026-09-10: steps 2–7 + 10 done (DX12 + config + overlay). +239/-7 across `Config.{h,cpp}`,
  `OptiScaler.ini`, `DlssNr_Common.h`, `DlssNr_Dx12.cpp`, `DlssNr_Menu.cpp`; no shader-blob
  change. Release x64 26w/0e, Debug x64 63w/0e (both baseline). User asked to test DX12 before
  Vk — **paused for in-game test**; steps 8 (Vk), 11 (docs), 13–14 (reviews) pending.
  Artifact: `x64/Release/a/OptiScaler.dll`.
- 2026-09-10: user tested — the additive residual is **noisy / grain-flickering**.
  `TransferStrength=0` is clean (wiring correct), `>0` flickers ⟹ inherent additive-v1
  content problem (per-frame noise-correlated residual added onto a temporally stable
  frame). Next lever proposed: zero-code knobs test (`LocalStructure=0` etc. — tone/colour
  only), then blur+hold if that's stable. Awaiting the knobs-test result.
- 2026-09-10: **detour (unrelated):** ported `ae2a10af` "Keep the overlay usable over the
  Assetto Corsa + CSP input stack" from `dlss-neural-rendering` to a fresh branch
  `fix/overlay-input-assetto-corsa-csp` off `main` (cherry-pick `55cc05ee`, clean; R x64 26w,
  D x64 63w, 0e; 0 warnings from the ported files). Pushed; **PR #6 → main**. Residual WIP
  stashed/restored around the switch.
