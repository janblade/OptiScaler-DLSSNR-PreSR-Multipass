# Task Memory — main (rolling)

Protected-branch working notes. Drained by `MEMORY_CONSOLIDATE`, not `TASK_CLOSE`.

## 2026-09-06 — repo switch + DLSS-NR/pre-SR-multipass review
- Repointed this checkout's `origin` to `wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass.git`,
  checked out `main` (tracks `origin/main`). Old `dlss-neural-rendering` branch/task file
  preserved locally, no longer active.
- Built Debug|x64 and Release|x64 — both succeed (0 errors). Release post-build packages
  to `x64/Release/a/`.
- Ran `/code-review` on the DLSS-NR + pre-SR multipass feature (commits `926cee08`,
  `facc24f6`). 5 findings: 2 correctness (pre-SR HDR transform reads Output's format
  instead of Color's in `DlssNr_Dx12.cpp`; "Model passes" slider has no dead-control
  indication when proxy backend/scratch-alloc silently caps effective passes to 1), 3
  reuse/simplification (duplicated RR/DLSSD exclusion check across 3 call sites;
  duplicated pass-reset block in 2 places; hand-repeated transition/restore pattern
  instead of RAII, 5 call sites).
- Active plan: memory/plans/2026-09-06-fix-dlssnr-multipass-review-findings.md (done, 6/6)
- All 6 steps executed and verified by build (Debug|x64 + Release|x64, 0 errors each).
  Step 5 shipped 4 of the 5 originally-named RAII sites (the 5th, "encode", doesn't
  match the save/restore shape -- disclosed in the plan file, not forced). Step 2's
  new menu indicator is build-verified only, not exercised in a live game -- flagged
  for an in-game smoke test before release.
