# Plan: Pre-SR DLSS-NR jitter-cancellation via motion vectors (prototype)

- Branch: experiment/dlssnr-presr-jitter-mv  (off main; main is protected — no direct commits)
- Created: 2026-09-07
- Status: in-progress      # draft | approved | in-progress | done | abandoned
- Task file: memory/tasks/main.md

## Context

**Problem.** In pre-SR placement, OptiScaler runs the DLSS-NR model on the game's raw
render-resolution colour, which was rendered with a per-frame sub-pixel jitter offset. The
retail `nvngx_dlssnr.dll` (v310.8.0) has **zero jitter parameters** (binary-dump confirmed,
`docs/dlssnr-binary-dump/`), so the model cannot be told the offset. Its temporal history
reprojection then can't align this frame's jittered sample grid against the previous
frame's differently-jittered grid → the per-frame jitter delta reads as sub-pixel motion it
smears → softening. Post-SR has no gap (SR resolves jitter first).

**Idea.** Leave the colour alone (SR downstream needs it jittered). Instead, build a
**scratch motion-vector texture for the NR pass only**: copy the game's MVs and add the
per-frame jitter delta `(prevJitter - curJitter)` (sign/scale TBD in-game). NR then warps
its history by MVs that already account for the jitter shift, so accumulation aligns. The
MVs the game hands to SR are never touched; colour is never touched. This is the pattern of
`FFX_FSR2_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION` / XeSS jittered-MV mode.

**Acceptance criteria.**
1. New config flag `DlssNrJitterCancel` (default **off** — experimental). When off, byte-for-byte
   the current behaviour (no scratch MV, no extra pass).
2. When on **and** `frame.BeforeUpscale` (pre-SR): the NR model + NR compose pass consume the
   jitter-cancelled scratch MV; the game's MV resource in the NGX param block (what SR reads)
   is unchanged.
3. When on but NR is running **post-SR** (`!frame.BeforeUpscale`): flag is a no-op (no scratch,
   no pass) — jitter is already resolved there.
4. A tuning knob `DlssNrJitterCancelScale` (float, default 1.0, accepts negative) multiplies the
   applied delta, so sign and magnitude can be dialled in-game without a rebuild.
5. Debug/telemetry: one throttled `LOG_INFO` line per change reporting `curJitter`,
   `prevJitter`, applied delta (texel units), and MvScale — so the in-game A/B is measurable.
6. Both x64 configs build clean; no new warnings in changed files.
7. In-game A/B (pre-SR, slow-pan / near-static scene where the jitter delta is most visible):
   with the flag on vs off, judge whether pre-SR sharpness / temporal stability improves,
   worsens, or is unchanged. Result recorded either way.

**Out of scope.** The Vulkan native NR path (`DlssNrFeature_Vk.cpp`) — DX12 prototype first;
port only if it proves out. Any colour-domain change. Touching the SR feature. The
`RunBeforeSR` gating decision (separate).

**Verification reality.** No automated tests (`has_tests:false`). Every step = clean
Debug|x64 + Release|x64 + manual in-game. Prototype outcome may be negative (the model may
not do MV-driven history reprojection in a way this helps) — step 9 decides keep vs revert.

## Current-structure notes (verified 2026-09-07)

- `EvaluateInternal` (`DlssNr_Dx12.cpp:3031+`) builds `DlssNrFrameInfo frame` from the NGX
  param block: reads `MotionVectors` (`:3138`), `MV_Scale_X/Y` (`:3189-3193`), exposure
  (`:3203-3220`), `DLSS_Feature_Create_Flags` for `DepthInverted`. It does **not** read
  `NVSDK_NGX_Parameter_Jitter_Offset_X/Y` — nothing in `OptiScaler/dlssnr/**` or
  `OptiScaler/shaders/dlssnr/**` reads jitter today.
- `DlssNrFrameInfo` struct: `DlssNr_Common.h:54` (has `MvScaleX/Y` at 62-63, `BeforeUpscale`,
  `Reset`, `DepthInverted`, `ExposureTexture`, `PreExposure`).
- The NR pass entry: `g_compose->Dispatch(cmdList, target, depth, motion, target, frame, timingQueue)`
  at `DlssNr_Dx12.cpp:3318`. `g_compose` is a `DlssNr_Dx12` (`std::unique_ptr`, built at `:3307`).
  That `motion` argument is the one the compose class feeds to **both** its compute shader
  and the model evaluate (`dlssnr_call_evaluate` is driven from inside `DlssNr_Dx12::Dispatch`).
  So substituting `motion` at this one call site covers the model and the shader.
