# Plan: Port wilsjo2 PR #77 — automatic exposure from the HDR frame, wider Trim, Trim Anchor Points

- Branch: `feat/nr-auto-exposure-trim-port` (cut from `main` at execution, on a clean tree, R9)
- Created: 2026-09-20
- Status: executed and built 2026-09-20 on `feat/nr-auto-exposure-trim-port` (6 commits, not pushed); Release x64 exit 0; shader and Vulkan GPU tests pass; not run in a game
- Task file: memory/tasks/feat_nr-auto-exposure-trim-port.md
- Ledger: `wilsjo2-fork:pr-77-auto-exposure-trim`
- Source: wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass PR #77 (open, not merged), one commit `59855487` by mattjaas, head fetched to `refs/port/wilsjo2-fork/pr-77`. Base of the PR is `codex/release-v0.8.4` (`8802b2b4`). GPL-3.0.

## Study

**What it does.** Adds a fourth White Point source, "Automatic exposure from HDR frame" (source `3`), next to manual / game exposure / scanned exposure. On the GPU: the linear HDR frame goes through a 64x64 luminance meter (one thread group per tile, all pixels read), a second pass reduces the 4096 tile means to a 1x1 exposure texture (`0.18 / (mean * 0.82)`, optional highlight compression), and the same frame's NR passes use it. No CPU delay. CPU readback is only for the menu and for capturing anchors. `PreExposure` is folded in; a missing value falls back to 1.0. Automatic exposure defaults to 5x Trim.

It also changes the two exposure-based sources that already exist here:
- Trim range goes from 0.25-4x to 0.25-50x (Game exposure still defaults to 1x).
- Both Game exposure and Automatic exposure get **Trim Anchor Points**: up to 8 pairs of `Base White Point -> Trim` (`BaseWhitePoint = PreExposure / Exposure`), Trim interpolated in log space between them, edge anchors held flat. Two separate tables, stored as `baseWhitePoint:trim;` strings in the ini. A "Preview" checkbox (not saved) ignores the anchors so the slider can be tuned live, then "Add Anchor point" captures the current key.
- Highlight protection slider (0-100 %, default 100): tiles more than a knee above the scene average are compressed before averaging.
- On Vulkan the auto exposure is also passed to the private DLSS-NR feature as its exposure resource (D3D12: `DLSSNR.ExposureTexture` on the model `Run`).

**Why it matters here.** Games that hand the upscaler no usable exposure texture currently get either a fixed slider or the experimental scan. This gives a dynamic White Point without the game's help. The PR text names Spider-Man Remastered (XeSS swapped to DLSS) as a case where the old 4x Trim ceiling was too low. No report from this fork asks for it.

**What it depends on.** The PR is written against wilsjo2's v0.8.4 restructure. That release split the DX12 NR code into `DlssNr_Dx12_Encode.cpp`, `_Exposure.cpp`, `_Run.cpp`, `_State.h`, `_ModelState.h`, `_Models.cpp`, `_Resources.cpp`, `_Status.cpp`, split the Vulkan feature into `_Vk_Internal.h` / `_Vk_Model.cpp`, and added `DlssNr_MenuInput.cpp` and `DlssNr_Status.cpp`. Our `main` has none of those files: the DX12 code is one 3996-line `DlssNr_Dx12.cpp` (free functions on `g_nr`), Vulkan is one `DlssNrFeature_Vk.cpp` (`g_vk`), the menu is `DlssNr_Menu.cpp`, and there is no status header. It does not apply as a patch.

**Trial.** Throwaway worktree, `git cherry-pick -n` of `59855487`: 8 modify/delete conflicts (every split DX12 file, plus `_MenuInput`, `_Status`, `_Vk_Internal`, `_Vk_Model`), content conflicts in `DlssNrFeature_Vk.cpp`, `DlssNr_Proxy.cpp/.h`, `DlssNr_Common.h`, and all four precompiled shader files plus `dlssnr.hlsl`. `Config.cpp`, `Config.h`, `DlssNr_Dx12.cpp` and `DlssNr_Vk.cpp` merged (`DlssNr_Dx12.cpp` only the dispatch-size change). The worktree was removed. So this is a hand adaptation, guided by the diff.

**Already have?** No. `git merge-base --is-ancestor 59855487 HEAD` is false. Symbols `AutoExposure`, `DlssNrAutoExposureTrim`, `ExposureTrimAnchor` are absent from `main`. What `main` does have and the port builds on: the meter (`kDlssNrMeterGrid`, `CopyMeterToReadback`, `ConsumeMeterReadback`, `InvalidateExposureMeter`), `DlssNrWhitePointSource` 0/1/2, `DlssNrWhitePointTrim`, `ExposurePreMul` / `UseGameExposure` in the constants, the scan in `DlssNr_ExposureScan.cpp`.

