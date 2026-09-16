# Plan: Vulkan port of the SGSR1 reduced-resolution enlarge pass (PR #8 + #9 parity)
- Branch: feat/dlssnr-sgsr1-vulkan
- Created: 2026-09-16
- Status: in-progress (10/12, code complete, awaiting in-game verification)
- Task file: memory/tasks/feat_dlssnr-sgsr1-vulkan.md

## Context

PR #8 shipped DLSS-NR's SGSR1 enlarge pass (Qualcomm Snapdragon GSR v1, ported from scratch) for
the `workScale < 1.0` reduced-model-resolution case, explicitly scoped DX12-only
(`SGSR1_Dx12.{h,cpp}`, `sgsr1.hlsl` with no `VK_MODE` variant, no `.spv` build). PR #9 (already
merged and released as `v0.1.1-upscale-method-toggle`) built on top of that, still DX12-only: a
3-way `DlssNrReducedUpscaleMethod` (0 Bilinear / 1 SGSR1 answer-only / 2 SGSR1 both sides) and
live-tunable `DlssNrSgsr1EdgeThreshold`/`DlssNrSgsr1EdgeSharpness`, default threshold retuned to
0.3 from an in-game A/B against photoreal skin/hair noise.

Vulkan's reduced leg (`DlssNrFeature_Vk.cpp`) currently has no SGSR1 branch at all -- `modelInput`
still resolves to `g_vk.proxySmall` (the implicit-bilinear downsample target) and the resolve's
`resolveAnswer`/`resolveProxy` selection (~line 1263-1279) only branches on the `workScale > 1.0`
supersample leg, which already has real Vulkan parity via `OS_Vk`-based `superUp`/`superDown`.
Separately, PR #8 did wire Vulkan's `ModelWorkScale` (the gate the Replace-mode detail-injection
feature reads, `DlssNrFeature_Vk.cpp:1029`) for parity, so Vulkan already benefits from that fix
independent of this plan -- confirmed by the PR's own test-plan note ("Vulkan path is wired for
parity... not separately verified in-game").

Scope chosen via `AskUserQuestion` (user picked the recommended option): port the *current* DX12
state, i.e. both PR #8's base pass and PR #9's toggle/tunables in one pass, not PR #8's original
snapshot alone -- avoids shipping Vulkan behind an already-released DX12 default and a guaranteed
follow-on plan later.

**Acceptance criteria:**
- New `SGSR1_Vk` class (`Shader_Vk`-derived), mirroring `SGSR1_Dx12`'s public interface
  (`Dispatch(cmd, in, out, reversibleMode, passthrough, edgeThreshold, edgeSharpness)`), built on
  the `OS_Vk` precedent (one UBO + one sampled image + one storage image + one sampler, 4
  bindings) rather than `RCAS_Vk`'s depth-adaptive dual-pipeline shape, which SGSR1 doesn't need.
- `sgsr1.hlsl` gains a real `#ifdef VK_MODE` resource-binding variant (`[[vk::binding(slot, 0)]]`
  alongside the existing DX12 `register()` bindings), matching `rcas.hlsl`/`dlssnr.hlsl`'s own
  established pattern -- not a separate shader file.
- `DlssNrReducedUpscaleMethod`/`DlssNrSgsr1EdgeThreshold`/`DlssNrSgsr1EdgeSharpness` (existing
  shared `Config` fields, already exposed in the menu) now also drive Vulkan's reduced up-leg with
  the exact same 3-way semantics DX12 already has and already shipped.
- `g_vk`'s state gains `proxyNative` (`OwnedImage`) and `sgsr1UpAnswer`/`sgsr1UpProxy`
  (`std::unique_ptr<SGSR1_Vk>`), mirroring `NrState`'s DX12 fields; `outputNative`'s creation gate
  widens from effectively `workScale > 1.0f` only to `workScale != 1.0f`, matching DX12's
  `CreateScratch` gate.
- `resolveProxy`/`resolveAnswer` selection at the VK resolve site widens to the same 3-way branch
  DX12 has, falling back to today's implicit-bilinear behaviour exactly as now whenever SGSR1 is
  unavailable or method 0 (Bilinear) is selected.
- Debug + Release x64 build clean, 0 new warnings.
- Menu controls (`DlssNr_Menu.cpp`) need no changes -- confirm they already work correctly when
  the active feature is Vulkan-backed (no hidden DX12-only gate).
- In-game confirmation on a Vulkan title: all three methods produce a plausible image, log lines
  confirm per-method engagement, edge-threshold/sharpness sliders visibly affect the Vulkan image
  the same way they already do on DX12 (0.3 default reading close to Bilinear).

**Out of scope:**
- Any new design decisions -- this reproduces DX12's already-tested, already-shipped behaviour,
  not a fresh exploration.
- The `workScale > 1.0` supersample leg -- already has real Vulkan parity via `OS_Vk`-based
  `superUp`/`superDown`, untouched here.
