# DLSS-NR requirements matrix

This is a setup reference for running this fork's **DLSS Neural Rendering** (NR) pass. NR drives
NVIDIA's neural-rendering model — `nvngx_dlssnr.dll`, NGX feature 18 — over the frame at the
point where OptiScaler intercepts the game's upscaler.

It is broken down by graphics API (DX9 / DX11 / DX12 / Vulkan) and GeForce RTX generation
(50 / 40 / 30), across four scenarios: NR on its own, and NR combined with **DLSS Ray
Reconstruction**, **Frame Generation**, or **Multi-Frame Generation**.

Read [How to read this document](#how-to-read-this-document) and [§0](#0-base-requirements)
first, then jump to the scenario you need. [§1](#1-dlss-nr-on-its-own) is the baseline;
scenarios [§2](#2-dlss-nr-with-dlss-ray-reconstruction),
[§3](#3-dlss-nr-with-frame-generation) and [§4](#4-dlss-nr-with-multi-frame-generation) each add
to §1, and §4 also needs the Frame Generation setup from §3. Every matrix links down to the
file-placement, overlay / INI and [caveats](#7-caveats) sections that finish its answer.

---

## How to read this document

### Contents

- [How to read this document](#how-to-read-this-document) — [Matrix legend](#matrix-legend) · [Key concepts](#key-concepts)
- [0. Base requirements](#0-base-requirements) — needed for every scenario below
- [1. DLSS-NR on its own](#1-dlss-nr-on-its-own) — the baseline; scenarios 2–4 extend it
- [2. DLSS-NR with DLSS Ray Reconstruction](#2-dlss-nr-with-dlss-ray-reconstruction)
- [3. DLSS-NR with Frame Generation](#3-dlss-nr-with-frame-generation)
- [4. DLSS-NR with Multi-Frame Generation](#4-dlss-nr-with-multi-frame-generation)
- [5. Where each file goes](#5-where-each-file-goes)
- [6. Overlay settings and their INI keys](#6-overlay-settings-and-their-ini-keys)
  - *§6 quick links —*
    - [Frame Generation provider reference](#frame-generation-provider-reference)
    - placement: [post-SR](#nr-post-sr-baseline-placement) · [before SR](#nr-before-sr-pre-sr-multipass) · [model precision](#nr-model-precision-rtx-50-hybrid) · [DeferredDLSS](#nr-with-deferreddlss) · [Ray Reconstruction](#nr-with-ray-reconstruction)
    - frame gen: [FSR FG](#nr-with-fsr-frame-generation) · [real DLSS-G](#nr-with-real-nvidia-dlss-g) · [FSR replacement FG](#nr-with-fsr-replacement-fg)
    - multi-frame: [RTX 50 native](#nr-with-mfg-rtx-50-native) · [RTX 40 unlock](#nr-with-mfg-rtx-40-unlock) · [FSR-based MFG](#nr-with-fsr-based-mfg)
- [7. Caveats](#7-caveats) — the authoritative statement of every constraint a matrix *cell* tags
- [Sources](#sources)

### Matrix legend

Every cell in §1–§4 starts with one of these four tokens:

- **Yes** — a supported path exists and has been exercised by the fork's smoke tests or limited
  local game testing.
- **Bridge** — works, but only through an OptiScaler D3D12 bridge: OptiScaler runs the upscaler
  on a D3D12 device it owns (`with_dx12`) for a game whose native API is D3D11 or Vulkan.
- **Experimental** — the code path exists but is **not validated** on this API / GPU / stack
  (it may also be a bridge path). Expect to verify it yourself.
- **N/A** — not achievable with this stack.

In §3–§4 a bridge path is additionally unvalidated for Frame Generation, so it is shown as
**Experimental** rather than **Bridge**.

Columns are always **RTX 50 (Blackwell)**, **RTX 40 (Ada)**, **RTX 30 (Ampere)**. The RTX 30
column also covers **RTX 20 (Turing)** — same cross-generation runtime, same fragility caveat
([§7](#7-caveats)); prose and cells write that bucket as **"RTX 30 / 20"**, which the column
header abbreviates.

### Key concepts

| Term | Meaning | More |
|---|---|---|
| Upscale seam | Where OptiScaler intercepts the game's upscaler (`NVSDK_NGX_D3D12_EvaluateFeature`). NR runs here. | [§0](#0-base-requirements) |
| post-SR | NR runs *after* Super Resolution, on the upscaled image. The default placement. | [§6](#nr-post-sr-baseline-placement) |
| pre-SR multipass | NR runs *before* SR on the render-resolution frame, optionally 1–3 sequential model passes (`RunBeforeSR=true`). | [§6](#nr-before-sr-pre-sr-multipass) |
| DeferredDLSS | NR is computed at render resolution, its *change* is upscaled by a private DLSS pass, then applied after SR (`DeferredDLSS=true`). | [§6](#nr-with-deferreddlss) |
| ResidualFG | Experimental: run NR every second frame and interpolate its change with NVIDIA FG. Off by default. | [§3](#3-dlss-nr-with-frame-generation) |
| Hybrid precision | This fork's FP8+NVFP4 model path for RTX 50 (`Precision=4`); needs assets not in the repo. | [§6](#nr-model-precision-rtx-50-hybrid) · [§7](#7-caveats) |
| Forwarder vs model DLL | `nvngx.dll_dlssnr.dll` = this fork's ~13 KB **forwarder shim** (the caller-gate; in the release). `nvngx_dlssnr.dll` = NVIDIA's model (you supply). Both required, and different from `nvngx_dlss.dll` (SR) and `nvngx_dlssd.dll` (RR). | [§5](#5-where-each-file-goes) |
| ShortFuse cross-generation runtime | A repackaged `nvngx_dlssnr.dll` 310.8 that adds architecture-compatible GPU programs for RTX 40 / 30 / 20. RTX 50 uses the original. Called "ShortFuse cross-generation runtime" throughout. | [§1](#1-dlss-nr-on-its-own) |

---

## 0. Base requirements

Needed for **every** scenario in §1–§4.

| Requirement | Detail |
|---|---|
| GeForce RTX GPU | Turing or newer. OptiScaler's `dlssCapable` gate is `architecture_id >= TU100`. GTX / non-NVIDIA cannot run the model at all. |
| NVIDIA driver | **616.56 or newer**. |
| OptiScaler install | Full release archive extracted next to the real game exe, `OptiScaler.dll` renamed to a proxy name via `setup_windows.bat` (`dxgi.dll` usual; `dbghelp.dll` for Cyberpunk). |
| Forwarder shim | `nvngx.dll_dlssnr.dll` — ships in the release, open-source, no NVIDIA code. Satisfies the model's "caller path must contain nvngx.dll" check. |
| Model runtime | A separately obtained `nvngx_dlssnr.dll` **310.8**, matched to the GPU — see [§1](#1-dlss-nr-on-its-own). Not redistributed. |
| A hookable upscaler in the game | DLSS SR, FSR2 / FSR3.1, or XeSS. NR hangs off `NVSDK_NGX_D3D12_EvaluateFeature`; FSR / XeSS titles reach it transitively. **No upscaler in the game = no NR.** |
| The seam must reach a supported backend | Native D3D12, the D3D11→D3D12 bridge, the Vulkan→D3D12 bridge, or the native-Vulkan NR path. |
| 64-bit game | 32-bit is not supported. |
| No kernel anti-cheat | Do not inject into protected multiplayer titles. |

---

## 1. DLSS-NR on its own

The baseline setup — everything the other three scenarios extend.

**Builds on:** [§0](#0-base-requirements).

Extra requirements on top of §0:

- The `nvngx_dlssnr.dll` **310.8** model runtime, matched to the GPU (**Runtime per generation**
  table, below the matrix).

Legend: **Yes · Bridge · Experimental · N/A** — see [Matrix legend](#matrix-legend).

| Game API | RTX 50 (Blackwell) | RTX 40 (Ada) | RTX 30 (Ampere) |
|---|---|---|---|
| **DX9** | **N/A** ([§7](#7-caveats)) | **N/A** | **N/A** |
| **DX11** | **Bridge** — D3D11→D3D12 (`with_dx12`); NR call site `IFeature_Dx11wDx12.cpp`. | **Bridge** — same. | **Bridge** — same. Fragile ([§7](#7-caveats)). |
| **DX12** | **Yes** — native seam (`NVNGX_DLSS_Dx12.cpp`); all placements (post-SR, pre-SR multipass, DeferredDLSS). Hybrid precision available. | **Yes** — native seam, all placements. | **Yes** — native seam, all placements. Fragile ([§7](#7-caveats)). |
| **Vulkan (native)** | **Yes** — `DlssNrFeature_Vk.cpp`; post-SR and pre-SR. DeferredDLSS not available here ([§7](#7-caveats)). | **Yes** — same. | **Yes** — same. Fragile ([§7](#7-caveats)). |
| **Vulkan (D3D12 bridge)** | **Bridge** — `IFeature_VkwDx12.cpp`; all placements incl. DeferredDLSS. | **Bridge** — same. | **Bridge** — same. |

**Runtime per generation** — the `nvngx_dlssnr.dll` you supply:

| GPU | Runtime | Notes |
|---|---|---|
| RTX 50 | Original NVIDIA-signed 310.8 | SHA-256 `E16BCF15…E1FC8E`; Authenticode valid. |
| RTX 40 | ShortFuse cross-generation runtime, 310.8 | SHA-256 `E67DEE20…1C989A`; auto-selects an Ada path. Modified binary, so Windows reports the signature invalid — expected; verify by hash. |
| RTX 30 / 20 | ShortFuse cross-generation runtime, 310.8 | Same file as RTX 40; auto-selects the FP16 path on Ampere and Turing. |

→ File placement: [§5](#5-where-each-file-goes) · Overlay / INI: [§6](#6-overlay-settings-and-their-ini-keys) · Caveats: [§7](#7-caveats)

---

## 2. DLSS-NR with DLSS Ray Reconstruction

Ray Reconstruction (DLSS-D) replaces the game's denoiser; NR then runs on its output. Relevant
only for path- and ray-traced titles.

**Builds on:** [§1](#1-dlss-nr-on-its-own).

Extra requirements on top of §1:

- The game must **integrate Ray Reconstruction** (path- / ray-traced titles — Cyberpunk 2077,
  Alan Wake 2, Portal RTX…), or OptiScaler must be enabling RR. RR needs the game's own
  albedo / normal-roughness / noisy guides; they cannot be synthesised.
- A separate **`nvngx_dlssd.dll`** RR runtime (not the same file as `nvngx_dlssnr.dll`). No
  per-generation split — it is the ordinary game / NVIDIA RR DLL.
- NR runs on the RR+SR seam, **after RR+SR** (the supported placement). Pre-RR exists in code
  but is experimental ([§7](#7-caveats)). NR never edits RR's input guides.

Legend: **Yes · Bridge · Experimental · N/A** — see [Matrix legend](#matrix-legend).

| Game API | RTX 50 (Blackwell) | RTX 40 (Ada) | RTX 30 (Ampere) |
|---|---|---|---|
| **DX9** | **N/A** ([§7](#7-caveats)) | **N/A** | **N/A** |
| **DX11** | **N/A** — no DX11 game ships Ray Reconstruction. | **N/A** | **N/A** |
| **DX12** | **Yes** — NR after RR+SR on the native seam. | **Yes** — same. | **Yes** — same. Fragile, and RR costs more here ([§7](#7-caveats)). |
| **Vulkan (native)** | **Yes** — `EvaluateAtSeamVk(rayReconstruction=true)`; NR after RR. | **Yes** — same. | **Yes** — same. Fragile ([§7](#7-caveats)). |
| **Vulkan (D3D12 bridge)** | **Bridge** — NR after RR. | **Bridge** — same. | **Bridge** — same. |

→ File placement: [§5](#5-where-each-file-goes) · Overlay / INI: [§6](#nr-with-ray-reconstruction) · Caveats: [§7](#7-caveats)

---

## 3. DLSS-NR with Frame Generation

Frame Generation is a separate subsystem from NR — NR runs on the upscale seam, FG runs at
frame-present time. They coexist, but a working NR pass does not prove FG has the depth, motion
vectors, swapchain and pacing it needs; bring FG up first at 2× with NR off, then add one NR pass.

**Builds on:** [§1](#1-dlss-nr-on-its-own).

Extra requirements on top of §1:

- A **DX12 game** for OptiScaler's own FG output (OptiFG). D3D11 / Vulkan are bridge-only and
  unvalidated for FG → **Experimental** in the matrix.
- A temporal upscaler enabled — FG is fed from it (`FGInput=upscaler`).
- Windows **Hardware-accelerated GPU Scheduling** on.
- An FG provider and its DLLs — one of:
  - `FGOutput=fsrfg` → `amd_fidelityfx_dx12.dll` (any RTX)
  - `FGOutput=xefg` → `libxess_fg.dll` + `libxell.dll` (any RTX)
  - `FGOutput=dlssg` + `FGNvngxReplacement=None` → Streamline 2.14.1 + `nvngx_dlssg.dll` (RTX 40 / 50)
  - `FGNvngxReplacement=Nukems | Arturs | FFX | Combo` → an FSR replacement DLL (the only FG path on RTX 30 / 20)
  - full menu-label ↔ INI ↔ DLL mapping: [Frame Generation provider reference](#frame-generation-provider-reference)
- A Reflex / low-latency path — OptiScaler's fakenvapi / XeLL provides one if the game lacks it.

Legend: **Yes · Bridge · Experimental · N/A** — see [Matrix legend](#matrix-legend).

| Game API | RTX 50 (Blackwell) | RTX 40 (Ada) | RTX 30 (Ampere) |
|---|---|---|---|
| **DX9** | **N/A** ([§7](#7-caveats)) | **N/A** | **N/A** |
| **DX11** | **Experimental** — NR + OptiFG through `with_dx12`; FSR / XeSS output only. | **Experimental** — same. | **Experimental** — FSR replacement output only. |
| **DX12** | **Yes** — NR + any FG output. Native DLSS-G (stock `nvngx_dlssg.dll`) or FSR / XeSS. Tested: KCD2, DLSS Q + NR + DLSS FG, SDR. | **Yes** — native DLSS-G 2× on Ada, or FSR / XeSS. | **Yes**, but **no NVIDIA FG on RTX 30 / 20** — use `FGOutput=fsrfg` or an `FGNvngxReplacement` ([§7](#7-caveats)). |
| **Vulkan (native)** | **Experimental** — FSR-FG has partial native-Vulkan support; DLSS-G on Vulkan is limited. | **Experimental** — same. | **Experimental** — FSR replacement only. |
| **Vulkan (D3D12 bridge)** | **Experimental** — NR + OptiFG via the bridge. | **Experimental** — same. | **Experimental** — FSR replacement only. |

Scenario-wide notes (not cell-specific, so they stay here rather than [§7](#7-caveats)):

- SDR only on the tested DLSS-G route; KCD2's FP16 / scRGB HDR is unsupported by that path.
- `[FrameGen] External=true` hands FG to the game or an external unlocker; NR and NGX upscaling
  stay available, OptiScaler's Streamline interception / Reflex pacing / multiplier overrides
  switch off. Startup-only.
- **ResidualFG** (`DeferredDLSS=true` + `ResidualFG=true`) is a separate NR experiment — half-rate
  NR with NVIDIA interpolation for the in-between frame. D3D12 or D3D11 bridge only, not native
  Vulkan, not RR, off by default.

→ File placement: [§5](#5-where-each-file-goes) · Overlay / INI: [§6](#6-overlay-settings-and-their-ini-keys) · Caveats: [§7](#7-caveats)

---

## 4. DLSS-NR with Multi-Frame Generation

MFG is Frame Generation that emits more than one interpolated frame (3× / 4× …). Everything in
§3 applies; only the FG runtime changes.

`[DLSSG] InterpolationCount` is the number of **generated** frames, so the total multiplier is
`InterpolationCount + 1` (`2` → 3×, `3` → 4×, `5` → 6×). The config accepts `1`–`6`; the
runtime clamps to what it actually advertises, so treat `5` (6×) as the practical ceiling.

**Builds on:** [§3](#3-dlss-nr-with-frame-generation) (itself §1 + Frame Generation).

Extra requirements on top of §3:

- An FG runtime that can emit more than one generated frame — pick a path from the
  **MFG paths** table below the matrix.

Legend: **Yes · Bridge · Experimental · N/A** — see [Matrix legend](#matrix-legend).

| Game API | RTX 50 (Blackwell) | RTX 40 (Ada) | RTX 30 (Ampere) |
|---|---|---|---|
| **DX9** | **N/A** ([§7](#7-caveats)) | **N/A** | **N/A** |
| **DX11** | **Experimental** — FSR MFG replacement through the bridge only. | **Experimental** — Ada unlock not bridge-validated; FSR MFG replacement only. | **Experimental** — FSR MFG replacement only. |
| **DX12** | **Yes** — NR + native MFG (`InterpolationCount` `2`–`5`, runtime-clamped). | **Yes** — via FSR-based MFG, no unlock ([§6](#nr-with-fsr-based-mfg)). Native NVIDIA MFG only through the experimental Ada unlock ([§6](#nr-with-mfg-rtx-40-unlock), [§7](#7-caveats)). | **Yes** — via FSR-based MFG ([§6](#nr-with-fsr-based-mfg)); no NVIDIA MFG on RTX 30 / 20 ([§7](#7-caveats)). |
| **Vulkan (native)** | **Experimental** — MFG unvalidated on native Vulkan. | **Experimental** — same, plus the Ada unlock unverified. | **Experimental** — FSR MFG replacement only. |
| **Vulkan (D3D12 bridge)** | **Experimental** | **Experimental** | **Experimental** — FSR MFG replacement only. |

**MFG paths:**

| Path | What it needs | Overlay / INI |
|---|---|---|
| RTX 50 native MFG | Stock `nvngx_dlssg.dll` 310.9+. | [§6](#nr-with-mfg-rtx-50-native) |
| RTX 40 MFG — experimental unlock | `AdaMfgUnlock=true` (in-memory patch of `nvngx_dlssg.dll`, ceiling `InterpolationCount` 5 = 6×) **or** `External=true` + Dashdogy's RTX40MFG-Unlock ASI. Neither is verified on RTX 40 hardware in this fork. | [§6](#nr-with-mfg-rtx-40-unlock) |
| FSR-based MFG — any RTX | `FGNvngxReplacement=Arturs` (FSR 3 MFG) or `Combo` (FFX + Enabler, 4× / 6×). No unlock. The only MFG path on RTX 30 / 20. | [§6](#nr-with-fsr-based-mfg) |

Scenario-wide note: the Reflex FPS limit caps **total** output including generated frames — a
150 FPS limit at 4× throttles rendering to ~37.5 FPS. Raising the multiplier does not raise the
cap; set the limit to 0 to test uncapped.

→ File placement: [§5](#5-where-each-file-goes) · Overlay / INI: [§6](#6-overlay-settings-and-their-ini-keys) · Caveats: [§7](#7-caveats)

---

## 5. Where each file goes

Standard install: `setup_windows.bat` renames `OptiScaler.dll` to a proxy name (`dxgi.dll`
usual; `dbghelp.dll` for Cyberpunk). **"Beside OptiScaler"** below means the folder that renamed
DLL loads from — the game's executable folder in a standard install. The release also drops an
`OptiScaler/` backend folder there (`[Libraries] OptiDllPath`, default `.\OptiScaler\`); the
Streamline set nests inside it as `OptiScaler/streamline/`.

| File | Where you get it | Put it | Needed for |
|---|---|---|---|
| `dxgi.dll` (renamed `OptiScaler.dll`) + `OptiScaler.ini` + `OptiScaler/` backend folder | this fork's release archive | beside OptiScaler | OptiScaler itself |
| `nvngx.dll_dlssnr.dll` | release archive (this fork builds it) | beside OptiScaler | NR forwarder shim — **always required for NR** |
| `nvngx_dlssnr.dll` (310.8) | you supply — **RTX 50**: original NVIDIA-signed; **RTX 40 / 30 / 20**: ShortFuse cross-generation runtime | beside OptiScaler | the NR model — **always required for NR** |
| `nvngx_dlss.dll` | you supply — DLSS SR runtime | beside OptiScaler | `DeferredDLSS` mode only |
| `nvngx_dlssd.dll` | you supply — DLSS RR runtime | beside OptiScaler | NR **+ Ray Reconstruction** |
| `amd_fidelityfx_dx12.dll` (± `amd_fidelityfx_loader_dx12.dll` / `amd_fidelityfx_framegeneration_dx12.dll`) | FSR SDK / OptiScaler optional deps | beside OptiScaler | `FGOutput=fsrfg`, and `FGNvngxReplacement=FFX` / `Combo` |
| `libxess_fg.dll` + `libxell.dll` | Intel XeSS SDK | beside OptiScaler | `FGOutput=xefg` |
| `streamline/` set — `sl.interposer.dll`, `sl.common.dll`, `sl.dlss_g.dll`, `sl.reflex.dll`, `sl.pcl.dll` + `nvngx_dlssg.dll` | `get_streamline.ps1` (Streamline 2.14.1) — see `docs/DLSS-FRAME-GENERATION.md` | `OptiScaler/streamline/` (i.e. `<OptiDllPath>/streamline/`) | `FGOutput=dlssg` — **real NVIDIA FG / MFG** (RTX 40 / 50) |
| `dlssg_to_fsr3_amd_is_better.dll` | Nukem's dlssg-to-fsr3 mod | beside OptiScaler | `FGNvngxReplacement=Nukems` (FSR 3 FG, 2×) |
| `dlss-enabler-headless.dll` | DLSS Enabler | beside OptiScaler | `FGNvngxReplacement=Arturs` / `Combo` (FSR **MFG**) |
| RTX40MFG-Unlock ASI + its loader | Dashdogy's RTX40MFG-Unlock (`build_mfg_unlocker.ps1`) — see `docs/RTX40-MFG.md` | its **own** early ASI loader, kept separate from OptiScaler / ReShade | `[FrameGen] External=true` → RTX 40 MFG owned outside OptiScaler |

The FG-provider files map to menu labels and INI values in the
[Frame Generation provider reference](#frame-generation-provider-reference). The three
similarly-named NGX DLLs — `nvngx_dlssnr.dll` (NR model), `nvngx_dlss.dll` (SR),
`nvngx_dlssd.dll` (RR) — are different runtimes and all user-supplied; `nvngx.dll_dlssnr.dll`
is this fork's forwarder, not any of them (see [Key concepts](#key-concepts)).

---

## 6. Overlay settings and their INI keys

Overlay opens with **Insert**. `[FrameGen]` and `[DLSSG]` changes are **startup-only** — Save
Settings, restart. `[DlssNr]` changes apply live.

### Frame Generation provider reference

One place to map an overlay dropdown to its INI value to its DLL. The **FG Nvngx Replacement**
dropdown appears when **FG Output** = `DLSSG`.

| Overlay label | INI (`[FrameGen] FGNvngxReplacement=`) | DLL, beside OptiScaler | Gives you |
|---|---|---|---|
| `None (Real DLSSG)` | `None` | `streamline/` set + `nvngx_dlssg.dll` | real NVIDIA FG / MFG (RTX 40 / 50) |
| `Nukem's` | `Nukems` | `dlssg_to_fsr3_amd_is_better.dll` | FSR 3 FG, 2× |
| `Enabler` | `Arturs` | `dlss-enabler-headless.dll` | FSR 3 MFG |
| `FSR 3/4 FG` | `FFX` | `amd_fidelityfx_dx12.dll` | FSR 3 / 4 FG via FFX |
| `FFX + Enabler` | `Combo` | `amd_fidelityfx_dx12.dll` + `dlss-enabler-headless.dll` | FFX for the middle frame, Enabler for the rest — 4× / 6× |

Non-replacement outputs: **FG Output** = `FSR FG` → INI `FGOutput=fsrfg`; `XeFG` → `FGOutput=xefg`.

### NR post-SR (baseline placement)
- Overlay: **DLSS Neural Rendering → ☑ Enable Neural Rendering**; leave *Apply before Super Resolution* off.
- INI: `[DlssNr] Enabled=true`

### NR before SR (pre-SR multipass)
- Overlay: **DLSS Neural Rendering → ☑ Apply before Super Resolution**; **Model passes** = 1–3.
- INI: `[DlssNr] RunBeforeSR=true` · `Passes=1`

### NR model precision (RTX 50 hybrid)
- Overlay: **DLSS Neural Rendering → Model precision** = `NVIDIA` (default) or `Experimental (FP8+NVFP4 hybrid)`.
- INI: `[DlssNr] Precision=0` (NVIDIA FP8) · `Precision=4` (hybrid — constraints: [§7](#7-caveats)).

### NR with DeferredDLSS
- Overlay: **DLSS Neural Rendering → ☑ Generate before SR, apply after SR (DLSS)**.
- Files: add `nvngx_dlss.dll`. Constraints: [§7](#7-caveats).
- INI: `[DlssNr] DeferredDLSS=true`

### NR with Ray Reconstruction
- No NR toggle — NR detects the RR feature and runs after RR+SR automatically. Keep *Apply before Super Resolution* **off**.
- Files: add `nvngx_dlssd.dll` ([§5](#5-where-each-file-goes)); the game's Ray Reconstruction option must be on.
- INI: `[DlssNr] Enabled=true` · `RunBeforeSR=false`

### NR with FSR Frame Generation
- Files: `amd_fidelityfx_dx12.dll` beside OptiScaler.
- Overlay: **Frame Generation → FG Input** = `OptiFG (Upscaler)` · **FG Output** = `FSR FG` → Save, restart.
- INI:
  ```ini
  [FrameGen]
  Enabled=true
  FGInput=upscaler
  FGOutput=fsrfg
  ```

### NR with real NVIDIA DLSS-G
- RTX 40 / 50 only. Files: `OptiScaler/streamline/` set + `nvngx_dlssg.dll` ([§5](#5-where-each-file-goes)).
- Overlay: **Frame Generation → FG Input** = `OptiFG (Upscaler)` · **FG Output** = `DLSSG` · **FG Nvngx Replacement** = `None (Real DLSSG)` → Save, restart.
- INI:
  ```ini
  [FrameGen]
  Enabled=true
  FGInput=upscaler
  FGOutput=dlssg
  FGNvngxReplacement=None
  [DLSSG]
  InterpolationCount=1
  ```

### NR with FSR replacement FG
- The FG path on RTX 30 / 20 (works on any RTX). Files: one FSR replacement DLL beside OptiScaler.
- Overlay: as [real NVIDIA DLSS-G](#nr-with-real-nvidia-dlss-g) above, but set **FG Nvngx Replacement** — the [provider reference](#frame-generation-provider-reference) maps each label to its INI value, DLL and multiplier.
- INI: `FGNvngxReplacement=Nukems` | `Arturs` | `FFX` | `Combo`.

### NR with MFG, RTX 50 native
- Set up [real NVIDIA DLSS-G](#nr-with-real-nvidia-dlss-g) above, then pick the multiplier.
- Overlay: **Frame Generation → Override DLSSG Ratio** = `3X` / `4X` … (or ☑ **Force Dynamic MFG** + **DMFG FPS Target**).
- INI: `[DLSSG] InterpolationCount=2` (3×) · `3` (4×) · up to `5` (6×), runtime permitting. Dynamic: `[DLSSG] ForceDMFG=true` · `FramerateTargetDMFG=0`.

### NR with MFG, RTX 40 unlock
- Overlay: **Frame Generation → ☑ Built-in RTX 40 MFG unlock (experimental; restart)**, then set the multiplier as for RTX 50. The overlay reports `capability matched / validation matched` when the patch lands.
- INI: `[DLSSG] AdaMfgUnlock=true` · `AdaBlackwellKernels=auto` · `InterpolationCount=2`…`5` (3×–6×).
- Alternative, owned outside OptiScaler: **☑ External frame generation / MFG unlocker** → `[FrameGen] External=true`, then set the multiplier in Dashdogy's unlocker.
- Without any unlock: FSR-based MFG works on RTX 40 too — see [NR with FSR-based MFG](#nr-with-fsr-based-mfg).

### NR with FSR-based MFG
- Works on any RTX, no unlock. On RTX 30 / 20 it is the **only** MFG path ([§7](#7-caveats)).
- Files: `dlss-enabler-headless.dll` (+ `amd_fidelityfx_dx12.dll` for `Combo`) beside OptiScaler.
- Overlay: as [real NVIDIA DLSS-G](#nr-with-real-nvidia-dlss-g) above, but set **FG Nvngx Replacement** to `Enabler` or `FFX + Enabler` — see the [provider reference](#frame-generation-provider-reference).
- INI: `FGNvngxReplacement=Arturs` | `Combo`. The multiplier is set by that provider, not `[DLSSG] InterpolationCount`.

---

## 7. Caveats

The single authoritative statement of each constraint a matrix **cell** tags. Scenario-wide
notes that aren't cell-specific stay in their own section ([§3](#3-dlss-nr-with-frame-generation),
[§4](#4-dlss-nr-with-multi-frame-generation)).

- **RTX 30 / 20 fragility.** `docs/NR-COMPATIBILITY.md` records NR crashes on RTX 30 / 20
  (Onimusha / RE Engine, reported upstream, a *candidate* fix only, not reproduced in-house).
  First test on these cards: one model pass, FG off, DLSS→NR, and check loading / fast-travel.
  Ray Reconstruction adds further GPU cost on these cards on top of NR. Matrix cells marked
  "fragile" in the RTX 30 column point here.
- **RTX 30 / 20 need an FSR replacement FG provider.** The unmodified NVIDIA runtime gives no
  FG or MFG on these cards. Use `FGOutput=fsrfg`, or `FGNvngxReplacement=Nukems` (2×) /
  `Arturs` / `Combo` (MFG).
- **RTX 40 MFG unlock is experimental.** `[DLSSG] AdaMfgUnlock=true` (an in-memory patch of
  `nvngx_dlssg.dll`) and the external RTX40MFG-Unlock ASI both open Multi-Frame Generation on
  Ada, but neither is verified on RTX 40 hardware in this fork. Do not run either on RTX 30 / 20
  — they are Ada-only.
- **DeferredDLSS** (`[DlssNr] DeferredDLSS=true`) computes NR at render resolution, upscales its
  *change* with a private DLSS pass, and applies it after SR. It needs a working `nvngx_dlss.dll`,
  is **D3D12 (or a D3D12 bridge) only** — not native Vulkan — and is **hard-disabled when Ray
  Reconstruction is active** (`DlssNr_Dx12.cpp:2902`, `DlssNrFeature_Vk.cpp:527`). It overrides
  *Apply before Super Resolution*; disable Hold frame, Compare and Debug view.
- **Pre-RR NR** (`RunBeforeSR=true` while RR is active) has a code path (`"before RR+SR"`) but
  `OptiScaler/dlssnr/design/pre-sr-multipass.md` makes after-RR the intended contract. Treat it
  as experimental.
- **Hybrid precision** (`[DlssNr] Precision=4`) is RTX 50 only, needs `OptiScaler/nvfp4/hybrid`
  assets that are not in the repo, and is an optional performance path — never required for NR.
- **DX9** — OptiScaler has no Direct3D 9 device hooks, and no DX9-era game ships a temporal
  upscaler to intercept. A third-party DX9→DX12 / Vulkan wrapper plus an upscaler mod is the only
  theoretical route and is out of scope here.
- **DX11 is bridge-only for NR.** Native pure-D3D11 FSR2 / XeSS backends do not carry the NR
  pass; a DX11 game must run through OptiScaler's `with_dx12` bridge.
- **"Yes" is not a quality guarantee.** It means a code path exists and has passed the fork's
  smoke tests or limited local game testing — not that a given title is artefact-free.

---

## Sources

`INSTALL-DLSSNR.md` (repo root), `OptiScaler/dlssnr/README.md`,
`OptiScaler/dlssnr/design/pre-sr-multipass.md`, `docs/DEFERRED-NR-DLSS.md`,
`docs/NR-COMPATIBILITY.md`, `docs/DLSS-FRAME-GENERATION.md`, `docs/RTX40-MFG.md`,
`docs/RESIDUAL-FG-PROTOTYPE.md`, `OptiScaler/framegen/dlssg/MfgUnlock.h`, `OptiScaler.ini`,
and the call sites in `OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp` /
`OptiScaler/dlssnr/DlssNrFeature_Vk.cpp`.