**Licence.** Repo GPL-3.0, single author's own code, nothing vendored. The binary shader files are build output and get regenerated here, not copied.

**Evidence it works.** The PR body says "testing across several games showed 5x Trim is a useful starting point". No test files, no logs, no comments or reviews on the PR. Not stated whether Vulkan was run. Treat as untested.

**Size.** 21 source files, +797 / -61, plus ~10 000 generated lines in the shader headers. Mapped onto our seams, about 600 hand-written lines.

**Recommendation.** Port, as three separable commits: (1) Trim range and Anchor Points for Game exposure, (2) automatic exposure on D3D12, (3) automatic exposure on Vulkan. Do not take the v0.8.4 file split. Keep the PR's source-3 numbering so configs stay compatible with wilsjo2's build.

## Differences to keep in mind (adapt, don't paste)

- **Constants struct.** Ours is 31 scalars (124 bytes) inside `alignas(256)`. The PR adds 25 scalars. Add only those 25. The PR's 4 extra `Residual*Padding` fields in the HLSL cbuffer exist because v0.8.4's struct has residual fields we do not; leave them out. 149 bytes fits, `static_assert(sizeof == 256)` still holds. The HLSL cbuffer must mirror the C++ struct field for field, in the same order.
- **`exposurePreMul` changes meaning.** Today `ExposurePreMul = preExposure * trim` and the shader computes `ExposurePreMul / e`. In the PR it is just `preExposure`, trim comes from new fields. Every place that turns on `UseGameExposure` must also fill `PreExposure` and the trim fields, or the shader reads a zero `gExposureTrim` (clamped up to 0.25x) and the picture goes dark. Fill sites in `main` (`git grep -n "ExposurePreMul\|UseGameExposure"`): `DlssNr_Dx12.cpp` encode (~2503) and resolve (~2910), and Vulkan. `DlssNr_DeferredSr.inl` (~493, 654, 661) also sets `ExposurePreMul`, but as a plain scale for its apply pass with the game-exposure flag unset; it stays untouched provided those constants are zero-initialised, so the new flag and trim fields read 0 and `WhitePoint()` falls through to `gWhitePoint`. Confirm that at the three sites, and that `DlssNr_Late.inl` never reaches the shader's `WhitePoint()` with the new fields unset.
- **Vulkan's shader used to compile the live white point out** (`#ifndef VK_MODE`). The PR makes `WhitePoint()` read the exposure from the motion slot on both backends, so the Vulkan resolve and encode must now bind the exposure texture there.
- **Meter shader.** The PR replaces mode 3's sparse sampling (bounded 8x8 taps per tile) with a full-tile group reduction, used only when `MeterCopiesExposure == 0`. The game-exposure courier path stays a 1x1 single read. Keep our existing meter and calibration (mode 4) untouched apart from that branch.
- **Scan source (2) stays as it is**, including its Trim range (`DlssNr_ExposureScan.cpp:846` keeps 4x). Our menu has real scan controls that v0.8.4's `ofScan = false` path hides; do not remove them.
- **Config.h BOM.** The PR removes a UTF-8 BOM from `Config.h` line 1. Do not.

## Steps

1. [x] **Branch** `feat/nr-auto-exposure-trim-port` from `main` on a clean tree. Commit this plan, the ledger entry and the task file first (they are uncommitted now).
2. [x] **Re-fetch and re-diff the PR.** It is open and can still change. If the head is not `59855487`, diff `59855487..new-head` and re-read before writing code.
3. [x] **Commit 1: Trim range and Anchor Points, Game exposure.**
   - `Config.h/.cpp`: `DlssNrAutoExposureTrim` (5.0), `DlssNrAutoExposureShadowProtection` (100.0), the two anchor strings, the two non-persisted preview flags, ini read and save with the same key names.
   - `DlssNr_Common.h`: the 25 new constants, plus `DlssNrMode_AutoExposure = 11`. Add the matching HLSL cbuffer fields (no padding fields).
   - `dlssnr.hlsl`: `ExposureTrimAnchorKey/Value`, `EffectiveExposureTrim`, `WhitePoint()` reading `gPreExposure` and the trim; resolve the white point once per pixel as the PR does.
   - `DlssNr_Dx12.cpp`: `ParseTrimAnchors`, `TrimForKey`, `FillExposureTrimConstants` as file-local helpers; use them in `ResolveWhitePoint` (line ~1183) and in the encode/resolve constants (~2426, ~3099); widen the 4x clamp to 50x for source 1. Fill the new fields at every site from the audit above.
   - `DlssNrFeature_Vk.cpp`: the same helpers, used where line ~1007 clamps Trim.
   - `DlssNr_Menu.cpp`: Game-exposure Trim slider 0.25-50x with Reset, the anchor controls (`RenderExposureTrimAnchorControls` and its parser, put beside the menu code), the live "white point" line using anchors.