- The forwarder passes MVs straight through — `dlssnr_call_evaluate(..., motion, ...)` →
  `setResource(capabilityParams, "DLSSNR.MVec", motion)` (`dlssnr_forwarder.cpp:805`), with
  `MVecSubrectBaseX/Y = 0` (`:828-829`). Origin-zero MV is already assumed everywhere; the
  scratch pass assumes it too.
- Scratch-resource pattern: `g_nr.activeColor` / `g_nr.colorCopy` / `g_nr.colorSmall` —
  `CreateScratch(device, format, w, h)` + `ParkNrResource(...)` on teardown/resize
  (`DlssNr_Dx12.cpp:1935-1937`, `:3572-3605`). `g_nr` is the module state struct (`:240+`).
- Compute-pass options:
  - **(a)** add a 4th mode to the existing `dlssnr.hlsl` / `DlssNr_Dx12` compose shader
    (`shaders/dlssnr/precompile/dlssnr.hlsl` → `DlssNr_Shader.cso`/`.h`, included at
    `DlssNr_Dx12.cpp:26`). It already binds `motion` as an SRV (5 SRV / 2 UAV, `DlssNr_Dx12.h:56-57`).
    Reuses the root sig, 48-slot descriptor/constant ring, `Shader_Dx12` base.
  - **(b)** a standalone tiny compute helper, like `OS_Dx12 superUp/superDown` instances.
  Step 4 picks; (a) is less new scaffolding and matches the file's stated "belongs here
  alongside RCAS" philosophy, but touches the shared constant struct + shader.
- Config pattern: `OptiScaler/Config.h` / `Config.cpp`, `CustomOptional<T>`; menu wiring in
  `OptiScaler/dlssnr/DlssNr_Menu.cpp`. Existing NR knobs: `DlssNrWorkingScale` (Config.h:386),
  `DlssNrPreset` (:269), `DlssNrWhitePointSource` (:453).

## Steps

1. [x] **Capture jitter into `DlssNrFrameInfo`.** DONE. `DlssNr_Common.h`: `float JitterX/JitterY = 0`
   on `DlssNrFrameInfo`. `DlssNr_Dx12.cpp` `EvaluateInternal` reads
   `NVSDK_NGX_Parameter_Jitter_Offset_X/Y` right after the `MV_Scale` reads, default 0 on failure.
   Nothing consumes it unless the flag is on.

2. [x] **Config flag + tuning knob.** DONE. `Config.h`: `CustomOptional<bool> DlssNrJitterCancel { false }`,
   `CustomOptional<float> DlssNrJitterCancelScale { 1.0f }`. `Config.cpp`: `readBool`/`readFloat` on
   `DlssNr`/`JitterCancel` + `JitterCancelScale`, and matching `ini.SetValue` in the writer.

3. [x] **Previous-frame jitter state + delta.** DONE, but the delta lives in `DlssNr_Dx12::Dispatch`
   (not `EvaluateInternal`) — `frame` reaches it by ref and `g_nr`/`guide*` are already in scope.
   `NrState`: `prevJitterX/Y`, `havePrevJitter`, `jitterMv`, `jitterMvState`. Delta
   `= g_nr.prevJitterX - frame.JitterX` (per axis), zeroed on `!havePrevJitter || frame.Reset ||
   g_nr.reset` (the last catches our own resize/recreate rebuilds). `prevJitter`/`havePrevJitter`
   stored each pre-SR frame the flag is on; cleared in all three teardown/park sites
   (`ReleaseSurfacesIfFormatChanged`, the resize park block, Shutdown) and by the flag-off `else`
   branch. Throttled `LOG_INFO` every 60 frames prints cur/prev jitter, MV texel delta, MvScale.

