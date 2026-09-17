# Plan: Add "SGSR1 (input only)" as a real Enlarge filter option
- Branch: feat/dlssnr-sgsr1-input-only
- Created: 2026-09-17
- Status: done
- Task file: memory/tasks/feat_dlssnr-sgsr1-input-only.md

## Context

Ships Panel 4 (from the reduced-resolution enlarge-path diagram session) as a real, selectable
`DlssNrReducedUpscaleMethod = 3`: SGSR1 sharpens the proxy (`colorSmall`), the answer stays on the
cheap implicit bilinear tap. Value 2 ("both") keeps its current meaning -- no `.ini` compatibility
break for existing users.

**Why this needs the gate fix, not just the new option:** the residual gate infers "did the model
run small" from `gSource`'s bound dimensions. Once SGSR1 enlarges the proxy, `gSource` reads native
regardless of what the answer does -- the same silent skip that hits method 2 today would hit this
new method identically. Shipping the option without the fix means it arrives already broken, and
defeats its own point: the appeal of enlarging only the proxy (real game content, no model-noise
false-edge-vote risk, per `sgsr1.hlsl`'s own documented skin/hair finding) is paired with matched
residual's `fullProxy` correction. Without a working gate, it's just method 2 with a cheaper answer
-- no distinct benefit over what already ships.

Root cause of the gate bug, for reference: `modelRanSmall` (`dlssnr.hlsl:1025-1027`) was written
2026-09-01 (`40bc62bf`), before SGSR1 enlarge existed. It correctly inferred "ran small" from the
bound proxy's own size because nothing sat between the model and the resolve at the time. The SGSR1
enlarge pass was inserted 2026-09-16 (`5970da66`), which answers that same size question on the
proxy's behalf once bound -- no commit ever revisited the gate's assumption. The correct signal,
`gModelWorkScale` (computed in C++ before SGSR1 runs), was added the same day for the neighbouring
Replace-mode detail-injection feature (`88ed7f5d`) and never back-ported here.

**Out of scope:** the false-edge-vote issue on skin/hair itself (separate, larger investigation
candidate for a previously shelved "skin light wave" report -- not blocking this plan, and this
plan doesn't make that risk worse for the answer since it stays bilinear here); reworking the
encoding scheme into independent checkboxes instead of a linear combo; Replace-mode behavior
(unaffected -- Replace bypasses the whole proxy/residual mechanism regardless of enlarge method).

## Steps

1. [x] **Fix the shared shader gate.** In `dlssnr.hlsl`, delete the `proxyW`/`proxyH`/
   `modelRanSmall` block (1025-1027) and change the gate at 1029 to
   `if (gTransfer == 1 && gModelWorkScale < 0.999)`, matching detail injection's existing idiom at
   1202. This is one shared HLSL source compiled to both the D3D12 and Vulkan blobs -- no
   per-backend divergence.
   -- verify: grep confirms no remaining consumer of `modelRanSmall`; gate is true in every case
   enumerated in the six-cell table (methods 0/1 matched-residual cells) plus the new method 3, and
   false for method 2/3-classic and 100%-or-above.

2. [x] **Regenerate both shader blobs and headers** (`fxc.exe -T cs_5_0 -E CSMain -O3` for DXBC,
   `dxc.exe -spirv -T cs_6_0 -E CSMain -O3 -Qstrip_debug -D VK_MODE -Cc -Vi` for SPIR-V,
   `create_header.py` with array names `DlssNr_cso` / `dlssnr_spv`).
   -- verify: `git diff --stat` touches only `dlssnr.hlsl` plus the four generated blob/header
   files.

3. [x] **Wire method 3 into D3D12.** In `DlssNr_Dx12.cpp`: change `wantsSgsr1Answer` at 2954 from
   `upscaleMethod >= 1` to `upscaleMethod == 1 || upscaleMethod == 2` (must NOT include 3 -- method
   3's whole point is a cheap answer), and `wantsSgsr1Proxy` at 2955 from `upscaleMethod == 2` to
   `upscaleMethod == 2 || upscaleMethod == 3`. Update the allocation gate at 2011 the same way
   (`== 2` -> `== 2 || == 3`). Update the stale comments at 274, 2008, 2941 to describe four values,
   not three.
   -- verify: read the new boolean expressions against all four values (0/1/2/3), not just the
   changed ones -- confirm method 3 yields (answer=false, proxy=true) and methods 0/1/2 are
   unchanged from today.

4. [x] **Mirror step 3 in Vulkan.** Same two boolean changes in `DlssNrFeature_Vk.cpp`
   (1296-1297), same allocation-gate change (909), same comment updates (108, 899, 1288).
   -- verify: the two files' boolean expressions are identical in structure; grep confirms no
   third derivation site was missed.

5. [x] **Add the menu entry.** In `DlssNr_Menu.cpp`: append `"SGSR1 (input only)"` to
   `upscaleMethodNames` (513), change the clamp at 515 from `2u` to `3u`, update the HelpMarker
   (523) to describe four options -- including, briefly, why input-only exists (sharpens the real
   frame; the answer is left alone because it's where the model's own per-frame noise lives, which
   is the thing SGSR1's edge-vote can mistake for a real edge). Leave `sgsr1Active` (525,
   `upscaleMethod != 0`) and Optimized Defaults' explicit `= 2u` (167) untouched -- both already
   correct for the new value.
   -- verify: combo shows 4 entries in the right order; `.ini` round-trips a hand-set
   `ReducedUpscaleMethod=3` correctly (`Config.cpp`'s read/write at 369/1320 are generic `uint` --
   no change needed, but confirm by reading, not assuming).

6. [x] **Update `Config.h`'s field comment** (410-419) to document value 3 alongside 0/1/2.

7. [x] **Build both configurations,** Debug|x64 and Release|x64, 0 errors, no new warnings.
   -- done: both configs built via MSBuild directly. Debug: 0 errors, 0 occurrences of "error" in
   the full log, no warnings attributed to any of the 5 touched files. Release: 0 errors, same
   clean result across all 8 DLSS-NR-area files rebuilt (DlssNr_Dx12.cpp, DlssNr_Vk.cpp,
   DlssNrFeature_Vk.cpp, DlssNr_ExposureScan.cpp, DlssNr_Proxy.cpp, DlssNr_Menu.cpp,
   NVNGX_DLSS_Dx12.cpp, dllmain.cpp -- all recompiled by the header change, none warned). Both
   `OptiScaler.dll` and the `dlssnr_forwarder` DLL linked successfully in both configs.

8. [x] **In-game confirmation (needs the user).** At model resolution < 100%, a Composed HDR
   mapping, method 3: confirm via the existing SGSR1 up-leg log line that answer reads "bilinear"
   and proxy reads "engaged" (the inverse of today's method-1 log pattern) -- and separately, via a
   Classic/Matched-residual screenshot-diff, confirm matched residual is now actually live at this
   new setting (non-zero delta = live, bit-identical = still not engaging).
   -- verify: log check and A/B result. No automated shader-output harness exists for this file, so
   this is disclosed as the one step this session can't exercise itself.
   -- done: user confirmed OK in-game.

## Review Pass (core.dev-loop.sk)

Independent reviewer subagent dispatched (Path A -- this host supports subagent dispatch) with
the diff and this plan's Context as the spec, not implementation reasoning. Verdict: mergeable, no
correctness bugs -- independently confirmed `gModelWorkScale` set identically in both backends,
boolean logic correct for all 4 method values in both D3D12 and Vulkan, exhaustive grep found no
missed `ReducedUpscaleMethod` consumer, `.ini` round-trips, methods 0/1/2 behaviorally unchanged,
shader blobs genuinely regenerated (mtime check). One non-blocking finding: an out-of-range
hand-edited `.ini` value (4+) now falls through to Bilinear instead of the old `>= 1` comparison's
"answer only" fallback -- a real but obscure behavior change for malformed config, outside this
diff's stated compatibility promise (which only covers value 2's meaning). Left as a disclosed,
accepted gap rather than fixed -- not part of this diff's scope, and the menu-vs-dispatch display
mismatch for out-of-range values already pre-existed for value 3 before this change.
