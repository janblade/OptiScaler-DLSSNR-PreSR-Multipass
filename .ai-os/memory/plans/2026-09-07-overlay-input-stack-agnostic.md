# Plan: Input-stack-agnostic overlay input

- Branch: fix/overlay-input-stack-agnostic  (off main; PR back — see Context)
- Created: 2026-09-07
- Status: done
- Task file: memory/tasks/main.md
- Reviewed: 2026-09-07 by independent Plan agent — findings folded into steps below (v2).

## Context

**The problem (general):** OptiScaler's overlay assumes input arrives via window messages
(`WM_INPUT` / WndProc). Games that read input through DirectInput device reads
(`GetDeviceState` / `GetDeviceData`) or raw-input polling (`GetRawInputData` /
`GetRawInputBuffer`) bypass that path entirely, so the overlay goes blind (clicks lost,
shortcut double-fires) and its blocking is all-or-nothing (game uncontrollable with the
menu up, Alt+F4 swallowed). Assetto Corsa + CSP is the first validated case; the same fix
applies to any title with that input stack (many sims, custom-input engines, bundled
wheel/HOTAS setups).

**What it does:** make the overlay capture and arbitrate mouse/keyboard/gamepad input
regardless of which API the game uses to acquire it. The complete change already exists as
commit `ae2a10af` on the unmerged one-commit branch `dlss-neural-rendering`; `main` has
since advanced 109 commits and refactored the input-blocking layer, so this is a
**re-implementation against the current structure**, not a cherry-pick (a plain pick
conflicts in xinput / directinput / messages).