- Any tunable or mode beyond what DX12 already has (no new capability, parity only).

## Steps

1. [x] Add `#ifdef VK_MODE` bindings to `sgsr1.hlsl`: `[[vk::binding(0, 0)]]` on the UBO,
   `(1, 0)` on `InputTexture`, `(2, 0)` on `OutputTexture`, `(3, 0)` on the sampler (binding order
   matching what `OS_Vk`/`RCAS_Vk` already expect -- UBO, sampled image, storage image, sampler),
   alongside the existing DX12 `register(b0)`/`register(t0)`/`register(u0)`/`register(s0)`
   attributes, following `rcas.hlsl`'s exact `#ifdef VK_MODE` placement pattern. -- verify: DX12
   compile (`dxc -T cs_6_0 ...`, no `-D VK_MODE`) produces a byte-identical `.cso` to before this
   change, confirming the VK-only additions are inert for DX12.
2. [x] Compile the VK variant: `build_precompiled_shader_vk.bat sgsr1` (from
   `shaders/shader_tools`) -> `sgsr1_Shader_Vk.spv`/`sgsr1_Shader_Vk.h`. -- verify: dxc succeeds,
   valid SPIR-V magic number in the generated header.
3. [x] New `SGSR1_Vk` class (`shaders/sgsr1/SGSR1_Vk.h/.cpp`), `Shader_Vk`-derived, modeled on
   `OS_Vk` (not `RCAS_Vk`). One UBO mirroring `Sgsr1Constants` from `SGSR1_Dx12.cpp` (ViewportInfo,
   DstSize, ReversibleMode, Passthrough, EdgeThreshold, EdgeSharpness), one sampled image in, one
   storage image out, one bilinear-clamp sampler. `Dispatch()` signature matches
   `SGSR1_Dx12::Dispatch`, adjusted for Vulkan types: `(VkCommandBuffer, const VkImageInfo& in,
   const VkImageInfo& out, uint32_t reversibleMode, uint32_t passthrough, float edgeThreshold,
   float edgeSharpness)`. -- verify: builds against `Shader_Vk`'s existing interface, no new
   virtuals needed (matches `SGSR1_Dx12`'s own precedent).
4. [x] `OptiScaler.vcxproj`/`.vcxproj.filters`: add `shaders\sgsr1\SGSR1_Vk.{h,cpp}` -- new source
   files aren't auto-picked-up (no wildcard globbing), the same gotcha the original `SGSR1_Dx12`
   port hit. -- verify: builds, no unresolved externals.
5. [x] `DlssNrFeature_Vk.cpp`'s `g_vk` struct: add `OwnedImage proxyNative;` (native-res enlarged
   proxy, parallel to `outputNative`) and `std::unique_ptr<SGSR1_Vk> sgsr1UpAnswer, sgsr1UpProxy;`
   -- two separate instances, not one reused twice a frame, same rationale as DX12's
   `sgsr1UpAnswer`/`sgsr1UpProxy` (one `Dispatch()`/frame per instance; a single instance would
   have both CPU-side constant writes land before either GPU dispatch executes). -- verify:
   compiles once call sites are updated in later steps.
6. [x] Widen `outputNative`'s creation gate (currently effectively `workScale > 1.0f` only, the
   `workScale <= 1.0f || CreateImage(...)` short-circuit at ~line 889) to `workScale != 1.0f`,
   matching DX12's `CreateScratch` gate. Add `proxyNative`'s creation gated on `reduced &&
   workScale < 1.0f && cfg.DlssNrReducedUpscaleMethod.value_or_default() == 2` (only method 2
   needs it), matching DX12's identical gate. -- verify: buffers allocate/release correctly across
   a `workScale` change; add both to whatever teardown path `outputNative`/`superUp`/`superDown`
   already go through so nothing leaks.
7. [x] At the reduced-leg dispatch site (~line 1073-1142, where `modelInput` currently resolves to
   `g_vk.proxySmall`), add the SGSR1 up-leg dispatch mirroring `DlssNr_Dx12.cpp`'s ~2941-3012
   block: `wantsSgsr1Answer = method >= 1`, `wantsSgsr1Proxy = method == 2`; lazily construct
   `sgsr1UpAnswer`/`sgsr1UpProxy`; `Dispatch()` each with `resolve.ReversibleMode`/`.Passthrough`
   and `cfg.DlssNrSgsr1EdgeThreshold`/`DlssNrSgsr1EdgeSharpness`; set `sgsrAnswerOk`/`sgsrProxyOk`
   on success; `LOG_INFO` the engagement state using the same change-gated static-bool pattern DX12
   uses (log once per state change, not every frame), so a log scan can confirm Vulkan parity the
   same way it did for DX12. -- verify: log line appears once per state change.
   **Placement correction made during execution:** the plan's draft placement (at the `modelInput`
   site, pre-model-eval) was wrong -- the answer-side enlarge needs the model's actual output
   (`answer`), which does not exist until after the NGX evaluate loop runs. Re-checked DX12's own
   dispatch ordering and confirmed its up-leg block (~2941-3012) also runs post-eval, inside the
   resolve-preparation code, not at its `modelInput` site either. Implemented in the resolve
   section instead (right after `resolve.Mode = DlssNrMode_Resolve;`, before the existing
   supersample down-leg check), matching DX12's actual ordering rather than the plan's draft one.
