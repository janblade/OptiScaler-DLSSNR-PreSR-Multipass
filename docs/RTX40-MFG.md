# RTX 40 MFG unlocker (optional)

This fork can leave frame generation to [Dashdogy's RTX40MFG-Unlock](https://github.com/dashdogy/RTX40MFG-Unlock).
The unlocker is separate, MIT-licensed work by Michael Robles. Its source is pinned as a git submodule
at `4e776d068f91b4a665425542bb005dd57cc3d891`. No NVIDIA FG binary is bundled or patched on disk by us.
This integration has been built, but has not been verified on RTX 40 hardware. It is not an RTX 30
FG unlocker and does not add native DLSS FG to games which lack it.

## Use with OptiScaler

1. Back up your working setup. Close the game.
2. Install the unlocker using its own README. Keep its loader separate from OptiScaler and ReShade.
   Do not overwrite an existing `dxgi.dll`, `version.dll`, `dinput8.dll`, or other proxy owned by a mod.
   The ASI must load before the first FG pipeline; use the upstream-recommended early ASI loader.
   Do not rely on OptiScaler's late ASI-plugin loading for this.
3. In `OptiScaler.ini`, set `[FrameGen] External=true`. Or select **External frame generation / MFG
   unlocker** in the overlay, Save Settings, then restart.
4. Enable native DLSS FG in the game. Select the multiplier in the unlocker/game, not OptiScaler.
5. Start at 2x, then try 3x. Confirm the active provider and applied multiplier in the unlocker log,
   not just the requested setting or an FPS counter.

External mode disables OptiScaler's Streamline interception, NVIDIA API overrides (including Reflex,
flip metering and driver-preset interception), multiplier overrides and replacement FG routing.
Streamline/FG DLL loads also pass through to the original loader. NR and NGX upscaling remain available.
It is a startup option: switching ownership without restarting is unsafe. Your saved OptiScaler FG
settings are retained and return on a later startup with `External=false`.
Use the game's/driver's FPS limiter in this mode; OptiScaler does not own Reflex pacing.
Do not use this mode when you need OptiScaler to replace DLSSG with FSR FG on an RTX 30 card.

Keep the game's working Streamline and NVIDIA runtime DLLs. Do not copy a second Streamline stack
from another game's NR/FG package. Disable competing NR injectors when testing this fork.

For a frozen image above 2x, upstream documents FG Preset B as a reported workaround in some games;
Cyberpunk recovery was not separately confirmed. See the upstream README for current limitations.
No security exclusions or disabled antivirus are required. Use single-player games without anti-cheat.

## Build the optional unlocker

Install VS 2022 C++ Build Tools and CMake 3.24+. From this repository:

```powershell
git submodule update --init external/RTX40MFG-Unlock
.\build_mfg_unlocker.ps1
```

If CMake isn't on PATH, pass `-CMake 'C:\path\to\cmake.exe'`.
This builds the core DLL and ASI into a separate `release/mfg-optional-*` folder. It does not install
anything into a game or include the unlocker in normal OptiScaler releases.
The core defaults to following the game. To build the optional ReShade control panel too, provide
`-ReShadeRoot` and `-ImGuiRoot` pointing to matching source trees as described by upstream.
Without the panel, use the game's multiplier or the upstream JSON configuration mechanism.
For example, with the game closed, merge these keys into `RTX40MFG-Universal.json` beside the real
game executable (preserve any other keys):

```json
{"followGame": false, "mode": "fixed", "multiplier": 3, "dynamicTargetFrameRate": 0, "dynamicExperimental56": false}
```

If the legacy CET `plugins/cyber_engine_tweaks/mods/RTX40MFG/init.lua` exists, the universal JSON
lives in that mod's folder instead. `RTX40_MFG_CONFIG_PATH`, if set, overrides both locations.
The unlocker may limit the request to a supported multiplier; inspect its log.

For a report, include the GPU, driver, game and unlocker versions, loader filenames, `OptiScaler.ini`,
`OptiScaler.log` and `%TEMP%\MfgUnlock-<PID>.log`. Remove personal paths before posting publicly.

## Built-in unlock options (the alternative route)

The overlay's **Built-in RTX 40 MFG unlock** (`[DLSSG] AdaMfgUnlock`) is the other way to get MFG on RTX 40. It patches the DLSS-G module and the Streamline plugin in memory only, and no file on disk is changed. It is experimental and off by default. Use this route or the external unlocker above, never both: set `[FrameGen] External=false`, save and restart.

Everything below is applied at load, so Save Settings and restart after changing it. The options sit under **RTX 40 (Ada) MFG Unlock Options** in the overlay, which is drawn only while the unlock is on.

- **Provider discovery.** The DLSS-G module is found by the game's `nvngx_dlssg.dll`, by the driver's OTA store (`models\dlssg\...\<hash>.bin`), and, for a renamed snippet, by a rate-limited walk of the loaded modules. Finding a module never patches it: the signatures decide.
- **Plugin frame ceiling.** Streamline's `sl.dlss_g` lowers its compiled maximum to a device value its wrapper cached. A wrapper that cached 1 then rejects 3X and 4X with `sl::Result` 38. Once the snippet unlock has landed, the one-byte clamp is neutralised. The compiled maximum stays as a hard bound. The source fork applies this only together with its flip-metering option; here it applies whenever the unlock has landed.
- **Frame timing fix** (`AdaTemporalFix = auto | retarget | ptx`). Above 2X every generated frame can land at the midpoint between two real frames. `auto` and `retarget` reuse the Blackwell interpolation kernel the module carries. `ptx` rewrites the Ada kernel's PTX so each frame is blended at its own time, and only works on the DLSS-G builds it has an exact profile for. `AdaBlackwellKernels=false` in an older ini still leaves the unlock unapplied while `AdaTemporalFix` is `auto`.
- **Software frame pacing** (`AdaFlipMeteringPatch`, default false). Only for a freeze at 3X or more. It edits NVIDIA's plugin in memory, when it loads, so that it takes its own software-pacing fallback, and refuses unless the plugin's code is of the shape it recognises. Try `[NvApi] DisableFlipMetering=true` first, which is milder and ini-only.

The overlay reports the result directly under each control, and a line under **Override DLSSG Ratio** shows what the game asked for, what was sent, and what Streamline says it presented. That line is the check the older advice asked for: confirm the presented count follows the ratio, not just that an FPS counter rose.

### What was checked, and what was not

Real modules were mapped (never run) and searched: `nvngx_dlssg.dll` 310.9 and 310.8, and `sl.dlss_g.dll` 2.13.0.0. Both frame-count gates match exactly once in both snippet builds. The PTX rewrite finds and rebuilds its kernel in both (8 descriptors each). The plugin's frame-count clamp is found once, with a compiled maximum of 5. The flip-metering state is derived from the plugin: context +0x44F0, value 1, one register store to rewrite. `tests/mfg_real_module_check.cpp` reports the same for any build you give it, without modifying the file. Run it on a new DLSS-G build to see whether the patches still find their targets.

**Not run in a game and not on RTX 40 hardware.** Nobody here has confirmed that intermediate frames advance, that the plugin ceiling patch removes the `sl::Result` 38 rejection, or that software pacing ends a freeze. Test moving scenes at 2X then 3X, read the presented count in the overlay, and report the GPU, driver, game, the DLSS-G version shown in the overlay status line, and `OptiScaler.log`.