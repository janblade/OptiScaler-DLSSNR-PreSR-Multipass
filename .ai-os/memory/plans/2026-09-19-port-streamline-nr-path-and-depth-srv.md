# Plan: Port the remaining Streamline and depth-SRV fixes from wilsjo2

- Branch: `fix/streamline-nr-path-and-depth-srv`
- Created: 2026-09-19
- Status: ported and built 2026-09-20 (2 commits on the branch, not pushed); untested in a game; awaiting push / PR
- Task file: memory/tasks/fix_streamline-nr-path-and-depth-srv.md
- Ledger: `wilsjo2-fork:streamline-plugin-binding`
- Source: wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass, GPL-3.0, refs `3fbffdc7` `7543d143` `b3618c05`. Final code read from `codex/release-v0.8.4`.

## Study

**The headline fix is already ours.** "Restore active Streamline plugin binding to fix BG3 startup" changes `Streamline_Proxy.h` by 52 lines. Our `Streamline_Proxy.h` already carries that change (it arrived with `42127e2d`, "...fix active Streamline binding"). Diffing our file against `release-v0.8.4` leaves only 6 lines. So the BG3 startup fix needs no port.

What the three commits contain, and what is left:

| Piece | Source | In our tree? |
|---|---|---|
| Resolve DLSSG, Reflex and PCL after device selection (plugin binding) | `3fbffdc7`, `7543d143` | **Yes.** Already in `Streamline_Proxy.h`. |
| Vulkan overlay teardown checks `Framebuffer` instead of `BackbufferView` | `7543d143` | **Yes.** [menu_overlay_vk.cpp](OptiScaler/menu/menu_overlay_vk.cpp) already checks `fd->Framebuffer`. |
| Add the `nvngx_dlssnr.dll` directory to Streamline's NGX search paths | `b3618c05` | **No.** Six lines, after the `DLSSFeaturePath` push in [Streamline_Proxy.h](OptiScaler/proxies/Streamline_Proxy.h). Reason from the commit: Streamline can initialize NGX before the upscaler does, and later initialization cannot repair the first search list. |
| Skip `TranslateTypelessFormats` when the SRV format is already `R32_FLOAT_X8X24_TYPELESS` | `7543d143` | **No, and the bug applies.** Our translator ([Shader_Dx12.cpp:60-61](OptiScaler/shaders/Shader_Dx12.cpp#L60)) maps that format to `D32_FLOAT_S8X24_UINT`, a depth-stencil format. Their commit says that removes the device when the SRV is created for NR's copied depth guide. |
| `tests/streamline_active_plugin_smoke.cpp` (124 lines) | `3fbffdc7` | No. Needs an installed `sl.interposer.dll` and a game bin directory, so it is not a plain WARP test. |

Our own NR code already builds that depth-plane format ([DlssNr_Dx12.cpp:1252](OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp#L1252)), which is consistent with their finding.

**Evidence.** Their notes cite a Release x64 build, the installed-runtime Streamline smoke test, and a separate Jedi depth-view diagnosis. That is their evidence on their tree; not reproduced here. No such crash has been reported against our build.

**Recommendation.** Port the two small edits. Skip everything else. Do not describe this as "the BG3 startup fix"; that part is already in.

## Steps

1. [x] **NR runtime search path.** In `Streamline_Proxy.h`, next to the other `nvngx_*` lookups (~line 359), add `Util::FindFilePath(exePath, "nvngx_dlssnr.dll")`. After the `DLSSFeaturePath` block, push its parent directory when found, with the same comment intent as theirs. No behaviour change when the file is absent.
2. [x] **Depth SRV format.** *(Callers checked: none passes the depth-plane format explicitly; `HC_Dx12` and `HCC_Dx12` pass swapchain formats and the rest pass no format, so only a resource whose own format is `R32_FLOAT_X8X24_TYPELESS` is affected, which is what NR's `TypedGuideFormat` produces.)* In `Shader_Dx12::CreateShaderResourceView` ([Shader_Dx12.cpp:198-201](OptiScaler/shaders/Shader_Dx12.cpp#L198)), keep `R32_FLOAT_X8X24_TYPELESS` as is instead of translating it. Leave the translator itself alone: other callers (menu, hudless compare, UI render) may rely on it, and the UAV call at :271 is separate. Check callers of `CreateShaderResourceView` for any that pass that format and expect a DSV format back.
3. [x] **Optional: the Streamline smoke test.** *(Skipped: no Streamline install at hand.)* Port it to `tests/` as a manual check with its documented build line. Only worth doing if a Streamline game install is at hand. Skip otherwise; do not add a test that cannot run here.
4. [x] **Provenance.** *(Authored as wilsjo2, trailers `@b3618c05` and `@7543d143`.)* Commit trailer `Ported-from: wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass@b3618c05, 7543d143`. The Vulkan guard in `7543d143` credits y4my4my4m in the source commit; we already have it, so no extra CREDITS entry is needed for this plan.

## Verification

- **No game needed:** Release x64 compile. Read-through of every caller of `CreateShaderResourceView` for step 2.
- **Needs a game:** a Streamline game with NR enabled and the NR runtime beside the exe for step 1; a game whose NR depth guide is `R32G8X24` for step 2 (their report is Jedi). Neither is verified here.
- The test in step 3, if ported, only checks startup and plugin identity, not presentation or frame generation.

## Risks

- Step 2 changes a shared shader helper. Scope it to the one format and keep the call-site check.
- Step 1 adds a directory to a Streamline search list. Low risk, but it is a change to what Streamline loads.

## Out of scope

Everything already present (plugin binding, Vulkan guard), and Streamline FG packaging.

## Rollback

Work on the branch above. Two independent small edits, each revertable alone.

## Execution log

- **2026-09-20:** cut `fix/streamline-nr-path-and-depth-srv` from `main` (`886200f9`). Both edits match what is in wilsjo2's `v0.8.4` (`8802b2b4`, 2026-09-15; the search-path line is carried there by his `6c52a033`, and `b3618c05` itself is not an ancestor of `codex/release-v0.8.4`, so the ledger's hash is a sibling of the released one; content identical). `v0.8.4` is not in our `main`. Two commits, authored as wilsjo2: `7df67eed` (b3618c05, search path) and `733c2db0` (7543d143, Shader_Dx12.cpp part only). Release x64 built, exit 0, 0 errors, no warnings in the touched files (`x64/Release/a/OptiScaler.dll` 00:04:15). No game run.
