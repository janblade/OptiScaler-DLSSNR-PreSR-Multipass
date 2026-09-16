# Task Memory — feat/dlssnr-sgsr1-vulkan

Branched off `main` (`aa4b17a5`, the commit `v0.1.1-upscale-method-toggle` was released from),
2026-09-16.

Active plan: memory/plans/2026-09-16-dlssnr-sgsr1-vulkan.md (in-progress, 10/12)

## Context

User asked to plan (not yet implement) "the vulkan part from PR #8" -- PR #8
(`feat/dlssnr-replace-detail-injection`, merged) shipped DLSS-NR's SGSR1 reduced-resolution
enlarge pass as DX12-only by deliberate scope cut (`.ai-os/memory/plans/2026-09-16-dlssnr-sgsr1-
upscale.md`'s own "Out of scope: Vulkan"). PR #9 (`feat/dlssnr-upscale-method-toggle`, merged,
released as `v0.1.1-upscale-method-toggle`) built a 3-way enlarge-method toggle and live-tunable
edge threshold/sharpness on top of that pass, also DX12-only.

Investigated before planning (not guessed): confirmed via `git show --stat` on both PR #8 commits
that the SGSR1 pass itself (`5970da66`) touched zero Vulkan files, while the Replace-mode detail-
injection commit (`88ed7f5d`) *did* touch `DlssNrFeature_Vk.cpp` (+2 lines, the `ModelWorkScale`
gate) -- so Vulkan already has correct detail-injection parity, confirmed by that PR's own test-
plan note. Read `OS_Vk.h/.cpp` (the existing Vulkan analog for the `workScale > 1.0` supersample
leg's `superUp`/`superDown`) as the structural template for the new `SGSR1_Vk` class, and
`rcas.hlsl`'s `#ifdef VK_MODE` / `[[vk::binding]]` pattern as the template for `sgsr1.hlsl`'s
missing VK variant. Confirmed `DlssNrFeature_Vk.cpp`'s reduced leg (~line 1073-1142, 1263-1279)
currently has no SGSR1 branch at all -- `modelInput`/`resolveAnswer`/`resolveProxy` only ever
resolve to the implicit-bilinear buffers for `workScale < 1.0`.

Scope question asked via `AskUserQuestion` (user picked the recommended option): port the
*current* DX12 state (PR #8's base pass plus PR #9's toggle/tunables/0.3-default), not PR #8's
original snapshot alone -- so Vulkan doesn't ship immediately behind DX12's already-released
behaviour, and this doesn't need a second follow-on plan later just to catch Vulkan up again.

## Notes for whoever executes this plan

This is scoped as a mechanical port of an already-shipped, already-tested design, not new design
work -- see the plan's own "Notes" section. Diff against the DX12 call sites
(`DlssNr_Dx12.cpp:2941-3012` for the up-leg dispatch, `:3032-3033` for the resolve-side
`resolveProxy`/`resolveAnswer` ternaries) at every step rather than re-deriving anything. If
Vulkan in-game testing surfaces behavior DX12 never had, treat that as real signal specific to
Vulkan's descriptor/barrier model, not evidence the port itself is wrong.

## Execution progress (2026-09-16)

Steps 1-10 done in one session, no in-game environment available to run steps 11-12. Summary:

- `sgsr1.hlsl` gained `#ifdef VK_MODE` bindings (UBO 0 / sampled image 1 / storage image 2 /
  sampler 3); DX12 recompile confirmed byte-identical `.cso` before touching anything else, so
  the VK-only additions are provably inert for DX12. VK variant compiled to `.spv`/`.h` via the
  existing `build_precompiled_shader_vk.bat`.
- New `SGSR1_Vk` (`shaders/sgsr1/SGSR1_Vk.h/.cpp`), modeled directly on `OS_Vk.h/.cpp` (not
  `RCAS_Vk`, which carries depth-adaptive complexity SGSR1 doesn't need) -- same 4-binding shape,
  same `Sgsr1Constants` layout as `SGSR1_Dx12.cpp`'s own struct. Added to both `.vcxproj` and
  `.vcxproj.filters` (new source files aren't auto-picked-up, the same gotcha the original DX12
  port hit).
- `DlssNrFeature_Vk.cpp`: `g_vk` gained `proxyNative`/`sgsr1UpAnswer`/`sgsr1UpProxy`;
  `outputNative`'s creation gate widened from `workScale > 1.0f`-only to `workScale != 1.0f`;
  `proxyNative` gated on `reduced && workScale < 1.0f && method == 2`, matching DX12 exactly.
- **One real placement correction found during execution, not just transcription**: the plan's
  draft step 7 said to add the up-leg dispatch at the `modelInput` computation site (pre-model-
  eval). That's wrong -- the answer-side enlarge needs the model's actual output, which doesn't
  exist until after the NGX evaluate loop. Re-checked DX12's own dispatch ordering and confirmed
  its up-leg block also runs post-eval, in the resolve-preparation code, not at its `modelInput`
  site. Implemented in the resolve section instead, matching DX12's real ordering rather than the
  plan's draft one. `resolveProxy`/`resolveAnswer` selection implemented as two `if` statements
  instead of DX12's combined ternary (equivalent given `workScale`'s sign makes the two legs
  mutually exclusive) so the existing supersample down-leg code needed no restructuring.
- Lifecycle: both `sgsr1UpAnswer`/`sgsr1UpProxy`/`proxyNative` added to both Vulkan teardown paths
  (the device-lost `.release()` abandon-path and the normal `.reset()`/`DestroyImage()` path).
  Confirmed by code inspection (not assumed) that no live-toggle drain is needed for the enlarge-
  method setting: unlike `OS_Vk`'s scaler switch (loads a *different* SPIR-V pipeline per filter,
  genuinely needs the drain-and-rebuild dance), `SGSR1_Vk` always loads one fixed shader --
  toggling the method only changes whether `Dispatch()` is called, matching DX12's own identical
  no-drain precedent for `SGSR1_Dx12`.
- Debug + Release x64 both built clean via local MSBuild, exit 0, 0 errors both; warning counts
  (64 Release / 63 Debug) match this session's pre-existing baseline, zero warnings from any of
  the touched/new files specifically (grepped explicitly, not just eyeballed the totals).

Left open: steps 11 (in-game check) and 12 (`TASK_CLOSE`) -- no GPU/Vulkan game environment
available in this session, disclosed rather than claimed. Next step is the user running a Vulkan
title, cycling `DlssNrReducedUpscaleMethod` (0/1/2), and confirming the log lines
(`DLSS-NR Vulkan SGSR1 up-leg: ...` / `DLSS-NR Vulkan reduced up-leg: Bilinear selected...`) show
correct engagement per method, and that the visual result matches DX12's already-shipped
behaviour on the same scene/model-resolution.