4. [x] **Commit 2: automatic exposure, D3D12.**
   - `dlssnr.hlsl`: mode 3 parallel tile meter (`MeterCopiesExposure == 0`), the `gMode == 11` reduction, `groupshared gExposureReduce`, new `CSMain` signature with group ids.
   - `DlssNr_Dx12.cpp` `DispatchPass` (~1567): dispatch one group per tile when `Mode == Meter && MeterCopiesExposure == 0`; set `MeterCopiesExposure = 1` on the existing game-exposure meter dispatch.
   - `g_nr` state: `autoExposure` texture (R32_FLOAT 1x1), `autoExposureReadable`, `autoExposureValue`, `autoExposurePreExposure`, `autoExposureFrames`, `exposureReadbackSource`; `meterExposureValid[4]` becomes `meterExposureKind[4]` (0 none, 1 game, 2 auto) with `meterExposurePreExposure[4]`. Update `CopyMeterToReadback`, `ConsumeMeterReadback`, `InvalidateExposureMeter`, and the release path (`ParkNrResource`).
   - Encode (~2374-2430): the source-3 branch (meter, reduce, barriers, readback), reset the meter when the source changes, `usingAutoExposure` flag, `exposureTex = autoExposure`.
   - `DlssNr_Proxy.h/.cpp`: add the `exposure` parameter to `Context::Run` and set `DLSSNR.ExposureTexture`; pass it from the model `Run` call only when auto exposure is in use.
   - Status: `AutoExposureStatus()` for the menu. We have no `DlssNr_Status.cpp`: `ExposureStatus` and `GameExposureStatus()` are declared in `DlssNrFeature_Dx12.h` (118, 127) and defined at the end of `DlssNr_Dx12.cpp` (~3786); add `AutoExposureStatus()` and the `autoExposure` snapshot member there, and the Vulkan twin beside `ExposureOfferedVk`.
   - Menu: source list gets the 4th entry, range check `<= 3`, the auto-exposure Trim slider (default 5x, Reset to 5x), shadow protection slider, anchor controls, status text.
5. [x] **Commit 3: automatic exposure, Vulkan.** `DlssNrFeature_Vk.cpp`: `autoExposure` image, the meter and reduce dispatches, readback copy with the host barrier, `meterExposureKind`, source-change reset, `autoExposureActive` in the status, CPU white point for source 3, exposure forwarded to the private feature. `DlssNr_Vk.cpp` `Dispatch`: same one-group-per-tile rule.
6. [x] **Commit 4: regenerate shader binaries.** After the last shader edit, recompile `dlssnr.hlsl` with `dxc` for DX12 (`DlssNr_Shader.cso` + `.h`) and Vulkan (`DlssNr_Shader_Vk.spv` + `.h`) the way earlier tasks did. Both must compile clean. Never copy the PR's generated files: they were built from v0.8.4's HLSL.
7. [x] **Provenance.** Each source commit carries `Ported-from: wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass PR #77 (59855487)` and `Co-Authored-By: Claude Sonnet 5`; author set to `mattjaas <183701493+mattjaas@users.noreply.github.com>` since the code is adapted by hand. Add a `docs/CREDITS.md` entry (substantial feature).
8. [x] **Build** Release x64 (PowerShell, memory: build allowed) and read the log for warnings in touched files.

## Verification

- **No game needed:** `dxc` compiles both shader targets clean; Release x64 builds; the existing `tests/nr_*_smoke` shader smoke tests still pass if run (user says whether to run them); `git grep` audit shows every site that sets `UseGameExposure` also fills the new fields, and the DeferredSr sites leave them zero; the anchor parser, checked by hand on the strings `""`, `"100:5;"`, `"50:2;200:8;"`, a malformed pair and a 9th anchor.
- **Needs a game, not claimed until run:** an HDR-linear-input NR game on D3D12 with source 3 (exposure follows scene lighting, no flicker, shadow protection visibly changes it); the same on a Vulkan NR game; Add Anchor point plus Preview on Game exposure and on Automatic; switching between sources 1, 2, 3 in game (no stale exposure held); a game already on Game exposure or Scan with no anchors looks exactly as before; the finished-picture and Late/DeferredSr paths, which use the constants without the encode.

## Risks

