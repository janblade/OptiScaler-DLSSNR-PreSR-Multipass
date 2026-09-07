# Architecture Overview

> System architecture, module boundaries, and host integrations.

- **Fork chain / git remotes**: `origin` = `janblade/OptiScaler-DLSSNR-PreSR-Multipass`
  (this checkout's own fork, push target for day-to-day work), `wilsjo2` =
  `wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass` (the immediate parent this fork was
  created from — the target for upstreaming fixes that belong to the DLSS-NR/multipass
  feature line), `upstream` = `optiscaler/OptiScaler` (the true root project, several
  forks removed — the target for `INFRA_SYNC_UPSTREAM` and for fixes with no
  DLSS-NR-specific dependency, e.g. the NBA 2K26 swapchain-wrapping fix went to
  `wilsjo2` as PR #2, not `upstream`, since it only makes sense in `wilsjo2`'s DXGI
  hook code shape). Direct pushes to either non-`origin` remote get a 403 — fork to
  the user's own account (`gh repo fork`) and push there instead, never force/retry.

- **DXGI factory hooking is Detours-based inline code-patching, not vtable-pointer
  patching**: `DxgiFactoryHooks::HookToFactory` (`hooks/DxgiFactory_Hooks.cpp`) calls
  `DetourAttach` on function pointers read from an `IDXGIFactory` instance's vtable
  once, guarded by `if (o_EnumAdapters != nullptr) return`. This is safe precisely
  because Detours patches the *target function's machine code in memory*, not the
  vtable slot's stored pointer value — so every `IDXGIFactory`/`IDXGIFactory2`/etc.
  instance the process later obtains, however it obtains it, shares the same
  underlying implementation and is therefore already intercepted once the first
  instance triggers the hook install. Don't reintroduce a "did we already hook this
  specific instance" check keyed on comparing a vtable slot's pointer value against
  the hooked function — that comparison is meaningless (the trampoline `DetourAttach`
  produces lives at a different address than the original vtable slot's value ever
  did) and was already tried and removed as a dead end.

- **The menu/overlay and DLSS itself are independent, and only one depends on the
  swapchain being wrapped**: OptiScaler's DLSS override works via `LdrLoadDll`
  substitution of `nvngx*.dll` (its own exported `NVSDK_NGX_D3D12_EvaluateFeature`,
  `inputs/NVNGX_DLSS_Dx12.cpp`) and never touches the game's `IDXGISwapChain` — it can
  work perfectly even if the swapchain is never wrapped. The menu/toast overlay
  (`MenuCommon`/`MenuOverlayBase`, ImGui-based) is driven entirely off
  `WrappedIDXGISwapChain4::Present`, so if the game's real swapchain is never
  intercepted by one of DXGI's 4 swapchain-creation entry points
  (`IDXGIFactory::CreateSwapChain`, `IDXGIFactory2::CreateSwapChainForHwnd`/
  `CreateSwapChainForCoreWindow`/`CreateSwapChainForComposition`, all hooked in
  `DxgiFactory_Hooks.cpp`), the menu never initializes at all — with zero crash, zero
  error, and DLSS visibly working, which reads as "half the feature works" but is
  actually a single missed swapchain-creation call. `DxgiFactoryHooks::CreateSwapChain`
  now correctly treats a `Width==0 && Height==0` descriptor with a real (`GetClientRect`
  ≥100×100) `OutputWindow` as DXGI's documented "size to the window's client area"
  idiom rather than a tiny overlay/helper swapchain — see `known_gotchas.md` for why
  this was hard to diagnose.