8. [x] At the resolve dispatch site: widen `resolveProxy`/`resolveAnswer` selection to the 3-way
   branch DX12 already has at `DlssNr_Dx12.cpp:3032-3033`. Implemented as two `if` statements
   (`if (sgsrAnswerOk) resolveAnswer = &g_vk.outputNative;` / `if (sgsrProxyOk) resolveProxy =
   &g_vk.proxyNative;`) placed after the reduced-leg block and before the existing supersample
   down-leg block, rather than one combined ternary -- avoids restructuring the existing
   supersample code, and workScale's sign makes the two blocks mutually exclusive in practice
   (never both `> 1` and `< 1` in the same frame), so the two forms are behaviourally identical.
   -- verify: code review against the DX12 ternary shape, same effective selection.
9. [x] Lifecycle: add `sgsr1UpAnswer`/`sgsr1UpProxy`/`proxyNative` to Vulkan's shutdown/release
   path (wherever `superUp`/`superDown`/`outputNative` already release) and confirm (not assume)
   whether the enlarge-method setting needs the same `vkDeviceWaitIdle` resize-drain the
   `nrScaler` filter change already has -- check whether DX12's `SGSR1_Dx12` needed an equivalent
   live-toggle drain (it did not, no per-frame state a live switch could corrupt) and whether the
   same holds for `SGSR1_Vk`. -- verify: toggling the enlarge-filter combo mid-session doesn't
   crash or leak on Vulkan.
   **Confirmed by code inspection**: `sgsr1UpAnswer`/`sgsr1UpProxy` are constructed lazily on
   first use and never rebuilt -- unlike `OS_Vk`'s scaler switch (which loads a *different* SPIR-V
   pipeline per filter choice, so a live toggle genuinely needs the drain-and-rebuild dance),
   `SGSR1_Vk` always loads the same single fixed shader. Toggling `DlssNrReducedUpscaleMethod`
   live only changes whether `Dispatch()` gets called, not what the instance contains -- nothing
   for a drain to protect. No drain added, matching DX12's own identical no-drain precedent.
10. [x] Debug + Release x64 full build (Vulkan and DX12 compile in the same solution/config in this
    project). -- verify: 0 errors, 0 new warnings vs baseline. DONE: both configs built clean via
    local MSBuild, exit 0, 0 errors both; warning counts (64 Release / 63 Debug) match this
    session's own pre-existing baseline from the upscale-method-toggle branch build, and neither
    log has a single warning from `SGSR1_Vk`, `DlssNrFeature_Vk.cpp`, or `sgsr1.hlsl` (grepped
    explicitly).
11. [ ] In-game check on a Vulkan title: cycle all three `DlssNrReducedUpscaleMethod` values,
    confirm log lines show correct per-method engagement, confirm SGSR1's sharper look matches the
    already-shipped DX12 behaviour on the same scene/model-resolution, confirm the edge-
    threshold/sharpness sliders visibly affect the Vulkan image the same way they do on DX12 (0.3
    default reading close to Bilinear, same as this session's DX12 finding). -- verify: manual
    play-test + log inspection, screenshots if a difference needs confirming.
    **Not performed, disclosed rather than claimed.** No GPU/Vulkan game environment is available
    in this session to actually run the DLL and confirm engagement or visual parity in-game --
    genuinely unverified, needs the user (or a session with GPU access) to check.
12. [ ] `TASK_CLOSE`: record in `architecture_overview.md`/`known_gotchas.md` only if the VK/DX12
    shader-binding mirroring turned up something non-obvious; otherwise a short closing note is
    enough since this reproduces an already-validated design rather than exploring new territory.
    Held open pending step 11's in-game confirmation.

## Notes

This is a "port an already-shipped, already-validated design" plan, not new design work -- risk
is almost entirely mechanical (binding-slot mismatches, a missed lifecycle field, a forgotten
`Transition()` call) rather than architectural, unlike the original DX12 SGSR1 port (which needed
three separate in-game-driven corrections: the colorCopy-vs-modelInput proxy bug, the double-
dispatch descriptor-heap bug, and the Neutwo/Hybrid domain-mismatch blur). Lean on diffing against
the DX12 call sites at every step instead of re-deriving the design; if Vulkan in-game testing
surfaces a *new* divergence DX12 didn't have, that is real signal (Vulkan's descriptor/barrier
model differs enough from DX12's that a silent behavioral difference is plausible), not something
to explain away by assuming the port is wrong.
