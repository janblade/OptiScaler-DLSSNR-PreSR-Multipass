# Plan: Port cmh1448's MV/depth subrect metadata handling to DLSS-NR

- Branch: fix/dlssnr-mv-subrect-metadata  (off main; main is protected — no direct commits)
- Created: 2026-09-09
- Status: abandoned      # superseded by wilsjo2 v0.7.3 (cb1f7aa3), synced into main 2026-09-09

## ABANDONED 2026-09-09 — upstream shipped it first, and better

While preparing the wilsjo2 v0.7.2/v0.7.3 sync, `cb1f7aa3` ("Release v0.7.3 with KCD2
pacing and NR motion-vector fixes", co-authored with cmh1448) turned out to be wilsjo2's
own port of the same cmh1448 `6446cc8d` motion-metadata fix this plan implemented. Theirs
is the superset:
- pre-SR + multipass + native Vulkan (this plan scoped to post-SR + proxy only);
- the guide-region math factored into a shared `OptiScaler/shaders/dlssnr/DlssNr_Guides.h`
  (`ResolveGuideRegions` / `GuideSubrect`) with `tests/nr_guides_smoke.cpp`;
- `DlssNrFrameInfo` also carries explicit `OutputWidth/OutputHeight` (this plan inferred the
  display extent from `active->width` and hand-fixed the depth-base nuance in review — they
  plumb it directly);
- forwarder ABI versioned as `dlssnr_{call,vk}_evaluate_v2` "to reject stale helpers"
  (this plan used `_ex` + a legacy zero-filling wrapper — same idea, their naming).

Our branch `fix/dlssnr-mv-subrect-metadata` (uncommitted WIP, never pushed) is discarded;
the local branch is deleted. The implementation experience + the cmh1448 review in this
plan stay as the record. No code from this plan lands.
- Task file: memory/tasks/main.md
- Source: cmh1448/OptiScaler_DLSSNR commit fa78de5c (merge of 6446cc8d,
  "Fix DLSS NR motion vector metadata handling")

## Context

**What it does.** Our DLSS-NR post-SR path hard-codes the motion-vector and depth *subrect*
metadata sent to the model: base offset (0,0), and MV valid-region = guide (render)
dimensions. That is wrong for two real cases cmh1448's `fa78de5c` fixes:
1. **Display-resolution motion vectors** — when the game does *not* set
   `NVSDK_NGX_DLSS_Feature_Flags_MVLowRes`, MVs are at output resolution, not render
   resolution. Telling the model its MV region is `renderW x renderH` makes it reproject
   from a small top-left crop of a full-res texture.
2. **Non-zero subrect base** — dynamic-resolution titles that render guides into a
   non-origin corner.

**Approach (decided with user 2026-09-09).** Port only what cmh1448 did — the post-SR
native path + the D3D12 proxy path — on **both DX12 and Vulkan**, and keep the forwarder
ABI backward-compatible via **new `_ex` exports** with the old exports kept as
zero-filling wrappers (byte-identical to today's behaviour).

**Acceptance criteria.**
1. When the game sets `MVLowRes` and uses zero subrect offsets (the common case), the
   model receives byte-identical parameters to today — the legacy-wrapper path produces
   the same `DLSSNR.*Subrect*` values.
2. When `MVLowRes` is absent, `DLSSNR.MVecSubrectWidth/Height` = display resolution
   (clamped to the MV allocation), not render resolution.
3. Non-zero `DLSS_Input_{Depth,MV}_Subrect_Base_{X,Y}` are read, clamped to the
   allocation, and forwarded as `DLSSNR.{Depth,MVec}SubrectBase{X,Y}`; guide dims are
   reduced by the offset.
4. An old forwarder DLL (no `_ex` export) + new `OptiScaler.dll` -> host falls back to the
   legacy export, no crash, behaviour unchanged.
5. DX12 multipass and the proxy path both carry the new values through every pass.
6. Vulkan native NR path carries them through every pass.
7. Both x64 configs build clean; no new warnings in the changed files; forwarder DLL
   rebuilt.
8. In-game: a DLSS title on the post-SR NR path shows no regression.

**Out of scope.** The pre-SR path — `DlssNr_DeferredSr.inl`'s "inactive on non-zero
colour/guide/output offsets" guard and the VK `beforeSr` early-return on `baseX||baseY`
stay as they are (cmh1448 didn't touch pre-SR; reversing that "refuse rather than get it
wrong" decision is a separate call). DX11 is covered for free — it never calls the
evaluate export directly, and any untouched call routes through the legacy wrapper.

**Verification reality.** `has_tests:false`. Every step gates on clean Debug|x64 +
Release|x64. AC 2/3 need a game that actually exercises display-res MVs or non-zero
offsets; if none is on hand that is disclosed per the Verification-Before-Completion
Protocol, never ticked silently.

## Current-structure notes (verified 2026-09-09)

- `DlssNrFrameInfo` — `DlssNr_Common.h:96` area (`RenderSubrectWidth/Height` ~128-129).
  cmh1448's struct hunk applies verbatim.
- **DX12 read site:** `EvaluateInternal`, `DlssNr_Dx12.cpp:3095` (`DlssNrFrameInfo frame{}`),
  `createFlags` in scope at :3096, `Render_Subrect_Dimensions` read at :3125-3126.
- **DX12 compute + consume site:** `DlssNr_Dx12::Dispatch` (:1597-~2600, one function).
  `guideDesc`/`guideWidth`/`guideHeight` at :1675-1677; RenderSubrect clamp block
  :1695-1716; `active` ColorExtent (post-SR = `{desc.Width, desc.Height}` = display res)
  at :1653-1656; single `mvToWork` at :2485; `Proxy::Run` call at :2496; `g_nr.evaluate`
  **pass loop** at :2587. Locals here reach both call sites (same scope as `mvToWork`).
- **DX12 evaluate PFN:** `PFN_NrEvaluate` typedef `DlssNr_Dx12.cpp:160-163`; resolved :571;
  optional exports already resolved with null-tolerance at :574-576 (pattern to mirror).
- **Proxy** (`DlssNr_Proxy.{h,cpp}`) — our static code, not the DLL: `Run` decl
  `DlssNr_Proxy.h:43`, body sets `DLSSNR.{Depth,MVec}Subrect*` at `DlssNr_Proxy.cpp:269-276`
  (bases `0u`, dims `guideWidth/guideHeight`). One call site. No ABI concern.
- **Forwarder** (`dlssnr_forwarder.cpp`, own `.vcxproj` in `OptiScaler.sln`):
  `dlssnr_call_evaluate` :818, `dlssnr_vk_evaluate` :694; both `__cdecl __declspec(dllexport)`,
  resolved by name. Hardcoded `MVecSubrectBaseX/Y=0`, `MVecSubrectWidth/Height=guideWidth`
  at :730-733 and :859-862. No version handshake.
- **VK:** workhorse is `EvaluateAtSeamVk` (`DlssNrFeature_Vk.cpp:517-1306`), not the tiny
  `EvaluateAfterUpscaleVk` wrapper (:1319). `GameCreateFlags` helper :483;
  `createFlags`/`depthInverted` :932-934; `guideWidth/guideHeight` **`const`** :669-670;
  MV allocation dims = `motion->Resource.ImageViewInfo.Width/Height` (used :672-673);
  per-axis `mvX/mvY` **already** computed :1184-1189; `g_vk.evaluate` **pass loop**
  :1198-1203. `PFN_VkEvaluate` typedef :34; resolved :449.

## Steps

1. [x] **Branch + `DlssNrFrameInfo` fields.** DONE. Branch `fix/dlssnr-mv-subrect-metadata`
   created off `main`. Added `DepthSubrectBaseX/Y`, `MotionSubrectBaseX/Y`
   (`unsigned int = 0`), `MotionVectorsLowResolution` (`bool = false`) after
   `RenderSubrectHeight` in `DlssNr_Common.h` with a comment block. Pure struct growth with
   defaults; nothing reads them yet. — verify: folded into the step-5 build checkpoint.

   NOTE — builds are batched at coherent checkpoints rather than one per step: after step 5
   (all OptiScaler.dll host changes; steps 2-5 touch overlapping TUs and step 5's signature
   change needs its call site updated in the same step to compile), after step 7 (forwarder
   DLL), and the full both-configs build at step 10. Intermediate single-step states do not
   all compile in isolation.

2. [x] **DX12 read site.** DONE. `EvaluateInternal`:
   `frame.MotionVectorsLowResolution = (createFlags & NVSDK_NGX_DLSS_Feature_Flags_MVLowRes) != 0;`
   next to `DepthInverted`; four `params->Get(... Depth_Subrect_Base_X/Y, MV_SubrectBase_X/Y ...)`
   after the `Render_Subrect_Dimensions` reads. The diagnostic log moved to step 3's compute
   site (no consolidated frame-info log existed to extend; the clamped values are what matters).

3. [x] **DX12 compute site.** DONE. In `Dispatch`, right after the RenderSubrect clamp block,
   before `g_nr.guideWidth = guideWidth`: `motionDesc = motion->GetDesc()`; `depthBaseX/Y` =
   `min(frame.DepthSubrectBase*, guideDesc.W/H)`; `guideWidth/Height` reduced by the depth
   base; `motionBaseX/Y` = `min(frame.MotionSubrectBase*, motionDesc.W/H)`; `wantedMotion{W,H}`
   = `MotionVectorsLowResolution ? guide* : width/height` (used the existing `width`/`height`
   locals = `active->width/height`, :1662-1663); `motionWidth/Height` = `min(wanted*,
   motionDesc.* - motionBase*)`. All `const` function-scope locals; reach both call sites.
   Throttled `LOG_INFO` fires only when a base != 0 or the motion region != guide region.

4. [x] **DX12 per-axis `mvToWork`.** DONE. `mvToWork` -> `mvToWorkX = workWidth/width`,
   `mvToWorkY = workHeight/height`. Both proxy and pass-loop evaluate now use the X/Y pair
   (pass loop factors it to per-pass `mvX`/`mvY` locals). No-op under uniform `workScale`.

5. [x] **`Proxy::Run` signature + body.** DONE. `DlssNr_Proxy.h`/`.cpp`: +6 uint params
   (`motionWidth, motionHeight, depthBaseX, depthBaseY, motionBaseX, motionBaseY`) after
   `guideHeight`. Body: `DepthSubrectBaseX/Y`, `MVecSubrectBaseX/Y`, `MVecSubrectWidth/Height`
   now driven from them (were `0u` / `guideWidth`). Call site in `Dispatch` updated.
   — verify: **Release|x64 built clean, 0 errors** (steps 1-5 + 8 DX12 host). Forwarder
   project also rebuilt unchanged and linked fine.

6. [x] **Forwarder DX12 `_ex` export.** DONE. `dlssnr_call_evaluate` body renamed
   `dlssnr_call_evaluate_ex` with `motionWidth, motionHeight, depthBaseX, depthBaseY,
   motionBaseX, motionBaseY` after `guideHeight`; `DLSSNR.{Depth,MVec}SubrectBase{X,Y}` and
   `DLSSNR.MVecSubrectWidth/Height` driven from them. Legacy `dlssnr_call_evaluate` re-added
   as a wrapper -> `_ex(..., guideWidth, guideHeight, 0,0,0,0, ...)`. No `.def` file — pure
   `__declspec(dllexport)`, new symbol exported automatically.

7. [x] **Forwarder VK `_ex` export.** DONE. Same transform: `dlssnr_vk_evaluate_ex` +
   legacy `dlssnr_vk_evaluate` wrapper.
   — verify: `dumpbin /exports x64/Release/a/nvngx.dll_dlssnr.dll` shows
   `dlssnr_call_evaluate`, `dlssnr_call_evaluate_ex`, `dlssnr_vk_evaluate`,
   `dlssnr_vk_evaluate_ex`. Forwarder builds clean.

8. [x] **DX12 host: resolve `_ex` with fallback.** DONE. `PFN_NrEvaluateEx` typedef (6 uints
   after guideHeight); `NrState::evaluateEx` (nullable). `LoadForwarder` resolves
   `dlssnr_call_evaluate_ex` (null-tolerant). Pass loop: one-shot `LOG_INFO` ("extended
   (subrect metadata)" / "legacy") before the loop; inside, `if (g_nr.evaluateEx) ... else
   g_nr.evaluate(...)` with shared per-pass `mvX`/`mvY`. Built clean.

9. [x] **VK host: compute + resolve `_ex`.** DONE. `PFN_VkEvaluateEx` typedef;
   `VkState::evaluateEx` (nullable) resolved from `dlssnr_vk_evaluate_ex`. In
   `EvaluateAtSeamVk` after `depthInverted`: `lowResolutionMotion` from `createFlags`; read
   the 4 base params; `depthAllocW/H` + `motionAllocW/H` from the view infos; `depthBaseX/Y`,
   `motionBaseX/Y` clamped to the allocations; `effGuideWidth/Height` (new locals, the
   `:679-680` pair stays `const`) = `min(guideW/H, depthAlloc - depthBase)`; `motionWidth/Height`
   = `min(lowResMotion ? effGuide : width/height, motionAlloc - motionBase)`. Throttled
   `LOG_INFO` on any non-zero base or motion != guide. Pass loop: one-shot path log +
   `if (g_vk.evaluateEx) ... else g_vk.evaluate(...)`, both now passing `effGuideWidth/Height`.
   VK's `mvX/mvY` already per-axis (:1198-1199) — untouched.

10. [x] **Full build, both configs.** DONE. 64-bit `amd64\MSBuild.exe`, `OptiScaler.sln`.
    Debug|x64: **Build succeeded, 0 Error(s)**, 63 Warning(s) (whole-solution baseline).
    Release|x64: **Build succeeded, 0 Error(s)**, 26 Warning(s) (baseline). Forced recompile
    of the 4 changed `.cpp` (+ `DlssNr_Common.h` dependents) -> **zero warnings referencing
    any changed file**. Both `OptiScaler.dll` and `nvngx.dll_dlssnr.dll` rebuilt in each config.

11. [~] **Review Pass DONE; in-game PENDING (user).** `core.dev-loop.sk` Review Pass ran as
    the **self-review fallback branch** (no subagent dispatched — honouring the session's
    "don't spawn agents unless asked" rule; the skill's honestly-labelled Path B). Cold pass
    over the full diff, five checks:
    - **Correctness:** underflow guarded on every `alloc - base` (base is `min(raw, alloc)`);
      legacy-forwarder + zero-offset path proven byte-identical (wrapper reproduces the exact
      old args); `_ex` typedef param count matches the forwarder signature (10 uints then
      int,int); VK `const guideWidth/Height` validation untouched.
    - **Conventions:** comment style, throttled-`static said*`-log idiom, `(unsigned int)`
      casts on `RESOURCE_DESC::Width`, null-tolerant optional-export resolution — all match
      the surrounding code.
    - **Security:** values flow only into `setUInt` NGX params; no indexing, no format-string
      risk; absent `params->Get` keys leave the pre-zeroed default.
    - **Ownership fit:** metadata compute sits by the existing RenderSubrect clamp; the
      `_ex`/legacy split is this forwarder's established ABI-evolution pattern; no needless
      config knob (correct — fully automatic). The 20-arg `if/else` evaluate duplication is
      the least-indirection option given the fallback requirement.
    - **One nuance fixed during review:** the low-res-motion "wanted" size now uses the
      render-area extent *before* the depth base trims it (new `renderGuideWidth/Height` in
      DX12; the `const guideWidth` in VK), matching cmh1448 and avoiding a ≤depthBase-pixel
      under-report when `depthBase != 0 && MVLowRes` coincide. Rebuilt both configs clean.
    - **Test-coverage disclosure:** `has_tests:false`. **AC 1 & 4 verified** (byte-identical
      common case + old-forwarder fallback — by construction + `dumpbin` export check + clean
      build). **AC 2 & 3 NOT verified** — need a game that supplies display-resolution motion
      vectors or non-zero subrect bases; none confirmed on hand. The throttled `LOG_INFO`
      lines ("motion region WxH (display-res)", "depth base XxY ...") are the in-game probe.
    — remaining: user runs a DLSS title on the post-SR NR path, confirms no regression;
      ideally A/Bs a display-res-MV title.

## Risks / notes

- **ABI:** the `_ex` split removes the stack-corruption risk entirely — a missing `_ex`
  symbol just selects the legacy path.
- **Underflow:** `guideDesc.Width - depthBaseX` is safe because `depthBaseX` is
  `min(..., guideDesc.Width)`. Same for motion.
- **`UINT64` desc dims** cast to `unsigned int` as cmh1448 does.
- **`has_tests:false`** — AC 2/3 depend on finding a game that exercises the new paths;
  disclose if unavailable.
- No push / PR without explicit user go-ahead.
