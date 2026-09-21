# Plan: Port Restore Sharpness (Replace-mode native detail injection) onto wilsjo2 v0.8.4

- Branch: `feat/nr-restore-sharpness-on-v0.8.4`, cut from `wilsjo2/codex/release-v0.8.4` (`8802b2b4`, tag v0.8.4) in a separate git worktree
- Created: 2026-09-20
- Status: done          # PR opened 2026-09-20: wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass#81, base codex/release-v0.8.4, commit 4719b3d6
- Task file: memory/tasks/main.md (working notes stay on main; the worktree branch carries no `.ai-os`)
- Type: flat plan, 12 steps. Outbound PR to wilsjo2, not an inbound port, so `core.port.sk`'s ledger is not used.

## Context

The user asked to port "Restore Strength" to wilsjo 0.8.4. **No control by that name exists in this tree or in any wilsjo2 ref** (searched `Restore ?Strength`, `RestoreStrength`, and `DlssNr*Restore*` on `main`, `codex/release-v0.8.4`, `wilsjo2/main`, `per-pass-strengths`). The closest match is the **"Restore Sharpness"** slider, config `[DlssNr] ReplaceDetailStrength` (`DlssNrReplaceDetailStrength`, default 0.5, range 0..2). It shipped as "Replace detail strength" in `88ed7f5d` and was renamed in `3f17ec45`. This plan assumes that is the feature. Confirm before execution.

What it does: with Final Image Composition on a Replace mode (`ReversibleMode` 2 or 4) and NR running below 100% model resolution, the model's answer is soft and Replace has no native-resolution fallback. The resolve shader pulls native high-frequency luminance back in, multiplicatively and hue-preserving, using the file's dual-floor `kRatioFloor` idiom so shadows cannot crush to black. Confirmed in-game on our line (Hybrid Replace at 80%: less blur, no ghosting; 100% unchanged). It has **not** been run on v0.8.4, and nothing below claims it works there.

**v0.8.4 lacks this feature.** `git grep ReplaceDetail` on `codex/release-v0.8.4` finds nothing. Its Replace decode (`dlssnr.hlsl` ~1070) is followed directly by `result *= normScale`.

**Acceptance criteria** (stated as checks, none ticked):
- [ ] At strength 0, and at model resolution >= 100%, output is bit-identical to unmodified v0.8.4 for Replace and for every Composed mode (the injection is gated off).
- [ ] Below 100% model resolution in a Replace mode, the slider changes the image on the D3D12 path; ini key `ReplaceDetailStrength` round-trips.
- [ ] Composed modes (0/1/3) cannot reach the injection code.
- [ ] `tests/nr_skin_shader_smoke.cpp` still passes, and a new WARP case shows the injection off at 0 and on when `ModelWorkScale < 1`.
- [ ] Release x64 builds clean with no new warnings in touched files.

**Out of scope:** the Pre-SR/Post-SR tier presets and their `restoreSharpness` values (`DlssNr_Menu.cpp` ~351-405; v0.8.4 has no tier presets); SGSR1 (v0.8.4 has no `DlssNrUpscaleMethod`); `ApplyReplaceGuard` (see decision 2); our `DlssNr_Menu.cpp` layout (v0.8.4 split the menu into `DlssNr_Menu*.cpp`).

## Study: what differs on v0.8.4 (verified this session, R21)