- Ran `DEV_IMPLEMENT_REVIEWED` retroactively (`core.dev-loop.sk`), Path A: this host
  has genuine subagent-dispatch, so a second independent reviewer got only the diff +
  original findings, not this session's own reasoning. It caught 2 real bugs the
  initial pass missed, both fixed and re-verified by build: `LastPassCapStatus()` was
  reading `g_nr` without the `g_nrMutex` lock every other entry point takes (race vs.
  `Dispatch()`'s per-frame writes); the menu's pass-cap message had no case for a
  *permanent* pass-creation failure (`passCreateFailed`), so it misleadingly said
  "still building" forever in that case. See plan step 6 for full detail.
- Pushed to `origin` (repoint: `origin` is now `janblade/OptiScaler-DLSSNR-PreSR-Multipass`,
  a fork of `wilsjo2`'s repo, forked via `gh repo fork` since `janblade` had no push
  access to `wilsjo2`'s repo directly). Rebased our 3 local commits onto
  `origin/main`, which had 2 unrelated commits already on it ("native RR controls",
  "per-pass strengths") touching the same DLSS-NR files -- 2 real conflicts in
  `DlssNr_Dx12.cpp` (both were "our SetExtras signature change" vs "their new
  PassTuning line" -- resolved by keeping both), re-verified by a clean Debug|x64
  build after resolving, then pushed. `1366c6fa..1b773ae1 main -> main`.
- Applied EP-1 (`registry/core.planning.sk/SKILL.md`): `PLAN_EXECUTE` now has a
  mandatory independent-review gate (step 6, `core.dev-loop.sk`'s review half) that
  must run once per plan before `Status: done` is set. Closes the gap that let the
  DLSS-NR fix plan above complete without independent review the first time. Not yet
  committed to git — this is an installed project, so commit/push follows normal git
  workflow, not the framework's own `RELEASE` command (project-only, MaiKS's dev repo).
- Ran `/code-review` on DLSS-NR's forwarder-DLL usage (`EnsureForwarder`, the
  function-pointer table, every call site). 5 findings, all verified against the code
  (one also cross-checked against `dlssnr_forwarder.cpp`'s actual exported signature):
  2 real `g_nrMutex` data races (`RetryAfterFailure`; `IsRunning`/`FailureReason`/
  `Calibration`/`GameExposureStatus`), 1 latent UI/UIAlpha aliasing bug in `SetExtras`,
  1 stale-success bug in `EnsureForwarder`'s already-loaded fast path, 1 lock-ordering
  inefficiency in `ProbeD3D11`.
- Active plan: memory/plans/2026-09-06-fix-dlssnr-forwarder-dll-usage-findings.md
  (done, 7/7). Fixed: EnsureForwarder fast-path now checks both create+evaluate;
  RetryAfterFailure + IsRunning/FailureReason/Calibration/GameExposureStatus all
  locked; SetExtras takes a real uiAlpha param (3 call sites updated); ProbeD3D11
  checks the config flag before locking. Step 6's independent-review gate (first
  real run of EP-1's new mandatory step) confirmed all 5 fixes correct, no
  self-deadlocks/lock-ordering issues; its one "bundled changes" note was a false
  alarm caused by nothing being committed to git between this plan and the prior
  one (git diff naturally shows both plans' uncommitted work together). Debug+Release
  x64 both build clean, 0 errors.

## 2026-09-06 — NBA 2K26 dxgi.dll load investigation + ReShade addon64 epic
- Investigated why OptiScaler's proxy-DLL install (`dxgi.dll`) never loads in
  `J:\SteamLibrary\steamapps\common\NBA 2K26` while ReShade's identical technique
  previously worked there. Ruled out, in order: wrong exe/folder (dxgi.dll confirmed
  sitting next to `NBA2K26.exe`, byte-identical to this session's own Release build),
  broken exports (dumpbin confirmed `CreateDXGIFactory`/1/2, `D3D12CreateDevice` all
  present), EasyAntiCheat (user confirmed Offline Mode doesn't attach EAC), and
  `TargetProcess`/`ProcessExclusionList` (both `auto` in the deployed ini; NBA2K26.exe
  isn't in the built-in exclusion default). Discovered `ReShade.log` predates the
  current `dxgi.dll` by a day -- the "ReShade works" comparison was actually
  yesterday's separate ReShade-as-dxgi.dll install vs. today's OptiScaler overwrite
  of that same file, not a live side-by-side test. A direct (non-Steam) launch of
  `NBA2K26.exe` was tried and ruled invalid as a test (process self-exits ~10s in,
  ~39 modules only, no window -- Steamworks ownership check, not a real run). Root
  cause still unconfirmed -- a Process Monitor `Load Image` capture on `dxgi.dll`
  during a real Steam Offline Mode launch is the next decisive step, not yet done
  by the user. Speculatively added an `nba2k26.exe` quirk entry (copied from Path of
  Exile 2's `LoadD3D12Manually`+`DisableDxgiSpoofing`) at the user's request; flagged
  it as unlikely to fix the zero-log symptom since quirks are consulted after the
  point where logging is already failing to happen.
- Given the above, approved and started an epic:
  `memory/plans/2026-09-06-optiscaler-reshade-addon64.md` -- package OptiScaler as a
  ReShade `.addon64` add-on (host = ReShade, guest = OptiScaler; the reverse of the
  existing `LoadReShade` config option), as a general-purpose second install mode,
  not just a one-game workaround. 5 stories: addon-skeleton, device-bridge,
  present-cycle integration, overlay integration, packaging/docs.
- Story 1 (addon-skeleton): built. New `OptiScaler/addon/` project
  (`OptiScalerAddon.vcxproj`, added to `OptiScaler.sln`), vendored 8 ReShade addon-API
  headers under `external/reshade/include/` (+ `LICENSE.md`) from a shallow clone of
  `crosire/reshade` at `D:\DEV\reshade-compare`. `OptiScalerAddon.cpp` implements
  `AddonInit`/`AddonUninit` (confirmed against ReShade's own `addon_manager.cpp` that
  these exact export names/signatures are what its loader looks up), calling
  `reshade::register_addon(...)` and logging. Builds clean, 0 errors/0 warnings,
  Debug+Release x64, output `x64\<Config>\a\OptiScaler.addon64`. Step 3's live
  verification (drop into NBA 2K26, confirm `ReShade.log` shows our registration
  line) is blocked on the user: ReShade is no longer the active `dxgi.dll` in that
  folder (this session's own earlier work overwrote it with OptiScaler's proxy-DLL
  build) and I don't have a prebuilt ReShade binary to restore it with -- user needs
  to reinstall their ReShade build as `dxgi.dll` before this can be exercised.
- Story 1: done. User confirmed the addon loaded successfully via ReShade in NBA 2K26
  (no UI shown, correctly expected -- Story 1 has none).
- Story 2 (device-bridge): found the plan's original approach was wrong while
  implementing -- `D3d12Proxy::Init`/`D3D12Hooks::Hook` only hook the *module-level*
  `D3D12CreateDevice` export (intercepting the game's own creation call), which never
  fires in the addon path since ReShade already created the device before handing it to
  us. Corrected to the real entry point: `D3D12Hooks::HookDevice(ID3D12Device*)`
  (`hooks/D3D12_Hooks.h:19`), feeding `State::Instance().currentD3D12Device`. Also found
  reaching `State`/`D3D12Hooks` requires the addon code to live inside `OptiScaler.vcxproj`
  itself -- retired the standalone `OptiScalerAddon.vcxproj` from Story 1, folded
  `OptiScaler/addon/OptiScalerAddon.cpp` into the main project. This meant the existing
  `DllMain` would otherwise also run its full proxy-DLL init sequence every time ReShade
  loads us, so added an early guard in `dllmain.cpp`'s `DLL_PROCESS_ATTACH`: if the
  module's own extension is `.addon64`/`.addon`, skip straight to `PrepareLogger()` and
  return, bypassing `CheckWorkingMode`/`CheckMemoryForProxies`/NVAPI probing/etc.
  entirely. Both course corrections were surfaced to and approved by the user before
  implementing (R24). One binary now serves both roles: `OptiScaler.dll` (proxy-DLL,
  regression-checked by a clean full-solution Debug+Release x64 build, same warning
  baseline) and the same bytes copied to `OptiScaler.addon64` (addon install). Confirmed
  via `dumpbin /exports` that `AddonInit`/`AddonUninit` are present in the renamed copy.
  Live verification (does `init_device` actually fire and bridge a real device in NBA
  2K26) is blocked on the user dropping the rebuilt `.addon64` in and checking
  `ReShade.log`.
- Story 2: done, verified live (`ReShade.log` showed the device-bridge log line during a
  real NBA 2K26 run). Added an idempotency guard (refuse + warn instead of overwriting
  `currentD3D12Device` if already set) as its 3rd acceptance criterion, rather than
  leaving it unimplemented.
- Story 3: found the "full upscaler chain" scope decided during brainstorming is not
  achievable via the addon path in general -- diagnostic confirmed `nvngx_dlss.dll`,
  `nvngx_dlssg.dll`, `nvngx_dlssnr.dll`, `libxess.dll`, `sl.interposer.dll` all already
  loaded before `AddonInit` runs in NBA 2K26, which kills OptiScaler's `LdrLoadDll`-
  substitution swap technique (`inputs/NVNGX_DLSS_Dx12.cpp`'s own exported
  `NVSDK_NGX_D3D12_EvaluateFeature`) for any game shaped this way. User chose to
  re-scope the epic to DLSS-NR only (this fork's actual feature) rather than keep the
  broader claim or pause. DLSS-NR isn't blocked the same way: `sl.interposer.dll`
  (Streamline) being already-loaded is hookable in place via
  `StreamlineHooks`/`LibraryLoadHooks::CheckModulesInMemory()` (made `public`, was
  `private`) -- real inline hooking, not load substitution, so load-order doesn't
  matter. Wired that into `AddonInit`. Build-verified; live result (does it actually
  fire DLSS-NR's dispatch) pending the user's next test.
- Story 3 step 1 build (`CheckModulesInMemory()` wired into `AddonInit`) crashed the
  game live. `ReShade.log` stopped dead right after `AddonInit`'s diagnostic warnings
  with no further output at all -- traced this (not guessed) by reading
  `CheckModulesInMemory()`/`GetDllNameWModule()`/`KernelBase_Proxy.h` source: the
  `.addon64` `DllMain` guard added in Story 2 returns right after `PrepareLogger()`,
  never calling `NtdllProxy::Init()`/`KernelBaseProxy::Init()`/`Kernel32Proxy::Init()`
  the way the existing `_passThruMode` branch does. `GetDllNameWModule()`'s first call
  goes through `KernelBaseProxy::GetModuleHandleW_()()`, a null function pointer since
  `Init()` never ran -- null-pointer-call access violation (`StreamlineHooks::
  hookInterposer`/etc. all resolve via `KernelBaseProxy::GetProcAddress_()` the same
  way, so this wasn't a one-off). Fixed by adding the same three `Init()` calls (same
  order -- `Kernel32Proxy::Init()` depends on the other two) to the `.addon64` guard
  in `dllmain.cpp` before it returns. Debug+Release x64 both rebuilt clean (0 errors,
  same warning baseline), `OptiScaler.addon64` refreshed. Live re-test (does it now
  survive past `CheckModulesInMemory()` and log Streamline-hook confirmation) pending
  the user.
- Live re-test confirmed the crash fix: `ReShade.log` showed a full 900+ line session
  (device bridge, swapchain, shader compiles), no abrupt stop. Added an explicit
  `reshade::log::message` report of `StreamlineHooks::isInterposerHooked/isDlssHooked/
  isDlssgHooked/isReflexHooked/isPclHooked/isCommonHooked` to `AddonInit` (OptiScaler's
  own file logging defaults off on this path, so `OptiScaler.log`'s `LOG_DEBUG` lines
  from `CheckModulesInMemory()` were otherwise invisible). Live result: `interposer=yes`,
  everything else `=no` (expected -- those plugin DLLs aren't resident at `AddonInit`
  time in this game per the earlier diagnostic).
- Traced `hkslEvaluateFeature` (the hooked function) and found hooking the interposer
  alone does NOT trigger DLSS-NR dispatch -- it only inspects DLSSG resource tags and
  passes every call straight through. Surfaced this to the user via AskUserQuestion
  before building anything further (R24); user chose "Build the redirect (new work)".
- Built the redirect. Added `DlssNr::EvaluateFromStreamline` (declared in
  `dlssnr/DlssNrFeature_Dx12.h`, implemented in `shaders/dlssnr/DlssNr_Dx12.cpp`):
  builds a throwaway `NVSDK_NGX_Parameter` block from raw D3D12 resources, bridges each
  one's actual current state to whatever `EvaluateAfterUpscale`'s `Dispatch()` assumes
  on entry (own before/after barriers -- `Dispatch()` never transitions depth/motion
  itself, only output), then calls `EvaluateAfterUpscale(forcePost=true)`, same
  disposition as native RR passthrough in `NVNGX_DLSS_Dx12.cpp`. Wired into
  `hkslEvaluateFeature`: after the real `slEvaluateFeature` succeeds, if
  `feature==sl::kFeatureDLSS_RR`, extracts Depth/MotionVectors/ScalingOutputColor
  resource tags from `inputs` (reimplemented `sl::findStructs` locally --
  `sl_helpers.h` pulls in a `sl_nis.h` this repo's vendored Streamline subset doesn't
  have), skips defensively if any tag or its `Resource::state` (`UINT_MAX`=unset) is
  missing, reads jitter/mvecScale/reset/depthInverted from a newly-added per-frame
  `sl::Constants` cache (`hkslSetConstants` now stashes the latest one). Explicitly
  flagged as unproven in both the code comments and this plan -- built from reading
  Streamline's SDK headers and OptiScaler's own NGX-parameter contract, not from live
  tracing of what NBA 2K26's actual RR evaluate call carries. Debug+Release x64 both
  rebuilt clean (0 errors). `OptiScaler.addon64` refreshed and deployed into the live
  NBA 2K26 folder; live result (does DLSS-NR actually render, or does something break)
  pending the user's next test.
- Live re-test with real `OptiScaler.log` visibility (LogToFile fix worked, 684-line
  session) showed `hkslEvaluateFeature` firing continuously (~340 frames, no crash) but
  a new one-time marker for `feature==sl::kFeatureDLSS_RR` never fired once -- NBA
  2K26 never evaluates Ray Reconstruction through Streamline at all. Asked the user
  whether the game even has an RR/DLSS-D toggle; answer: no such setting / not sure --
  NBA 2K26 most likely doesn't implement Ray Reconstruction (mainly a
  ray-tracing-heavy-title feature). Closed Story 3 on that basis: the
  `EvaluateFromStreamline` redirect is real, defensively coded, and build-verified, but
  unverified and unverifiable in this specific game since there's no RR evaluate call
  here to intercept. Updated the plan file's Story 3 section and acceptance criteria to
  say so plainly rather than leave it reading as more proven than it is. Real
  validation needs a different title that actually drives DLSS-RR through Streamline.
- Active plan: memory/plans/2026-09-06-optiscaler-reshade-addon64.md (epic
  in-progress, 3/5 stories done -- Story 3 closed: hooking/crash-safety live-verified,
  the DLSS-NR redirect itself unverified for lack of an RR-capable test game. Stories
  4 (overlay) and 5 (packaging/docs) not yet started.)