4. [x] **Decide + scaffold the MV compute pass.** DECISION: **option (a)** — a new
   `DlssNrMode_JitterCancelMv = 5` mode in the existing `dlssnr.hlsl` compose shader, driven through
   the existing `DlssNr_Dx12::DispatchPass` (game MV -> SRV t3 via `InMotion`, scratch MV -> UAV u0
   via `OutTarget`). Two trailing `DlssNrConstants` fields `JitterMvDeltaX/Y` carry the delta already
   in MV texel units: `applied_texels = delta_px * DlssNrJitterCancelScale / MvScale` (MvScale 0/unset
   -> 1). Shader block: `gTarget[id.xy] = gMotion.Load(int3(id.xy,0)) + float4(gJitterMvDeltaX, gJitterMvDeltaY, 0, 0);`
   over the full MV allocation (not just the guide rect) so the scratch has no uninitialised margin.
   Rationale for (a) over (b): far less C++ (no bespoke root sig / PSO / descriptor ring), reuses the
   48-slot ring and `Shader_Dx12` base, and `DispatchPass` already binds `motionIn` as an SRV in the
   resolve path so that path is proven. Cost: regenerate 4 committed artifacts
   (`DlssNr_Shader.cso/.h`, `DlssNr_Shader_Vk.spv/.h`) with the bundled `dxc.exe`; `create_header.py`
   can't run (no Python on this host) so the `.cso`->`.h` byte-array step is hand-rolled to the exact
   same format. Trailing-only struct/cbuffer growth keeps every existing mode byte-identical and
   `sizeof(DlssNrConstants)` unchanged (`alignas(256)`, 29->31 scalars still <= 256).
   — verify: build; pass compiles into the precompile artifact; dispatch records with no
   validation-layer error (run with the D3D12 debug layer once).

   NOTE — substitution point moved from the plan's original "just before `g_compose->Dispatch`
   (`:3318`)" to **inside `DlssNr_Dx12::Dispatch`, right after `motionIn = ReadableGuide(...)`
   (`:2531`)**: the compose shader never samples `gMotion` (verified — only the meter mode reads it,
   for an exposure fallback), so the real consumers are `g_nr.evaluate` and `DlssNr::Proxy::Run`, both
   downstream of `motionIn`; and `guideWidth/Height`, `device`, `g_nr`, `frame` are all already in
   scope there. `frame.JitterX/Y` reach `Dispatch` via the `DlssNrFrameInfo&` param. Steps 5/6 fold
   into this same site.

5. [x] **Scratch MV texture.** DONE. `g_nr.jitterMv` created lazily in `Dispatch` when
   `jitterCancel` (= `DlssNrJitterCancel && frame.BeforeUpscale && !frame.AfterRayReconstruction`):
   `CreateScratch(device, motionDesc.Format, motionDesc.Width, motionDesc.Height)` — full MV
   allocation size/format, `CreateScratch` always adds `ALLOW_UNORDERED_ACCESS`. Reallocated on a
   format/size mismatch. Parked + nulled in all three teardown sites and by the flag-off `else`
   branch. Never created post-SR / after RR (AC 3). Alloc failure → `modelMotion` stays `motionIn`
   and a note rides the throttled log.

6. [x] **Wire the substitution.** DONE, inside `DlssNr_Dx12::Dispatch` right after
   `motionIn = ReadableGuide(...)`. `ID3D12Resource* modelMotion = motionIn;` — when the pass runs,
   `Barrier(jitterMvState -> UAV)`, `DispatchPass(mode 5, motionIn -> SRV, g_nr.jitterMv -> UAV)`,
   `Barrier(UAV -> NON_PIXEL_SHADER_RESOURCE)`, `jitterMvState = NPSR`, `modelMotion = g_nr.jitterMv`.
   `jitterMvState` is a tracked `NrState` member so the next frame's pass restores the resource
   itself — no end-of-Dispatch barrier that an early `return` (proxy path) could skip. Consumers
   swapped `motionIn -> modelMotion`: `DlssNr::Proxy::Run` and the `g_nr.evaluate` pass loop. The
   **resolve `DispatchPass` is left on `motionIn`** — verified the compose shader never samples
   `gMotion` outside the meter mode, so there is nothing there to feed. NGX param block's
   `MotionVectors` never written; SR reads the game's MV (AC 2). Flag off → `modelMotion == motionIn`,
   no pass, no scratch (AC 1).

7. [x] **Menu toggle.** DONE. `DlssNr_Menu.cpp`, nested under "Apply before Super Resolution" and
   only shown when that is on: checkbox `DlssNrJitterCancel` + (when on) `SliderFloat`
   `DlssNrJitterCancelScale` [-4, 4] "%.2fx", each with a `HelpMarker` (experimental / pre-SR / DX12,
   and how to dial the sign+magnitude in-game). Live evaluate-time branch, no feature rebuild.