**Acceptance criteria** (from `ae2a10af`'s own scope):
1. In a game that reads mouse via DirectInput / raw input with no `WM_INPUT` reaching our
   hooks (validated on Assetto Corsa + CSP), overlay mouse **clicks register** — including a
   click whose press and release land in the same frame.
2. One physical tap of the menu shortcut **toggles the menu once**, not twice.
3. With the menu open, **game keyboard/mouse bindings still work** when the cursor is not
   over a menu window and no ImGui text field is focused (ReShade-style conditional
   blocking); the OS cursor still stays put.
4. Gamepads / wheels stay **fully blocked** during menu navigation regardless of (3), and
   the menu is still navigable by controller; closing the menu leaves no stuck stick/button.
5. **Alt+F4 closes the game** while the menu is open.
6. No regression in a normal `WM_INPUT` game (menu still works; no doubled wheel/delta/click;
   no post-close input burst).

**Out of scope:** the DLSS-NR pre-SR / jitter / resolution investigation; any other input
behaviour; touching `dlss-neural-rendering` (leave it; this supersedes it on main).

**Branch:** `fix/overlay-input-stack-agnostic`, cut from `main`, normal PR back into `main`
(`main` is protected/rolling — no direct commits).

**Verification reality:** no automated test suite (genome `has_tests:false`). Every
behaviour step = clean Debug+Release x64 build + manual in-game. User has Assetto Corsa
under Steam; use NBA 2K26 as the `WM_INPUT` regression title.

## Current-structure notes (verified by review, 2026-09-07)

- Blocking is a helper layer in `input_system.cpp:60-66`:
  `ShouldApplyBlockingPolicyLocked()` = `bypassHookDepth==0 && MenuVisible && Focused`;
  `ShouldBlock{Keyboard,Mouse,Cursor}InputLocked()` = policy `&& _state.Block{Mouse,…}`.
- `ShouldBlockDirectInputOtherLocked()` (`input_system_directinput.cpp:69-72`) and
  `ShouldBlockXInputLocked()` (`input_system_xinput.cpp:31-34`) currently read
  `_state.Initialized && (ShouldBlockKeyboardInputLocked() || ShouldBlockMouseInputLocked())`
  — **no explicit `_state.Focused` prefix** (folded into the helpers). The gamepad split
  replaces the `|| ` combo with `ShouldBlockGamepadInputLocked()`, so each becomes
  `_state.Initialized && ShouldBlockGamepadInputLocked()` and nothing more.
  `ShouldBlockDirectInputMouseLocked` / `…KeyboardLocked` (`directinput.cpp:59-67`) are
  correctly left alone (they follow the conditional mouse/keyboard block).
  Non-pointer-device blocking is enumerated in exactly those two places — HID
  (`input_system_hid.cpp:265`) is mouse-only, GameInput does no blocking.
- `input_system.cpp` literals to change: `_state.BlockMouse = visible;` /
  `BlockKeyboard = visible;` in `ApplyMenuVisibilityChangeLocked` (fn @731, lines 736-738);
  5 hardcoded `io.AddMouseButtonEvent(0..4, …Down)` in `FeedImGui` (fn @1183, lines
  1287-1291); reset block in `ResetStateAfterShutdown` (fn @873, lines 919-921); diagnostics
  snapshot `state.BlockMouse = _state.BlockMouse; …` in `GetDebugState` (fn @1451, lines
  1477-1479). `LOG_INFO` at `input_system.cpp:742` enumerates the three flag names in a
  format string — update for log parity (nit).
- `ApplyMenuVisibilityChangeLocked` is called only from `EndFrame` (`input_system.cpp:1327`)
  → `MenuCommon::RenderMenu` (`menu_common.cpp:7888`), which runs **after**
  `FeedImGui` + `ImGui::NewFrame()` (`menu_common.cpp:1647-1650`). So `io.WantCaptureMouse`
  / `WantCaptureKeyboard` are valid there (last-frame hover/active state, one-frame lag,
  ReShade-identical). Precedent for touching ImGui under `_state.Mutex` here:
  `input_system.cpp:748` (`ImGui::GetIO()` guarded by `GetCurrentContext() != nullptr`).
- `DebugState` struct is in **`input_system.h:30`** (`BlockMouse/Keyboard/Cursor` at 52-54);
  `GetDebugState()`'s only other reference is `input_system.h:211` — no renderer consumes
  a new field, just add it to the struct.
- `input_system_messages.cpp`: `WM_KEYDOWN`/`WM_SYSKEYDOWN` share one block (577-586);
  `shouldBlock = blockKeyboard;` at **line 584**; local `blockKeyboard` from
  `ShouldBlockKeyboardInputLocked()` at line 473; `vk` at 580 is
  `NormalizeModifierVirtualKey(wParam,lParam)` (returns `VK_F4` unchanged).
- `input_system_raw.cpp`: `UpdateStateFromRawInputLocked` @638; `HandleRawInputLocked` @664
  (calls it @718, takes `decision` **by value** @705); `hkGetRawInputData` @812 (decision
  by value @840); `hkGetRawInputBuffer` @853. Insert feed calls right after
  `RecordRawInputSanitizeCounterLocked` / before `ApplyRawInputSanitizeActionLocked`
  (~845-846 / ~899-900).
- `_state.RawInputSanitizeCache` = `std::array<RawInputSanitizeDecision, 128>` **ring
  buffer** (`input_system_internal.h:201-202`), linear-scanned by `Handle`, reset each
  frame by `ResetRawInputSanitizeCacheLocked()` via `ClearTransientState`
  (`messages.cpp:686-714`, `raw.cpp:251-254`). `TryConsumeRawInputStateLocked` must walk
  `_state.RawInputSanitizeCache` **directly** (mutate the live entry, not the by-value
  copy) and must run *after* `GetRawInputSanitizeDecisionLocked` has populated that handle's
  entry (both call sites already do). Risks to note: >128 distinct handles in one frame
  defeats dedup (ring overwrite → double feed); cache reset runs on the render thread while
  raw reads run on the game thread, so a packet straddling `EndFrame` feeds twice — one
  extra idempotent button *level* event.
- `hkDirectInputGetDeviceData` blocking path (`directinput.cpp:965-985`) does **NOT**
  zero-and-return — it calls `original(device, objectDataSize, nullptr, &INFINITE, 0)` to
  drain the device event queue so menu-time events don't replay after close. This MUST be
  preserved. Trampolines are resolved **per device** from `*(PVOID**)device` →
  `vtable[9]` (GetDeviceState) / `vtable[10]` (GetDeviceData) →
  `ResolveDirectInputMethodTrampolineLocked` into a local `original`
  (`directinput.cpp:919-924`, `958-961`) — do **not** use the `o_DirectInputDevice*`
  globals (`directinput.cpp:202-203`; first-device only). `hkDirectInputGetDeviceState`
  is immediate-state (no queue) — zero-and-return is fine there.
- `menu_common.cpp` `CheckShortcut` lambda @263 fires directly on `IsKeyReleased`;
  `const auto currentTick = GetTickCount64();` at line 280 (after the lambda — step moves it
  above); `lastShortcutFireTick` insertion point line 67; `OptiInput::IsKeyPressed` exists
  (`input_system.cpp:1355`). One shared `lastShortcutFireTick` now also gates the
  `inputDlssNr` shortcut (`menu_common.cpp:290`, post-`ae2a10af`) → any fire suppresses all
  shortcuts for 250 ms. Acceptable; note it.
- `input_system_internal.h`: `RawInputSanitizeDecision` @51-56, `InputState` Block flags
  @132-134, sibling helper decls @534-537.

## Steps

1. [x] **Confirmation note (no code).** Reviewer verified `82eb45fb`, `5741f9a2`,
   `8030ecca`, `10aa2053`, `26c1e8bb` are all already ancestors of `main` (baseline, dated
   Sep 2-5) and **none** implements the conditional block, Alt+F4 pass, DI/raw overlay
   feed, or shortcut debounce. What they *did* add and the port must respect:
   `mustReachGame` in the `WM_INPUT` case; `DrainXInputKeystrokesLocked`; per-device DI
   `vtable[9]/[10]` trampoline resolution + the `INFINITE` buffered-data flush +
   `DrainDirectInputBufferedDataLocked` (`directinput.cpp:691-713`). — verify: n/a
   (already done); recorded here.

2. [x] **Struct fields.**
   - `input_system_internal.h`: `bool BlockGamepad = false;` in `InputState` (by
     `BlockCursor`); `bool StateConsumed = false;` in `RawInputSanitizeDecision` (with the
     "overlay consumed this packet" comment).
   - `input_system.h`: `bool BlockGamepad = false;` in `struct DebugState` (by
     `BlockCursor`, line ~54).
   — verify: Debug x64 compiles.

3. [x] **`input_system.cpp`** — add
   `bool ShouldBlockGamepadInputLocked() { return ShouldApplyBlockingPolicyLocked() && _state.BlockGamepad; }`
   beside the other three helpers (~62-66); declare it in `input_system_internal.h`
   (~535, by the siblings). — verify: compiles.

4. [x] **`input_system.cpp` `ApplyMenuVisibilityChangeLocked`** — replace
   `_state.BlockMouse = visible; _state.BlockKeyboard = visible;` with: default
   `wantMouse = wantKeyboard = visible`, and when
   `visible && ImGui::GetCurrentContext() != nullptr` override from `io.WantCaptureMouse`
   and `io.WantCaptureKeyboard || io.WantTextInput`; keep `BlockCursor = visible`; add
   `BlockGamepad = visible`. Port the comment; update the `LOG_INFO` at line ~742 to
   include `BlockGamepad`. — verify: compiles; **manual** — (a) menu text fields still
   receive typing, (b) menu nav (arrows/Tab/click on a menu window) works, (c) menu open +
   cursor over game viewport + no ImGui text focus → game movement keys and mouse-look
   still reach the game, (d) cursor over a menu window → game gets neither; (e) OS cursor
   still parks/virtualizes on open.

5. [x] **`input_system.cpp`** — `ResetStateAfterShutdown` (~919) sets
   `_state.BlockGamepad = false;`; `GetDebugState` snapshot (~1479) mirrors
   `state.BlockGamepad = _state.BlockGamepad;` (struct field added in step 2). — verify:
   compiles.

6. [x] **`input_system.cpp` `FeedImGui`** — replace the 5 literal
   `io.AddMouseButtonEvent(n, _state.MouseButtons[n].Down)` lines with the
   `for (mb < _state.MouseButtons.size())` loop feeding
   `_state.MouseButtons[mb].Down || _state.MouseButtons[mb].Pressed`; port the press-edge
   comment. — verify: compiles; manual — a fast click inside the menu registers.

7. [x] **Gamepad split** — `input_system_xinput.cpp` `ShouldBlockXInputLocked` and
   `input_system_directinput.cpp` `ShouldBlockDirectInputOtherLocked` become
   `return _state.Initialized && ShouldBlockGamepadInputLocked();` (drop the
   `Keyboard || Mouse` combo). Port the comments. — verify: compiles; **manual** with a
   controller — (a) pad fully neutralised in-game while the menu is open, even with the
   cursor outside menu windows; (b) the menu is still navigable by controller (ImGui nav is
   fed from `FeedImGui`, not the XInput passthrough); (c) closing the menu leaves no stuck
   stick/button in the game (existing `DrainXInputKeystrokesLocked` /
   `DrainDirectInputBufferedDataLocked` should cover it — confirm).

8. [x] **`input_system_messages.cpp` `WM_KEYDOWN`/`WM_SYSKEYDOWN`** — line 584 becomes
   `shouldBlock = blockKeyboard && !(msg == WM_SYSKEYDOWN && vk == VK_F4);` with the
   "never swallow Alt+F4" comment. — verify: compiles; manual — Alt+F4 quits the game with
   the menu open; normal keys still blocked when appropriate.

9. [x] **`input_system_raw.cpp` dedup + feed** — add file-local
   `bool TryConsumeRawInputStateLocked(HRAWINPUT rawInput)` **above its first caller** (or
   declare in `input_system_internal.h` ~533): `rawInput == nullptr → return true`; else
   walk `_state.RawInputSanitizeCache` directly, on matching `Handle` return `false` if
   `StateConsumed` already set else set it and return `true`; no match → `return true`.
   Gate the existing `UpdateStateFromRawInputLocked(*input)` in `HandleRawInputLocked` (718)
   on it. In `hkGetRawInputData`, when `_state.MenuVisible && TryConsume…(rawInput)`, call
   `UpdateStateFromRawInputLocked(*input)` after `RecordRawInputSanitizeCounterLocked` /
   before `ApplyRawInputSanitizeActionLocked`. In `hkGetRawInputBuffer`, when
   `_state.MenuVisible`, feed each packet the same way (no dedup — no handle). Port
   comments; add the ring-overwrite + cross-thread `EndFrame` notes as code comments.
   — verify: compiles; manual — menu gets the mouse in a raw-input title; a `WM_INPUT`
   title shows no doubled wheel/delta.

10a. [x] **`input_system_directinput.cpp` — feed helper.** Add
    `constexpr DWORD DirectInputMouseButtonsOffset = 3 * sizeof(LONG);` and file-local
    `void FeedOverlayMouseFromDirectInputStateLocked(const void* data, DWORD dataSize)`
    (**above its callers** or declared in internal.h): per button byte at
    `data + DirectInputMouseButtonsOffset`, high bit → `SetMouseDown(i, GetTickCount(),
    _state.BlockMouse)` on the down edge, else `SetMouseUpStateOnly(i, …)` when
    `_state.MouseButtons[i].Down`; clamp count to `_state.MouseButtons.size()`. — verify:
    compiles.

10b. [x] **`hkDirectInputGetDeviceState` rework** (immediate state, no queue). Compute
    `blocking` and `feedOverlay = _state.MenuVisible && kind == Mouse && data != nullptr`.
    Keep the fast **zero-and-return** when `blocking && !feedOverlay` (safe here — no
    buffered queue). Otherwise: **hoist** the per-device `original` resolution
    (`*(PVOID**)device` → `vtable[9]` → `ResolveDirectInputMethodTrampolineLocked`) so it
    is available on the `blocking && feedOverlay` path too; call `original(...)` under
    `ScopedHookBypass`; on success `FeedOverlayMouseFromDirectInputStateLocked(data,
    dataSize)`, then if `blocking` `memset(data,0,dataSize)` + bump the Blocked counter;
    return `hr`. — verify: compiles; manual in Assetto Corsa (+CSP) — menu clicks land with
    the cursor over a menu window; game still gets mouse state with the cursor over the car
    view.

10c. [x] **`hkDirectInputGetDeviceData` rework** (queue-backed — **preserve the drain**).
    Compute `blocking` and
    `feedOverlay = _state.MenuVisible && kind == Mouse && data != nullptr && inOut != nullptr
    && (flags & DIGDD_PEEK) == 0 && objectDataSize >= 2 * sizeof(DWORD)`. Per-device
    `original` as in 10b (`vtable[10]`).
    - `blocking && !feedOverlay`: keep the existing behaviour verbatim —
      `original(device, objectDataSize, nullptr, &INFINITE, 0)` flush, then `*inOut = 0`.
    - `feedOverlay`: `original(device, objectDataSize, data, inOut, flags)` under bypass;
      on success, per returned entry map `dwOfs` in
      `[DirectInputMouseButtonsOffset, +MouseButtons.size())` → button, high bit of
      `dwData` → `SetMouseDown` / `SetMouseUpStateOnly`. Then if `blocking`: **also flush
      the remainder** — `original(device, objectDataSize, nullptr, &INFINITE, 0)` — and set
      `*inOut = 0`, bump the Blocked counter.
    - `!blocking`: unchanged real passthrough.
    — verify: compiles; manual in AC — menu clicks land; after moving the cursor from a
    menu window back to the car view there is **no camera snap / click burst** in-game
    (drain preserved); `WM_INPUT` regression title unaffected.

11. [x] **`menu_common.cpp` `CheckShortcut` debounce** — file-scope
    `static uint64_t lastShortcutFireTick = 0;` (line ~67); inside `UpdateManualInput` add
    `static bool shortcutArmed[256] = {};`, `constexpr uint64_t shortcutCooldown = 250;`,
    and move `const auto currentTick = GetTickCount64();` above the lambda. In
    `CheckShortcut`: arm on `OptiInput::IsKeyPressed(vk)`; bail if `!shortcutArmed[vk]`;
    bail if `currentTick - lastShortcutFireTick < shortcutCooldown`; on `IsKeyReleased(vk)`
    clear `shortcutArmed[vk]`, stamp `lastShortcutFireTick = currentTick`, then the
    existing fire. Port the comment; note in-comment that the shared tick cross-suppresses
    all shortcuts for 250 ms. — verify: compiles; manual in AC — one tap toggles the menu
    once; the FPS/FG/DlssNr shortcuts still each work with >250 ms between presses.

12. [x] **Full build** — Debug x64 and Release x64, 0 errors each (new warnings noted, not
    gated). — verify: both configurations build clean.

13. [REVERTED 2026-09-07] **DirectInput relative-motion virtual cursor.** Built and
    committed as `3707092c` (3 files, +92), then reverted at the user's request:
    `git reset --mixed 2f9d2746` + `git checkout --` on the 3 source files; branch tip back
    at `2f9d2746`. Why reverted: the AC1 log showed `mode:window` (normal `WM_MOUSEMOVE`),
    so AC1 is not a parked-cursor DirectInput game and the virtual cursor never engaged
    there — it fixed nothing observable, and no other test case exercised it. What it was:
    5 `DirectInput*` fields in `input_system_internal.h`; `lX/lY` accumulation in
    `FeedOverlayMouseFromDirectInputStateLocked` + `DIMOFS_X/Y` in the `GetDeviceData` loop;
    `ApplyDirectInputVirtualMouseLocked` in `input_system.cpp` (seed from real pos while the
    polled cursor is stationary, add DI deltas, clamp, make authoritative; un-latch on
    real-cursor move / menu close). Kept only in git history at `3707092c` if ever wanted
    back for a genuinely parked-cursor DI title.

14. [FOLDED 2026-09-07] Manual test matrix + sign-off — **superseded by**
    `memory/plans/2026-09-07-overlay-input-review-fixes.md` step 8, which re-runs this
    matrix plus the Review-Pass regression scenarios after the remediation commit. AC1
    menu-mouse remains a **separate, pre-existing** problem (never worked on any build;
    `mode:window` game — CSP input interference the likely cause) tracked outside both plans.
    Original wording:
    **Manual test matrix + sign-off (non-AC).**
    - A title that drives its mouse through DirectInput / raw input with no `WM_INPUT`
      reaching our hooks: overlay clicks land (incl. same-frame press+release); one shortcut
      tap toggles once; Alt+F4 quits with the menu open; game bindings work with the cursor
      over the viewport, blocked over a menu window; no in-game input burst on cursor return;
      controller navigates the menu and leaves nothing stuck.
    - A title that drives both DirectInput and raw input for the mouse (if available):
      no doubled clicks.
    - NBA 2K26 (`WM_INPUT` regression): menu opens/closes normally, clicks + drag work, no
      doubled input, no stuck keys, no post-close burst. — verify: user confirms in-game.

## Notes / risks

- **Step 10c is the highest-risk edit.** The drain must survive; per-device trampoline
  resolution must be used; the three branches (`blocking && !feedOverlay`, `feedOverlay`,
  `!blocking`) have different correctness constraints. Re-express against the current body,
  do not paste `ae2a10af`.
- Cross-path (DI vs raw) mouse-button dedup is **intentionally not attempted** — DI feed is
  buttons-only (no wheel/delta) and `FeedImGui`'s `Down || Pressed` collapses redundant
  level events. Documented, not a bug.
- **DI virtual cursor (step 13)** engages only when the polled real cursor is stationary
  *and* DI relative motion is arriving, and un-latches on the first real-cursor move. So a
  game whose OS cursor moves normally never uses it; only the parked-cursor case (AC + CSP)
  does. Wheel via DI is still not fed (out of scope). Risk if AC's DI mouse format is
  non-standard (lX/lY not the first two LONGs) — matches the same assumption the button
  offset already relies on.
- `TryConsumeRawInputStateLocked` correctness depends on it running after
  `GetRawInputSanitizeDecisionLocked` has created the handle's cache entry — preserve that
  call order in both sites.
- No push / PR without explicit user go-ahead.