| Our code | v0.8.4 | Consequence |
|---|---|---|
| One `DlssNr_Menu.cpp`, slider at ~866 | Menu split into `DlssNr_MenuInput.cpp` (has the "HDR mapping (experimental)" combo, `reversible` at ~66-72), `DlssNr_MenuBlend.cpp` (Detail/Colour strength sliders) | Slider goes in `DlssNr_MenuInput.cpp` right after the HDR mapping combo's help text, shown only for `reversible == 2 || 4`. Reuse that file's `HelpMarker` and Reset-button idiom. |
| Menu label "Final Image Composition" | Label "HDR mapping (experimental)" | Slider help text must not name the other label. |
| `DlssNr_Dx12.cpp` `resolveParams` ~3127 | Resolve constants set in `DlssNr_Dx12_Encode.cpp` ~279-304, plus a config-to-constants builder in `DlssNr_Common.h` ~262-286 | Wire the value in `Encode.cpp` next to `ColourStrength` (~293). Also add it to the `Common.h` builder if the Vulkan path uses it (check `DlssNrFeature_Vk.cpp` ~347, ~380, ~480: `resolve = encode`). |
| `ModelWorkScale` computed as `reduced && workScale < 1.0f ? workScale : 1.0f` | `Encode.cpp` has `reduced` (~21) and `context.workScale`, but no such constant. Also has `workScale > 1` supersample. | Add `ModelWorkScale` with the same expression, so supersample (`> 1`) does not trigger it. |
| Shader gate reads `gModelWorkScale` | Shader infers "model ran small" from `gSource` dims (`modelRanSmall`, ~915) | **Do not reuse `modelRanSmall`.** v0.8.4 also has `EnlargeMatchedResidual` (`DlssNr_Dx12_Enlarge.cpp`), which can hand the resolve a native-sized buffer, so a shader-side size check is stale in that case: the same failure `known_gotchas.md` records twice. Use the explicit C++ value. |
| cbuffer: `gReplaceDetailStrength`, `gModelWorkScale` follow `gEnvironmentColour` | v0.8.4's `dlssnr.hlsl` cbuffer ends at `gEnvironmentColour`, but the C++ struct continues with `ResidualBlend`, `ResidualHistoryValid`, `ResidualMotionBaseX/Y` (Common.h ~239-242), which `dlssnr.hlsl` never declares. | Appending two fields in the shader right after `gEnvironmentColour` would land on the residual fields. Append the new C++ fields **after** `ResidualMotionBaseY`, and in `dlssnr.hlsl` declare four unused placeholder fields for the residual block first, then the new two. `dlssnr_residual.hlsl` is untouched. The existing `known_gotchas.md` entry says v0.8.4's struct offsets are not ours. |
| `static_assert(sizeof == 256)` with padding to spare | Same assert; 4+2 new dwords must still fit inside the 256-byte pad | Confirm by building, not by counting. `nr_skin_shader_smoke.cpp` also asserts `offsetof(..., EnvironmentColour) == 112`; new fields go after it so that stays true. |
| Shader locals `original`, `originalLuma`, `normScale`, `kLuma` | Present (`dlssnr.hlsl` ~834-837, ~286). `kRatioFloor` is a **local `const` at ~1012**, inside the composition, not a file-scope constant as in our tree. | Declare a local `const float kRatioFloor` in the injection block, or hoist it. Do not depend on scope from ~1012 without checking the block is in the same function scope. |
| Replace decode then `ApplyReplaceGuard` then `result *= normScale` | No guard. Decode goes straight to `result *= normScale`. | Insert the injection between the decode (~1073) and `result *= normScale`. No guard follows it here; see decision 2. |
| `DlssNr_Shader.cso/.h`, `_Vk.spv/.h` generated from our HLSL | v0.8.4 tracks its own binaries, compiled from its HLSL | **Never copy binaries across branches** (`known_gotchas.md`). Recompile from the edited v0.8.4 `dlssnr.hlsl` with `dxc` (DX12 `-T cs_6_0 -E CSMain -O3 -Qstrip_debug -Qstrip_reflect`; Vulkan `-spirv ... -D VK_MODE -Cc -Vi`; array names `DlssNr_cso` and `dlssnr_spv`). |
| Mode numbers | v0.8.4 has `ClampProxy = 8`, `ResizePrivateGuides = 10`; ours differ | Not touched here, since the injection adds no mode. Do not paste any code that names a mode by number. |

**Already have on v0.8.4?** No (grep above). **Competing implementation?** None found.

**Licence.** Our own code, GPL-3.0, into a GPL-3.0 fork. No third-party material in the diff.

**Evidence it works.** In-game on our line only (user confirmed 2026-09-16). Nothing on v0.8.4. A second Review Pass on our line found and fixed an unbounded ratio; that fix is part of what is ported.

**Risk.** Shader constants layout is the sharp edge: a wrong offset shifts every field after it silently. Vulkan gets a build-and-shader-test check only; no Vulkan game run is claimed.

