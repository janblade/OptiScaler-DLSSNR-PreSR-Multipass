# Epic: OptiScaler as a ReShade `.addon64` (alternative install mode)
- Branch: main
- Created: 2026-09-06
- Status: **aborted 2026-09-06** — original motivating problem (proxy-dxgi.dll "not working"
  in NBA 2K26) turned out to be a misdiagnosis (see closing note below), not a real limitation
  of the proxy-DLL install. With that fixed, this epic's entire reason for existing is gone.
- Type: epic
- Task file: memory/tasks/main.md

## Closing note (2026-09-06)
The premise this epic was built on — "OptiScaler's proxy-dxgi.dll install is confirmed broken
in NBA 2K26" — was wrong. It was caused by `Config::Instance()->LogToFile` defaulting to
`false` (shipped `OptiScaler.ini` has `LogToFile=auto`), so `OptiScaler.log` never got created
and made a fully-working install look dead. Once `LogToFile`/`LogLevel` were forced on for
diagnosis, the proxy-DLL install was confirmed to work correctly end-to-end: device/Streamline
hooking, DLSS evaluating every frame, and — after a separate, unrelated fix to
`DxgiFactoryHooks::CreateSwapChain`'s overlay-classification heuristic (it was misclassifying
the game's real, legitimately-`0x0`-sized "size to window" swapchain as a throwaway overlay
call and never wrapping it) — the menu overlay and startup toast now work too. The user's
actual goal ("make NBA 2K26 work with OptiScaler") is achieved via the existing proxy-DLL
install; no second install mode is needed. Story 1/2 (addon skeleton, DXGI/D3D12 hook bridge)
and Story 3 (DLSS-NR/Streamline-RR redirect) remain as working, merged code but are not being
carried forward as a maintained feature — see task file for what's safe to leave in place vs.
what should be reverted if this dead code becomes a maintenance burden. Story 4 (ReShade-tab
overlay integration) and Story 5 (packaging/docs) were never started and won't be.

## Context (historical — the premise below is now known to be wrong, kept for the record)
OptiScaler currently installs by renaming itself to `dxgi.dll`/`d3d12.dll`/etc. and relying
on Windows' DLL search order to load in place of the system library — this is confirmed
broken in NBA 2K26 (`OptiScaler.log` never appears; exports, folder placement, EAC, and
process-exclusion config have all been ruled out). ReShade, using the identical proxy-DLL
technique, does load successfully in that same game. This epic builds a second, permanently-
supported install mode: package OptiScaler as a ReShade add-on (`.addon64`), so it rides
ReShade's already-working hook instead of duplicating Windows DLL-search hijacking. This is
additive — the existing proxy-DLL install stays the default and is untouched. It is also the
*opposite* direction from OptiScaler's existing `LoadReShade` config option (where OptiScaler
hosts ReShade as a chained overlay) — this epic makes ReShade the host and OptiScaler the
guest instead; the two mechanisms don't interact and this epic doesn't touch `LoadReShade`.

Confirmed from ReShade's own headers (`reshade.hpp`, `reshade_events.hpp`): an addon is a
normal PE DLL loaded by ReShade via `LoadLibraryExW`, which calls `reshade::register_addon()`
(scans in-process modules for `ReShadeRegisterAddon`/`ReShadeUnregisterAddon` exports to find
the host) from `DllMain`/`AddonInit`, then registers typed callbacks via
`reshade::register_event<addon_event::EVENT>(...)`. ReShade's device/resource/swapchain types
are opaque wrappers with a `get_native()` accessor that returns the real backend pointer
(`ID3D12Device*`, `IDXGISwapChain*`, etc.) — that's the bridge point that lets OptiScaler's
existing D3D12 hook code (`hooks/D3D12_Hooks.cpp`, `proxies/D3D12_Proxy.h`) operate on real
native handles once ReShade hands them over, without a full rewrite.

