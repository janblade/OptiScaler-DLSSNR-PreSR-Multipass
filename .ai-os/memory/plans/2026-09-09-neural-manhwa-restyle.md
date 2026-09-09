# Epic: Neural manhwa restyling pass ("reverse-NR")

- Branch: none yet (per-story branches off main; main is protected)
- Created: 2026-09-09
- Status: approved      # DERIVED roll-up once execution starts — see PLAN_EXECUTE
- Type: epic
- Task file: memory/tasks/main.md

## Context

A real-time post-process, injected through OptiScaler in the same slot as DLSS-NR
(post-upscale, pre-frame-generation), that restyles the rendered frame toward a **Solo
Leveling colored-manhwa aesthetic**: cel-banded shading, strong directional shadow shapes,
high-chroma palette, crisp dark edge linework, bloomed highlights -- while preserving the
rendered scene's geometry and motion. Same *class* of operation as DLSS-NR
(structure-preserving learned image-to-image) pointed at an illustrated target instead of a
photographic one.

**What this is NOT.** Not retraining `nvngx_dlssnr.dll` -- it is a closed ONNX->TensorRT
inference engine shipped with no weights in trainable form, no training graph, no loss, no
dataset (our binary dump recovered parameter *names* only). "Lean stylized" is not a knob
inside it. This epic builds a *separate* model, trained from scratch, run through our own
inference path. OptiScaler contributes the plumbing: frame intercept, motion vectors,
depth, the compose-pass + ping-pong-buffer infrastructure, and the config/menu patterns.

**Method (default, overridable).** No paired data exists (no "this frame -> its manhwa
version"), so this is **unpaired translation**. Primary: **CUT (Contrastive Unpaired
Translation)** -- learns the *domain* mapping (flattens shading into cel bands, adds
linework), not just brushstroke texture statistics like classic neural style transfer --
trained at 256-512px, then distilled to a compact (~2-5M param) feed-forward U-Net
generator for inference. Fallback if CUT output reads as "mud" / generic-anime-filter:
distill a low-strength SDXL-Turbo img2img + a style-LoRA to a small U-Net (heavier, needs
cloud GPU). Story 2 decides.