- **Constants mismatch.** The struct and cbuffer are hand-mirrored. One field out of order or a filler that skips the new fields gives a wrong white point with no error. Mitigated by the audit and by comparing the HLSL and C++ field order by script.
- **Regression for existing users.** The trim maths moves from CPU-side `preExposure * trim` into the shader. With no anchors and Trim 1x it should give the same white point; check numerically on a captured pair (`preExposure`, `exposure`) before and after.
- **Vulkan behaviour change** for Game exposure: it now sees the shader-side white point path that was compiled out.
- **Drift from wilsjo2.** Our layout stays monolithic while his is split. A later port of `wilsjo2-fork:v0.8-nr-rewrite` will collide with this adaptation in the same functions.
- **Open PR.** May change or be rejected upstream; the source-3 number and ini keys are then ours alone.
- **Untested at source.** No reported test evidence for Vulkan or for the highlight-protection maths.

## Out of scope

The v0.8.4 file split, residual/`ResizePrivateGuides` changes, the four `Residual*Padding` fields, the BOM change, any change to the scan source.

## Rollback

Work on the branch; the four commits revert independently in reverse order (binaries, Vulkan, D3D12, Trim/Anchors). Reverting commit 1 alone after 2 or 3 will not compile, so revert from the top.

## Execution log

- **2026-09-20:** studied (diff read in full, trial cherry-pick in a throwaway worktree); plan written. Not started.
- **2026-09-20, executed.** Branch `feat/nr-auto-exposure-trim-port` from `main` (`b0191d86`). PR head re-checked: still `59855487`, open. Commits: `f1e7ca9b` config, constants and shader groundwork; `8515b5f4` Game exposure Trim to 50x and Trim anchors (D3D12 CPU and shader, Vulkan CPU, menu); `3ebd6f50` automatic exposure on D3D12; `66c1beca` automatic exposure on Vulkan; `69fcf9e4` regenerated shader binaries; `0a4e85d4` GPU smoke test and credits line. The first four source commits authored as mattjaas with `Ported-from:` trailers. Differences from the plan and the PR, all deliberate:
  - **Mode number.** Automatic exposure is mode 13 here; 11 and 12 were already `ZeroMotion` and `ClampProxy`.
  - **Constants.** 25 new scalars (149 bytes of 256); the PR's 4 residual padding fields left out.
  - **Shared helper.** The anchor parse/interpolate/serialize code lives once in `shaders/dlssnr/DlssNr_TrimAnchors.h`; the PR had three copies (D3D12, Vulkan, menu). The shader keeps its own copy of the interpolation.
  - **Vulkan live path.** The PR's shader reads the exposure from the motion slot on Vulkan; the plan said Vulkan compiled the live path out. Ported as the PR has it: only automatic exposure sets the flag on Vulkan, the game's own exposure stays on the CPU. The meter image goes from 8x8 to 64x64.
  - **Model exposure not forwarded.** The PR hands the automatic exposure to the NR model as `DLSSNR.ExposureTexture`. Here the model call goes through the forwarder (`dlssnr_call_evaluate_v2` / `dlssnr_vk_evaluate_v2`), which has no such argument and does not get the game's exposure either, so that part is not ported. If a game looks different from wilsjo2's build this is the first suspect.
  - **`DlssNr_Proxy`** is not touched: in this tree it is not the main model path.
  - **Feedback hazard, checked by reading.** This fork removed a frame-statistics white point because it read its own output. The automatic meter reads `target` at encode entry (the upscaler's fresh output) before anything of ours writes to it; not run in finished-picture mode. Only a game can confirm no feedback: toggle NR at a fixed spot and compare the reported exposure on and off.
- **2026-09-20, verification.** Release x64: `MSBuild ... /t:OptiScaler` exit 0, 0 errors, no new warnings in touched files (`x64/Release/a/OptiScaler.dll` 00:51). `dxc` compiled both targets clean. `tests/nr_vulkan_shader_smoke` (existing) passes on the new SPIR-V. New `tests/nr_auto_exposure_smoke` (17 checks) passes on an RTX 5070 Ti: meter and reduction against hand-worked exposures, and the shader's live white point (Trim, 50x clamp, anchors, flat ends, single anchor, Preview, CPU fallback). A host check of `DlssNr_TrimAnchors.h` (parse, malformed input, 9th anchor, upsert, interpolation, struct layout `static_assert`s) also passed; that one is not committed. No game run.
- **2026-09-20, Review Pass fixes** (self-review fallback; no subagent). Fixed: the medium finding (`ReleaseResources` reset `exposureReadbackSource`, clearing the held game exposure on every recreate; the PR has the same line), the layout `static_assert`s in `DlssNr_TrimAnchors.h`, and the anchor host test is now committed as `tests/nr_trim_anchors_smoke.cpp` (passes). Not changed: the per-pixel `WhitePoint()` finding was overstated -- the encode calls it once per thread, so hoisting inside the shader saves nothing; a real change would cache it in groupshared behind a barrier, unmeasured, so it is left. The Vulkan game-exposure readback (16 KB) and the descriptor-ring headroom are noted, not changed. Rebuilt Release x64 (exit 0, 01:20).