**Scope narrowed during Story 3 (was "full upscaler chain: DLSS/FSR/XeSS swap-in plus
DLSS-NR", now DLSS-NR only):** live testing in NBA 2K26 confirmed `nvngx_dlss.dll`,
`nvngx_dlssg.dll`, `nvngx_dlssnr.dll`, `libxess.dll`, and `sl.interposer.dll` are all already
loaded before `AddonInit` ever runs. OptiScaler's upscaler-swap mechanism
(`OptiScaler/inputs/*.cpp`, e.g. its own exported `NVSDK_NGX_D3D12_EvaluateFeature` at
`inputs/NVNGX_DLSS_Dx12.cpp:1092`) only works if the game resolves its function pointers
against OptiScaler's module instead of the real vendor DLL — which requires winning the
original `LoadLibrary` race, already lost by the time the addon exists. This is a hard
timing ceiling for any game that eagerly loads these runtimes at startup (confirmed true for
NBA 2K26; may not hold for every game, but isn't a general capability this epic can promise).
DLSS-NR is different: OptiScaler's own dispatch for it can still be triggered via Streamline
in-memory hooking (`StreamlineHooks`/`LibraryLoadHooks::CheckModulesInMemory()`, the same
fallback `dllmain.cpp` uses when Streamline is already resident) rather than load
substitution — real inline hooking of an already-loaded module, unaffected by load order.
Whether that actually fires DLSS-NR's dispatch in practice is what Story 3 is now testing.

**Epic-level acceptance criteria (what "epic done" means) — revised:**
- [x] A built `OptiScaler.addon64` loads successfully as a ReShade addon in NBA 2K26 and
      produces log output proving registration (Story 1).
- [x] Real D3D12 device successfully bridged into OptiScaler's existing hook code (Story 2).
- ~~At least one upscaler backend (DLSS, FSR, or XeSS swap) renders correctly end-to-end~~ —
      **dropped**: confirmed blocked by DLL-load timing (see above), not a capability this
      epic can deliver in general.
- [ ] DLSS-NR multipass renders correctly through this path.
- [ ] OptiScaler's settings menu is reachable and functional via ReShade's overlay.
- [x] The existing proxy-DLL install mode is unaffected (regression-checked by clean
      full-solution Debug+Release x64 builds throughout).

**Out of scope:** Vulkan/OpenGL addon support (D3D12 only, matching the primary interception
path in `D3D12_Hooks.cpp`); porting `LoadReShade`'s chained-overlay mechanism; any change to
the default (proxy-DLL) install instructions; swapping the game's own upscaler choice
(DLSS↔FSR↔XeSS redirection) — confirmed blocked by DLL-load timing in games that eagerly
load vendor runtimes at startup, not a reliable general capability.

## Story 1: addon-skeleton — minimal addon that registers and logs in a blocked game
Status: done
### Acceptance Criteria
- [x] A new build target (or standalone project) produces `OptiScaler.addon64`, a valid PE64
      DLL exporting `AddonInit`/`AddonUninit`
- [x] `reshade::register_addon()` succeeds and `reshade::log::message(...)` output appears in
      `ReShade.log` when loaded via ReShade in NBA 2K26 (Offline Mode) — user confirmed the
      addon loaded successfully
- [x] Loading this addon does not prevent ReShade's own effects/UI from working normally —
      user reported no disruption to ReShade itself, only (expected) absence of OptiScaler's
      own UI, which Story 1 never implements
### Steps
1. [x] Add a new project (`OptiScaler/addon/OptiScalerAddon.vcxproj`, added to
   `OptiScaler.sln`) linking the 8 ReShade addon-API headers vendored under
   `external/reshade/include/` (`reshade.hpp`, `reshade_events.hpp`, `reshade_api.hpp`,
   `reshade_api_device.hpp`, `reshade_api_pipeline.hpp`, `reshade_api_resource.hpp`,
   `reshade_api_format.hpp`, `reshade_overlay.hpp`, plus `external/reshade/LICENSE.md` for
   attribution), `TargetName=OptiScaler`/`TargetExt=.addon64`, output alongside the main
   build at `x64\$(Configuration)\a\` — verify: `MSBuild -t:OptiScalerAddon` succeeded 0
   errors/0 warnings for both Debug|x64 and Release|x64, confirmed
   `x64\Debug\a\OptiScaler.addon64` and `x64\Release\a\OptiScaler.addon64` exist.
2. [x] Implemented `OptiScaler/addon/OptiScalerAddon.cpp`: `AddonInit(addon_module,
   reshade_module)` calls `reshade::register_addon(...)` then
   `reshade::log::message(reshade::log::level::info, "OptiScaler addon loaded")` and
   returns `true`; `AddonUninit` logs and calls `reshade::unregister_addon(...)`. Confirmed
   against ReShade's own `source/addon_manager.cpp` (lines ~205, ~355) that these two exact
   export names (`AddonInit`/`AddonUninit`, `bool(HMODULE,HMODULE)` /
   `void(HMODULE,HMODULE)`) are what ReShade's addon loader looks up via `GetProcAddress`
   — this is a build-verified, code-correct implementation; the "log line appears in
   ReShade.log" half of this step's verification is a live-game outcome, folded into step
   3 below since it can't be checked independently of actually loading it.
3. [x] Dropped the built `.addon64` into the NBA 2K26 folder and launched via Steam
   Offline Mode — user confirmed the addon loaded successfully via ReShade in the exact
   game where the proxy-DLL install (`dxgi.dll`) never loads at all. No OptiScaler UI
   appeared, which is expected and correct: Story 1 implements only registration and
   logging, no device bridge, no present-cycle dispatch, no overlay — those are Stories
   2, 3, and 4 respectively. Confirms the epic's core premise: OptiScaler code can reach
   this game via ReShade's hook where its own proxy-DLL technique cannot.

## Story 2: device-bridge — get real D3D12 native handles from ReShade's addon events
Status: done
depends on: Story 1

**Architecture correction (found while implementing, surfaced and approved before
proceeding):** the original steps below assumed `D3d12Proxy::Init(HMODULE)`/
`D3D12Hooks::Hook()` were the right integration point. They aren't — both hook the
*module-level* `D3D12CreateDevice` export, i.e. they intercept the game *calling* that
factory function. In the addon path ReShade has already created the device before handing
it to us via `init_device`, so there is no creation call left to intercept. The actual
correct, already-existing entry point is `D3D12Hooks::HookDevice(ID3D12Device* device)`
(`hooks/D3D12_Hooks.h:19`), which instruments an already-existing device instance directly,
feeding `State::Instance().currentD3D12Device` — the same global every downstream upscaler
feature reads.

Second correction: reaching `State`/`D3D12Hooks` requires the addon code to live inside
`OptiScaler.vcxproj` itself, not the small standalone project Story 1 built (a separate
project can't reach that code without duplicating it). Retired
`OptiScaler/addon/OptiScalerAddon.vcxproj` from `OptiScaler.sln`; `OptiScaler/addon/
OptiScalerAddon.cpp` is now compiled directly into the main project. This meant the
existing `DllMain` (`dllmain.cpp`) would otherwise also run its full proxy-DLL
initialization sequence (`CheckWorkingMode`, `CheckMemoryForProxies`, NVAPI probing, etc.)
every time ReShade loads us — none of which applies when ReShade, not Windows' DLL search
order, is the one who loaded this binary. Added an early guard in `DllMain`
(`dllmain.cpp`, right after `MainDllPath`/`PluginPath` resolution): if this module's own
file extension is `.addon64`/`.addon`, skip straight to `PrepareLogger()` + a log line and
return, bypassing every proxy-DLL-specific step. Real addon init happens in `AddonInit`
instead, called by ReShade itself after `DllMain` returns.

One binary now serves both roles: `OptiScaler.dll` (proxy-DLL install, unchanged behavior,
regression-checked by a clean full-solution Debug+Release x64 build) and the same bytes
copied/renamed to `OptiScaler.addon64` (ReShade addon install, this story). Confirmed via
`dumpbin /exports` that the addon64 copy exports `AddonInit`/`AddonUninit`.

### Acceptance Criteria
- [x] `init_device` addon event fires and yields a working `ID3D12Device*` via
      `get_native()` — user confirmed `"OptiScaler addon: bridging real D3D12 device from
      ReShade"` in `ReShade.log` during a live NBA 2K26 run
- [x] That native pointer successfully drives `State::Instance().currentD3D12Device` +
      `D3D12Hooks::HookDevice(...)` without crashing — confirmed live (the log line only
      appears after `HookDevice` is reached without throwing/crashing the process)
- [x] No double-initialization if both install modes could theoretically coexist — added
      a guard: refuses and logs a warning instead of overwriting `currentD3D12Device` if
      it's already non-null, rather than leaving this unimplemented
### Steps
1. [x] Registered `reshade::addon_event::init_device` (`OnInitDevice` in
   `OptiScaler/addon/OptiScalerAddon.cpp`); guards on `device->get_api() ==
   reshade::api::device_api::d3d12` first (Vulkan/OpenGL addons are out of scope per this
   epic's Context) — verify: live log confirms the event fired for a real device
2. [x] Feed the native device pointer into `State::Instance().currentD3D12Device` and
   `D3D12Hooks::HookDevice(realDevice)` directly (the corrected integration point, not
   `D3d12Proxy::Init`/`D3D12Hooks::Hook`) — verify: Debug+Release x64 both build 0 errors,
   0 new warnings vs. baseline; `dumpbin /exports` confirmed `AddonInit`/`AddonUninit`
   present; live `ReShade.log` in NBA 2K26 shows the bridge fired
3. [x] Added an idempotency guard: if `currentD3D12Device` is already non-null when
   `OnInitDevice` fires, log a warning and return instead of overwriting it — verify:
   Debug+Release x64 both build 0 errors after adding it
4. [x] Dropped the rebuilt `x64\Release\a\OptiScaler.addon64` into the NBA 2K26 folder and
   launched via Steam Offline Mode — user confirmed `ReShade.log` shows
   `"OptiScaler addon: bridging real D3D12 device from ReShade"`, proving the bridge
   fires for a real, live D3D12 device in the exact game the proxy-DLL install cannot
   reach at all

## Story 3: DLSS-NR dispatch — get OptiScaler's Neural Rendering multipass actually triggered
Status: done (blocked on this game, not on the code — see closing note)
depends on: Story 2

**Scope-affecting finding, resolved — the story was rescoped, not abandoned:** the original
steps below assumed the per-frame upscale dispatch lives near `D3D12_Hooks.cpp`'s `Present`/
`ExecuteCommandLists` hooks. It doesn't. Traced `LocalPresent`
(`wrapped/wrapped_swapchain.cpp:149`) — the proxy-DLL install's actual present hook — and
found it only does device/queue capture, GPU-timing bookkeeping, vsync forcing, ticking the
feature's frozen-check, drawing the menu overlay, and the real `Present` call. No upscale
dispatch call lives there.

The real trigger for OptiScaler's upscale/DLSS-NR logic is the *game itself* calling
`NVSDK_NGX_D3D12_EvaluateFeature` — which OptiScaler intercepts by exporting its own function
of that exact name (`inputs/NVNGX_DLSS_Dx12.cpp:1092`) and getting the game to resolve its
pointer against OptiScaler's module instead of the real `nvngx.dll`, via `LdrLoadDll`
substitution installed early in the proxy-DLL install's `DllMain`. Added a diagnostic
(`LogAlreadyLoadedUpscalerModules` in `OptiScaler/addon/OptiScalerAddon.cpp`) checking
`GetModuleHandleW` for the relevant vendor DLLs at `AddonInit` time; **live result from the
user**: `nvngx_dlss.dll`, `nvngx_dlssg.dll`, `nvngx_dlssnr.dll`, `libxess.dll`, and
`sl.interposer.dll` were all already loaded before `AddonInit` ever ran — confirming the
substitution technique is a hard dead end here (the game already resolved its real function
pointers before the addon existed to redirect anything).

That kills the general "upscaler swap" goal (moved to the epic's Out of scope), but not
DLSS-NR specifically: `sl.interposer.dll` (NVIDIA Streamline) was also already loaded, and
this codebase already has proven code for hooking Streamline *in place* —
`StreamlineHooks::hookInterposer()`/`hookDlss()`/etc., swept by
`LibraryLoadHooks::CheckModulesInMemory()` (`hooks/LibraryLoad_Hooks.h:7`, made `public` —
was `private`, needed by the addon entry point) — the exact same fallback `dllmain.cpp`
itself uses when a Streamline module is already resident. That's real inline hooking of an
already-loaded module (unlike `LdrLoadDll` substitution), so it doesn't care that the module
loaded before `AddonInit` — only that the hook goes on before the game *calls into* it.
Wired `LibraryLoadHooks::CheckModulesInMemory()` into `AddonInit` right after registering the
device-init event. **Live-confirmed**: `StreamlineHooks::hookInterposer` attached
(`interposer=yes` in the addon's own status report), and a real, non-abandoned play session
showed `hkslEvaluateFeature` firing continuously (twice per frame, ~340 frames captured) — the
hook is genuinely live and being called during real gameplay, not just at startup.

Once the hook was confirmed, tracing `hkslEvaluateFeature`'s body found it only ever inspected
DLSSG resource tags and passed every call straight through — no code path redirected a Ray-
Reconstruction evaluate into OptiScaler's own `DlssNr_Dx12.cpp` composition pass at all. That
redirect didn't exist; it needed building, not wiring. Surfaced to the user before writing it
(R24); user chose to build it.

Built `DlssNr::EvaluateFromStreamline` (declared `dlssnr/DlssNrFeature_Dx12.h`, implemented
`shaders/dlssnr/DlssNr_Dx12.cpp`): builds a throwaway `NVSDK_NGX_Parameter` block from
Streamline's tagged D3D12 resources, bridges each one's actual current
`D3D12_RESOURCE_STATES` to whatever `EvaluateAfterUpscale`'s `Dispatch()` assumes on entry
(own before/after barriers, since `Dispatch()` never transitions depth/motion itself, only
output), then calls `EvaluateAfterUpscale(forcePost=true)` — the same disposition the native
RR passthrough in `NVNGX_DLSS_Dx12.cpp` uses. Wired into `hkslEvaluateFeature`: after the real
`slEvaluateFeature` succeeds, if `feature==sl::kFeatureDLSS_RR`, extracts
Depth/MotionVectors/ScalingOutputColor tags from `inputs`, skips defensively (logged) if a tag
or its `Resource::state` (`UINT_MAX`=unset) is missing rather than guess and risk an incorrect
barrier, reads jitter/mvecScale/reset/depthInverted from a newly-cached per-frame
`sl::Constants` (`hkslSetConstants` now stashes the latest one).

Two log-visibility bugs surfaced and were fixed along the way, both from the same root cause:
OptiScaler's own diagnostics (`LOG_DEBUG`/`LOG_WARN`/`LOG_INFO`) go through its internal
spdlog, not `reshade::log::message`, and `OptiScaler.log` was never being created on the addon
path (`LogToFile` defaults off) — so `CheckModulesInMemory()`'s own confirmation lines, and
later `EvaluateInternal`'s and the bridge's, were invisible in `ReShade.log`. Fixed at the
source rather than patching another one-off `reshade::log::message` call each time: the
`.addon64` guard in `dllmain.cpp` now forces `LogToFile=true` unless the user has explicitly
set it themselves, so `OptiScaler.log` gets created and all of OptiScaler's existing logging
becomes visible for this install mode.

**Final live result, with real `OptiScaler.log` visibility**: added a one-time marker that
fires the moment `hkslEvaluateFeature` is ever called with `feature==sl::kFeatureDLSS_RR`,
regardless of tag/state availability. Across a full session (684 log lines,
`hkslEvaluateFeature` firing continuously) that marker **never fired once** — NBA 2K26 never
evaluates Ray Reconstruction through Streamline at all. Asked the user whether the game even
exposes an RR/DLSS-D toggle; answer: no such setting, or not sure — NBA 2K26 most likely
doesn't implement Ray Reconstruction (it's mainly a ray-tracing-heavy-title feature). The
redirect is real, defensively coded, and reviewable, but **unverified and unverifiable in this
specific game** — there is no RR evaluate call here for it to ever intercept. Closing this
story rather than continuing to chase a call that will never come in NBA 2K26; genuine
validation needs a different title that actually drives DLSS Ray Reconstruction through
Streamline.

### Acceptance Criteria
- [x] Streamline's interposer gets hooked in place via `CheckModulesInMemory()`, confirmed
      live (`interposer=yes`, `hkslEvaluateFeature` firing every frame in real gameplay)
- [ ] OptiScaler's DLSS-NR dispatch actually gets called for real frames in NBA 2K26 through
      this path — **not achievable in this game**: it never evaluates Ray Reconstruction
      through Streamline at all (confirmed live, not assumed), so there is nothing for the
      redirect built below to intercept here
- [ ] DLSS-NR multipass visibly renders correctly — blocked on the above; needs a different
      test title that actually uses DLSS-RR via Streamline
### Steps
1. [x] Wired `LibraryLoadHooks::CheckModulesInMemory()` into `AddonInit` to hook Streamline
   in place regardless of when it loaded — live-verified: `interposer=yes`, no crash across a
   full session
2. [x] Traced `hkslEvaluateFeature` and found no existing redirect from a Ray-Reconstruction
   evaluate into `DlssNr_Dx12.cpp`; built one (`DlssNr::EvaluateFromStreamline`) after
   confirming with the user this was new work, not reuse — build-verified (Debug+Release
   x64, 0 errors), logic reviewable in `hooks/Streamline_Hooks.cpp` and
   `shaders/dlssnr/DlssNr_Dx12.cpp`
3. [x] Confirmed, live, that NBA 2K26 never fires the code path this story targets — it
   never evaluates Ray Reconstruction through Streamline (684-line session, continuous
   `hkslEvaluateFeature` activity, zero RR evaluates) — not a code failure, a mismatch
   between this game and the story's premise
4. [ ] Enable DLSS-NR end-to-end and visually confirm correct multipass output — **needs a
   different game** that actually drives DLSS Ray Reconstruction through Streamline; not
   possible in NBA 2K26

## Story 4: overlay integration — OptiScaler's menu inside ReShade's overlay
Status: approved
depends on: Story 3
### Acceptance Criteria
- [ ] OptiScaler's existing ImGui settings menu renders inside ReShade's own overlay via
      `reshade::register_overlay(...)`
- [ ] Menu interactions (toggling upscalers, adjusting DLSS-NR pass count) take effect
      immediately, same as the proxy-DLL install
### Steps
1. [ ] Register an overlay callback via `reshade::register_overlay("OptiScaler", callback)`
   that invokes the existing menu-drawing code (`menu/menu_common.cpp`) against ReShade's own
   ImGui context instead of OptiScaler's own — verify: menu appears when ReShade's overlay
   key is pressed
2. [ ] Confirm setting changes (upscaler selection, DLSS-NR multipass count) propagate to the
   Story 3 dispatch path — verify: changing a setting visibly changes rendered output within
   the same session, no restart needed

## Story 5: packaging & docs
Status: approved
depends on: Story 4
### Acceptance Criteria
- [ ] `setup_windows.bat` and README document the addon install mode as an explicit
      alternative, with guidance on when to prefer it (proxy-DLL install confirmed
      non-functional)
- [ ] Both install modes build cleanly from a single `OptiScaler.sln` solution run
### Steps
1. [ ] Add the addon build to `OptiScaler.sln` as a buildable project alongside the existing
   proxy-DLL target — verify: full solution build (Debug + Release x64) succeeds, 0 errors
2. [ ] Document install steps (copy `.addon64` next to ReShade's own DLL, no rename needed)
   in README/a new `INSTALL-ADDON.md` — verify: a fresh read-through matches the actual steps
   taken in Stories 1-4's manual tests