## Decisions (no answer given; the recommendations were taken when the user said "plan execute")

1. **Name.** Is the feature "Restore Sharpness" (`ReplaceDetailStrength`)? If "Restore Strength" is something else, name it and this plan changes.
2. **Scope of the shader change.** Recommendation: **injection only.** v0.8.4's Replace has no highlight guard at all, and our tree added one separately (`a8dcd2d3`, `4016dda5`) to fix a vertical-line artifact under Apply-before-SR at reduced model resolution. That artifact probably exists on v0.8.4 too, but that is a different fix with its own evidence, and bundling it makes the PR harder to review. Alternative: port the guard in the same PR, or as a second PR.
3. **Slider name in his UI.** Keep "Restore Sharpness" (recommended, matches ours) or match his naming style ("Detail strength" already exists there and would be confusing beside it).

## Steps

1. [x] **Worktree and branch.** From the main checkout run `git worktree add ..\OptiScaler-NR-v084 -b feat/nr-restore-sharpness-on-v0.8.4 wilsjo2/codex/release-v0.8.4`. A plain `git checkout` in place would delete our tracked `.ai-os` (101 files) from the working tree. `git status` clean first (R20); initialise submodules the build needs. — verify: `git log -1` is `8802b2b4`; `git ls-files .ai-os` is empty in the worktree; `git worktree list` shows both.
2. [x] **Baseline.** Before any edit, build Release x64 in the worktree and run `tests/nr_skin_shader_smoke.cpp` (its own runner needs the DXC/WARP path; `vswhere` is absent on this machine, see the hang-triage and MFG notes for the stand-in). — verify: build succeeds; smoke prints its PASS lines. This fixes what "no new warnings" is measured against.
3. [x] **Config.** `Config.h` next to `DlssNrTransferStrength` (~324): `CustomOptional<float> DlssNrReplaceDetailStrength { 0.5f };`. `Config.cpp`: load next to `ColourStrength` (~334, `readFloat("DlssNr", "ReplaceDetailStrength")`) and save next to ~1276. `OptiScaler.ini` comment if that file documents the other NR keys. — verify: build; ini round-trip (write, reload, same value; missing key gives 0.5).
4. [x] **Constants struct.** `DlssNr_Common.h`: add `float ReplaceDetailStrength; float ModelWorkScale;` after `ResidualMotionBaseY` (so `EnvironmentColour` stays at offset 112 and the residual block does not move), with comments naming why they trail. Initialise `ModelWorkScale = 1.0f` in the builder (~262-286) so any path that never sets it reads "not reduced". — verify: `static_assert(sizeof(DlssNrConstants) == 256)` still holds; `nr_skin_shader_smoke.cpp` static asserts hold.
5. [x] **Shader cbuffer.** `dlssnr.hlsl`: after `gEnvironmentColour`, declare four unused fields matching `ResidualBlend`, `ResidualHistoryValid`, `ResidualMotionBaseX`, `ResidualMotionBaseY` (same types and order as Common.h ~239-242), then `gReplaceDetailStrength` and `gModelWorkScale`. — verify: add a compile-time offset check on the C++ side for both new fields against the expected byte offsets; WARP test in step 9 exercises the real offsets.
6. [x] **Shader injection.** `dlssnr.hlsl`, between the Replace decode (~1070-1073) and `result *= normScale`: port our block with the dual-floor ratio, gated on `(gReversibleMode == 2 || gReversibleMode == 4) && gModelWorkScale < 0.999 && gReplaceDetailStrength > 0.0`, radius `clamp(round(1/gModelWorkScale), 1, 4)`, taps via `gOriginal.Load` divided by `normScale`. Declare `kRatioFloor` locally (it is not file-scope here). Adapt the comment to v0.8.4's wording ("HDR mapping"), not paste ours. — verify: compiles under both `dxc` and the WARP `D3DCompileFromFile` `cs_5_0` path the smoke test uses.
7. [x] **DX12 wiring.** `DlssNr_Dx12_Encode.cpp`: set `resolveParams.ReplaceDetailStrength = cfg.DlssNrReplaceDetailStrength.value_or_default()` beside `ColourStrength` (~293) and `resolveParams.ModelWorkScale = (reduced && workScale < 1.0f) ? workScale : 1.0f` from the values already in scope (~19-23). Check the other resolve sites that build `DlssNrConstants` (grep `DlssNrConstants` under `shaders/dlssnr/`: finished-picture compose, deferred SR, late) and decide per site whether it runs the Replace resolve; wire only those, and list the ones left at the 1.0 default with the reason. — verify: build; log or debug-print once that the constants reach the dispatch when a Replace mode is on and `WorkingScale < 1`.
8. [x] **Vulkan wiring.** `DlssNrFeature_Vk.cpp`: the encode constants (~347, ~380, ~480, where `resolve = encode`) get the same two values. — verify: build; the Vulkan shader smoke (`tests/nr_vulkan_shader_smoke.cpp`) still passes against the regenerated SPIR-V. **Not verified in a Vulkan game**; the PR says so.
9. [x] **Regenerate shader binaries and add the WARP test.** Recompile `dlssnr.hlsl` to `DlssNr_Shader.cso/.h` (DX12) and `DlssNr_Shader_Vk.spv/.h` (Vulkan) with `dxc`, using the flags and array names above; do not touch `dlssnr_residual*` or `dlssnr_finished_color*`. Extend `tests/nr_skin_shader_smoke.cpp` (or add a sibling) with a small texture case in Replace mode (`ReversibleMode = 2`): strength 0 leaves output equal to the model answer; `ModelWorkScale = 1` with strength > 0 does the same; `ModelWorkScale = 0.5` with strength > 0 changes luminance toward native detail while keeping the pixel's hue ratio (r/g, b/g) and staying finite. Use a texture wide enough for the radius taps. — verify: the new cases fail on the unmodified shader and pass on the edited one (run once against the step-2 baseline to show they can fail); the older PASS lines still print.
10. [x] **Menu.** `DlssNr_MenuInput.cpp`, after the HDR mapping combo's `HelpMarker` (~72): when `reversible == 2 || reversible == 4`, `SliderFloat("Restore Sharpness", ..., 0.0f, 2.0f, "%.2f")`, a `Reset##replacedetail` button back to 0.5, and a HelpMarker that describes the effect and says it has no effect at 100% model resolution or above, or at 0. Use v0.8.4's own label for the other control ("HDR mapping"). Check the file's existing indent and Reset idiom rather than copying ours. — verify: build; slider appears only for the two Replace choices; reset restores 0.5.
11. [x] **Build and self-review.** Release x64 and Debug x64 (if the baseline built both) in the worktree; run all `tests/nr_*` smoke tests that were green at step 2. Run `core.dev-loop.sk`'s Review Pass over `git diff wilsjo2/codex/release-v0.8.4` with this plan's acceptance criteria as the spec. — verify: 0 new warnings in touched files, all baseline-green tests still green, review findings surfaced to the user.
12. [x] **Hand-off (needs the user's go-ahead, R15).** Show the diff stat (expect about 8-10 files, no `.ai-os`, no binaries other than the two regenerated pairs). Push only the branch to `origin`; opening a PR against `wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass` base `codex/release-v0.8.4` is the user's call, as with PRs #79 and #80. PR body states: ported from `janblade/OptiScaler-DLSSNR-PreSR-Multipass` `88ed7f5d` and its follow-up review fix; confirmed in-game on that line only; **not run on v0.8.4**; Vulkan not run in a game; what it deliberately leaves out (tier presets, guard). Check `gh` compare (`ahead/behind/files`) before proposing it. — verify: the compare shows only this change.

## Rollback

Delete the worktree and branch: `git worktree remove ..\OptiScaler-NR-v084` then `git branch -D feat/nr-restore-sharpness-on-v0.8.4` (confirm first, R20). Nothing on `main` changes.

## Execution notes (2026-09-20)

Steps 1-11 done in worktree `D:\DEV\OptiScaler-NR\OptiScaler-NR-v084`, branch `feat/nr-restore-sharpness-on-v0.8.4`, **uncommitted**. Step 12 (commit, push, PR) waits for the user.

**Corrections to the plan found while executing:**
- **DX12 blob is fxc, not dxc.** v0.8.4's committed `DlssNr_Shader.cso` is DXBC (`SHEX`, no DXIL) and its own smoke test loads it on D3D11 WARP, so it must be `cs_5_0`. Recompiling the unmodified v0.8.4 HLSL with D3DCompiler_47 (`cs_5_0`, `D3DCOMPILE_OPTIMIZATION_LEVEL3`, no strip) reproduces the committed blob byte for byte. No `fxc.exe` on this machine; a 40-line `D3DCompileFromFile` wrapper did it. The Vulkan blob reproduces byte for byte with `dxc -T cs_6_0 -E CSMain -O3 -spirv -D VK_MODE -Cc -Vi` (no `-fspv-target-env`). The `known_gotchas.md` entry that says DX12 uses `dxc cs_6_0` describes this fork's tree, not v0.8.4's.
- **Wiring was simpler:** one DX12 site (`MakeResolveConstants`, only caller `DlssNr_Dx12_Run.cpp:404`) and one Vulkan site (`resolve = encode`, `DlssNrFeature_Vk.cpp:480`). No other dispatch uses `DlssNrMode_Resolve`. `kRatioFloor` turned out to be a function-scope local in `CSMain` and is reused.
- **Bug found in the source version, fixed in the port:** taps read with `Load(id.xy + offset)` return black outside the frame, so a ring of pixels within `radius` of the frame border was brightened, and side-by-side Compare read a different place from `original`. A failing test proved the border case on the `Load` version; the port samples through `cmpUv` with the clamping sampler. **Our own tree still has the `Load` version** (`dlssnr.hlsl` ~1470) and has this bug.
- Header generator was checked before use: regenerating the committed `.h` files from the committed blobs is byte-identical to the committed headers.

**Verification actually run** (no game, no hardware beyond WARP and one Vulkan device):
- Release x64 builds, 0 errors, warning set identical to the unmodified-v0.8.4 baseline (64 warning lines both, full builds), none in touched files.
- `tests/nr_replace_detail_smoke.cpp` (new): passes against the edited HLSL and against the regenerated production blob; fails against the unmodified v0.8.4 HLSL; border case failed on the `Load` version before the fix.
- `tests/nr_skin_shader_smoke.cpp`: all 5 PASS lines as at baseline.
- `tests/nr_vulkan_shader_smoke.cpp` on the RTX 5070 Ti: both PASS lines. That test exercises the existing encode/resolve, **not** the injection on Vulkan.

**Not verified:** in-game on v0.8.4 (either API); the C++ wiring of `ModelWorkScale` and the strength value (read, not run); ini round-trip (same pattern as `ColourStrength`, not run); the menu slider (compiled, not seen); side-by-side Compare view (by reading only); Vulkan injection.

**Review Pass:** self-review fallback (no reviewer subagent was dispatched; none was requested). No blocking findings. Open points for the user: (1) default 0.5 changes the picture for existing Replace users below 100% on upgrade, whereas wilsjo's NR experiments ship default-off; (2) `ModelWorkScale`'s expression is written out once in DX12 and once in Vulkan, matching how each file already handles `workScale`, not factored into a shared helper; (3) C++ wiring has no automated test.

## Closing (2026-09-20)

User verified the test build in-game ("verified OK"; game and API not stated) and asked for the PR. Committed `4719b3d6` on `feat/nr-restore-sharpness-on-v0.8.4` (1 commit, 13 files, compare clean against `codex/release-v0.8.4` `8802b2b4`), pushed to `origin` with an explicit refspec (the worktree branch had been tracking wilsjo2's release branch), PR **wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass#81**. Default 0.5 kept and flagged in the PR body as a reviewer choice. Vulkan injection and Compare-view fix are stated in the PR as not verified in a game. The DLL tested was built from the same content as the commit but before it existed. Worktree `..\OptiScaler-NR-v084` is still there; remove it once the PR settles. Our own `main` still has the `Load`-tap border bug in `dlssnr.hlsl` (~1470); fixing it was offered, not done.