**Runtime (default, overridable).** ONNX export -> **ONNX Runtime + DirectML** behind a
thin `IStyleInference` interface (cross-vendor, matches OptiScaler's design). Leave room
for a TensorRT backend later; do not build it now. Frame budget: ~2-5M params FP16
~= 2-6 ms at 1440p internal; if over budget, run at a reduced working scale and upsample
the style delta (OptiScaler already has that downscale-then-upsample pattern for NR).

**Epic-level acceptance criteria (what "epic done" means beyond the sum of stories).**
- [ ] A toggleable in-game pass with a strength slider that visibly restyles the frame
      toward the target look.
- [ ] Motion coherence materially better than a per-frame-independent baseline (measured,
      not "zero flicker").
- [ ] Inference fits the NR/FG frame-time budget on a target GPU at 1440p internal.
- [ ] Debug|x64 + Release|x64 build clean, no new warnings in changed OptiScaler files.
- [ ] In-game A/B recorded on at least two games.

**Verification reality.** The training half lives in a new `tools/style-transfer/` (PyTorch,
outside `OptiScaler.sln`) and has its own verification model: offline image/video metrics +
structured human visual eval + ms/frame measurement. The C++ `has_tests:false` build flow
only covers Story 5. This is a multi-week-to-multi-month effort; most of it runs outside
the interactive session.

**Risks.**
- **Quality is the real risk.** Unpaired "game -> *named artist's* style" frequently lands
  as a generic anime/comic filter, not the specific target. Story 2 is the cheap
  (~1-2 week) go/no-go before any integration work.
- **Temporal stability.** Per-frame-independent restyling boils/flickers. Mitigated by a
  flow-warp consistency loss in training (Story 3) + an MV-reprojection blend at inference
  (Story 5), the same history reprojection NR does. Some residual flicker is expected.
- **Copyright / ethics.** Training on Solo Leveling art and using the output is fine for
  personal experimentation. If shared: describe it as a generic "manhwa / webtoon cel
  style", do not redistribute scraped panels, do not brand the model with the IP or studio
  name.
- **Compute.** Story 2 CUT training fits one 24GB consumer GPU. The diffusion-distillation
  fallback needs cloud multi-GPU.

**Out of scope.** Arbitrary / swappable styles (fixed Solo Leveling look only); a TensorRT
backend (interface leaves room, not built); the Vulkan native path (DX12 first, port only
if it proves out); frame-generation interaction beyond running before it; shipping or
distributing the model or the dataset.

## Story 1: dataset-pipeline — Content + style corpora and reproducible tooling
Status: approved
### Acceptance Criteria
- [ ] ~5-10k UI-free game-frame crops from multiple games (day/night, indoor/outdoor,
      characters + environments), captured via an OptiScaler debug frame dump.
- [ ] ~2-4k Solo Leveling color-panel crops, cleaned of speech bubbles / SFX text / panel
      gutters, at a consistent resolution.
- [ ] Deduplicated, cropped, resized; a held-out eval split that never enters training.
- [ ] A dataset card (sources, counts, licence note, cleanup steps) and the tooling to
      rebuild it from raw inputs.
### Steps
1. [ ] Debug-only frame-dump hook in the OptiScaler DX12 path (write post-SR colour to disk
   on a hotkey; gated behind a config flag, never in a release build path) — verify: dumps
   land, correct colour space, no perf impact when disabled.
2. [ ] Capture sessions across >= 4 games; log game + settings per session — verify: frame
   count + variety check against a checklist.
3. [ ] `tools/style-transfer/data/` scripts: panel collection, bubble/SFX removal (manual
   mask pass + inpaint, or simple crop-around), gutter crop — verify: spot-check 50 random
   outputs are clean.
4. [ ] Dedup (perceptual hash), crop/resize to the training resolution, train/eval split —
   verify: split is disjoint by source image, counts hit the AC targets.
5. [ ] Write the dataset card — verify: another person could rebuild the corpus from it.

## Story 2: offline-stills-model — CUT model, stills quality (GO / NO-GO)
Status: approved
depends on: Story 1
### Acceptance Criteria
- [ ] A CUT model trained at <= 512px on the Story 1 corpora, checkpointed.
- [ ] On held-out game frames, evaluated *as stills*, it reads as target-style under a
      written rubric (cel banding / linework presence / palette shift / structure
      preservation), each scored, on a sample grid.
- [ ] It beats a classic feed-forward NST baseline on the same rubric.
- [ ] A written verdict: proceed to Story 3 / switch to diffusion-distillation / stop.
### Steps
1. [ ] Stand up a CUT training rig in `tools/style-transfer/` (PyTorch); pin deps, seed,
   log to disk — verify: one short run completes and writes samples.
2. [ ] Train to convergence at 256px, then fine-tune at 512px — verify: loss curves sane,
   no mode collapse.
3. [ ] Classic Johnson-style NST baseline on the same style set — verify: trains, produces
   output.
4. [ ] Eval harness: fixed held-out set, side-by-side grid (input / NST / CUT), the rubric
   as a scored form — verify: grid + scores produced.
5. [ ] Write the verdict into this plan's Story 2 section — verify: decision is explicit
   and evidence-backed.

## Story 3: temporal-consistency — Flicker-stable in motion
Status: approved
depends on: Story 2
### Acceptance Criteria
- [ ] An optical-flow-warp temporal consistency loss added; model retrained on short clips.
- [ ] On captured gameplay video (offline pipeline), a flicker metric (warped-previous-frame
      RMSE over co-visible regions) is materially below the Story 2 model.
- [ ] A side-by-side video (Story 2 model vs Story 3 model) on the same clip.
### Steps
1. [ ] Clip dataset: short sequences from the capture sessions (or synthetic warps of
   stills) — verify: clips load, flow computable.
2. [ ] Optical flow (RAFT or the game's MVs where dumped alongside) — verify: flow sanity on
   a known clip.
3. [ ] Add the temporal loss (warp N-1 output by flow, penalise divergence from N in
   co-visible pixels); retrain from the Story 2 checkpoint — verify: temporal loss
   decreases without wrecking style score.
4. [ ] Video eval: the flicker metric + rendered comparison video — verify: metric drop is
   real, video reads as more stable.

## Story 4: realtime-inference — Standalone ONNX/DirectML harness within budget
Status: approved
depends on: Story 3
### Acceptance Criteria
- [ ] Model exported to ONNX (opset checked, FP16 path verified) and loadable.
- [ ] A standalone C++ harness runs it via ONNX Runtime + DirectML over a folder of frames.
- [ ] Measured ms/frame at 1080p / 1440p / 4K internal resolution on a named target GPU,
      within the NR/FG budget; if over, the reduced-working-scale fallback brings it in.
### Steps
1. [ ] ONNX export from the Story 3 checkpoint; verify numerically against PyTorch on a few
   frames — verify: max abs diff within tolerance.
2. [ ] `IStyleInference` interface + a DirectML implementation (model load, FP16, single
   dispatch) — verify: harness produces the same output as the ONNX check.
3. [ ] Timing harness: warm-up + N-frame average at each resolution — verify: numbers
   recorded per GPU.
4. [ ] If over budget: run at 0.5-0.75x working scale, upsample the style delta, re-measure
   — verify: within budget, quality delta acceptable on a sample grid.

## Story 5: optiscaler-integration — In-game pass, config, menu, temporal blend
Status: approved
depends on: Story 4
### Acceptance Criteria
- [ ] A new pass parallel to `DlssNr_Dx12` (own model load; no nvngx forwarder — no
      caller-gate to satisfy), running post-SR / pre-FG.
- [ ] Config toggle + strength slider (blend stylized <-> original) + working-scale option;
      menu entries with tooltips.
- [ ] MV-reprojection blend with a disocclusion mask from MV/depth (reuses the NR history
      reprojection).
- [ ] Debug|x64 + Release|x64 build clean; no new warnings in changed files.
- [ ] In-game A/B recorded on >= 2 games (strength 0 == untouched frame; sweep to 1).
### Steps
1. [ ] Feature scaffold reusing the compose-shader / ping-pong-buffer / config / menu
   patterns from `DlssNr_Dx12` — verify: builds, pass is a no-op at strength 0.
2. [ ] Wire `IStyleInference` into the pass; colour-space in/out via the compose shader —
   verify: single-frame output matches the Story 4 harness.
3. [ ] Temporal blend: reproject previous stylized output by MV, blend, mask disocclusions
   from MV/depth — verify: motion stability matches the Story 3 offline result.
4. [ ] Config (`CustomOptional`), `Config.cpp` read/write, `DlssNr_Menu`-style menu section
   with `HelpMarker` tooltips — verify: settings persist, live-adjustable.
5. [ ] Full build both x64 configs; forced `/W3` recompile of changed TUs — verify: 0
   errors, 0 new warnings.
6. [ ] In-game A/B on >= 2 games; record the outcome here and in `main.md` — verify: user
   sign-off.

## Notes / risks (running)

- Story 2 is the pivot. Budget it small and be willing to kill the epic there.
- Keep the frame-dump hook (Story 1 step 1) behind a config flag and out of any release
  path — it is a debugging aid, not a feature.
- No push / PR for any story without explicit user go-ahead.