8. [x] **Full build.** DONE. Debug|x64 + Release|x64 both **Build succeeded, 0 errors**. A forced
   recompile of all four changed TUs (`Config.cpp`, `DlssNr_Menu.cpp`, `DlssNr_Dx12.cpp`,
   `DlssNr_Vk.cpp`) under `/W3` produced **zero** warning lines; the config totals (Debug 1 /
   Release 26) are the pre-existing baseline (Magnifier C4244, C4250 dominance, LNK4098, Streamline
   C4267). Precompiled shader artifacts regenerated from `dlssnr.hlsl` with the bundled `dxc.exe`;
   headers hand-rolled to `create_header.py`'s exact format (no Python on host), hex-token counts
   verified equal to the `.cso`/`.spv` byte sizes. First build caught a name mismatch: the VK header
   array is `dlssnr_spv` (lowercase) while the DX one is `DlssNr_cso` — regenerated to match.

9. [ ] **In-game A/B + verdict.** Pre-SR, a slow camera pan and a near-static shot (jitter
   delta is most visible in low-motion content). Flag off vs on; sweep `DlssNrJitterCancelScale`
   through +1, -1, +2 to find the sign/magnitude that helps, if any. Check: (a) does pre-SR
   sharpness / temporal stability improve; (b) any new ghosting or smear during real motion
   (wrong sign would add motion, not cancel it); (c) NR post-SR unaffected (flag is a no-op
   there); (d) NBA 2K26 `WM_INPUT`-style regression title still fine with the flag off.
   Record the result in this plan and `main.md`. **If no measurable improvement at any
   scale → revert steps 3-7, keep step 1 (jitter capture) only if independently useful, mark
   the plan `abandoned` with the finding.** If it helps → keep, and note the Vulkan port as
   follow-up.
   — verify: user confirms the A/B outcome in-game.

## Review Pass (core.dev-loop.sk, PLAN_EXECUTE / EP-67) — 2026-09-07

Independent-reviewer branch (subagent-dispatch capability present). Reviewer got the focused source
diff + the plan Context/ACs + governance context inline; not the implementer's reasoning.

**Verdict: merge with nits — valid, correctly-shaped gated experiment, no blocking issues.** All 7
ACs verified; D3D12 state handling across frames + early returns confirmed correct; security clean;
ownership fit good (right module/layer/seam, uber-shader-with-mode matches the file's idiom, cleanly
revertible). Nits, all folded:
- test-coverage: no distinct signal for "game supplies no jitter params" vs "zero jitter this frame"
  → added a one-shot `LOG_WARN` in `EvaluateInternal` when the flag is on pre-SR and both
  `Jitter_Offset` gets fail, so the step-9 A/B can't misread an absent input as "idea failed".
- correctness (cosmetic): warm-up-frame log printed `curJitter` in the `prevJitter` slot → now prints
  the genuine stored prev (0,0) with a `[warm-up]` tag; first call always logs.
- convention: resolve `DispatchPass` keeps `motionIn` with no note → added a comment (resolve shader
  has no accumulator, never samples motion).
- convention: `jc.GuideWidth/Height` set but unread by mode 5 → removed.
Not changed: menu checkbox gated on `DlssNrRunBeforeSr` *intent* not runtime `frame.BeforeUpscale`
(reviewer nit) — every sibling pre-SR sub-control in that menu is gated the same way; behaviour
matches AC 3 (no-ops on the post-SR fallback). Consistency with the surrounding menu wins.

## Notes / risks

- **Speculative.** The retail NR core's history reprojection may not be MV-driven in a way
  this helps, or it may internally assume a jitter model we can't observe. Negative result is
  a real possible outcome — step 9 accounts for it.
- **Sign/units are the fiddly bit.** NGX `Jitter_Offset_X/Y` pixel convention and the game's
  MV encoding (`MvScaleX/Y`, Y-down vs Y-up) both vary by engine. The `DlssNrJitterCancelScale`
  knob (accepts negative) is the escape hatch for tuning without rebuilds; do not hardcode a
  sign in step 4.
- **`MvScale` == 0 or unset** → treat as 1.0 (matches the forwarder's `MV_Scale` default
  handling at `:3189-3193`).
- **Origin-zero MV assumption** is inherited from the existing code (forwarder sets
  `MVecSubrectBase = 0`); if a game ever supplies a non-zero MV subrect this pass is wrong in
  the same way the current path already is — not a new risk, but note it.
- **Do not touch the NGX param block's `MotionVectors`.** The scratch MV is a local
  substitution passed only into `g_compose->Dispatch`; SR reads the block directly.
- No push / PR without explicit user go-ahead.
