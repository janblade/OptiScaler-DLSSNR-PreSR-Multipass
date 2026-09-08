# Plan: Address the Review Pass findings on the overlay-input commit

- Branch: fix/overlay-input-stack-agnostic  (same branch as the parent plan; one extra commit, PR back to main)
- Created: 2026-09-07
- Status: done             # draft | approved | in-progress | done | abandoned
- Task file: memory/tasks/main.md
- Parent plan: memory/plans/2026-09-07-overlay-input-stack-agnostic.md (commit 2f9d2746 is the reviewed baseline)
- Source: core.dev-loop.sk Review Pass (v2.8.0), independent-reviewer branch, run 2026-09-07 on 2f9d2746

## Context

**What this is:** remediation of the 9 findings the independent Review Pass raised against
commit `2f9d2746` ("Make the overlay usable regardless of how the game acquires input").
The commit builds clean and passed the `WM_INPUT` no-regression check in-game; these are
correctness gaps in the new DirectInput / raw-input / shortcut paths plus two ownership-fit
structural objections. No behaviour in the parent plan's 6 acceptance criteria is being
redesigned — this tightens the implementation that meets them.

**Findings addressed (severity from the review):**
- **#1 HIGH** — `_state.MouseButtons[b].Down` is only ever cleared in `ResetStateAfterShutdown`
  (`input_system.cpp:964`). `ResetButtonBlockedStateLocked` clears `.BlockedDown` only. So in a
  pure-DirectInput game, closing the menu (or losing focus) while a mouse button is physically
  held leaves the overlay's button state stuck down; `FeedImGui` feeds `Down || Pressed` on the
  next open → phantom click. (`io.ClearInputMouse()` in `FeedImGui(false)` fixes ImGui's own
  state but not `_state`.)
- **#2 MEDIUM** — `menu_common.cpp:275-291`: one shared `lastShortcutFireTick` + the cooldown
  `return` sits *before* the `IsKeyReleased` check, whose `.Released` transient is cleared each
  frame by `ClearTransientState`. A shortcut release landing within 250 ms of any other
  shortcut fire is consumed and lost, not deferred.
- **#3 MEDIUM (ownership-fit)** — `ApplyMenuVisibilityChangeLocked` samples `io.WantCaptureMouse`
  / `WantCaptureKeyboard` every call and is invoked from 3 sites (`input_system.cpp:1129`,
  `:1356` EndFrame, `:1365` SetMenuVisible). The one-frame-lag invariant in its own comment only
  holds for the EndFrame caller; via `SetMenuVisible` the `Want*` flags are sampled at an
  arbitrary frame point. Per-frame policy is the wrong concern for a visibility-transition
  handler.
- **#4 MEDIUM (ownership-fit)** — `hkDirectInputGetDeviceData` grew ~30 → ~120 lines: the
  `INFINITE` drain + `*inOut = 0` + counter block is written 3×; the button down/up dispatch is
  duplicated between `FeedOverlayMouseFromDirectInputStateLocked` and the buffered-data loop.
  `FeedOverlayMouseFromDirectInputStateLocked` / `DirectInputMouseButtonsOffset` sit at
  `namespace OptiInput` scope, not the file's anonymous namespace where its other helpers live.
- **#5 LOW** — the DI feed passes `_state.BlockMouse` to `SetMouseDown`, not the computed
  `blocking` (`Initialized && Focused && ShouldApplyBlockingPolicyLocked() && _state.BlockMouse`).
  In a mixed DI+WM / unfocused / bypass context this sets `BlockedDown` on a press the game did
  receive → the later real `WM_*BUTTONUP` suppresses a release the game is owed.
- **#6 LOW** — `feedOverlay` is gated on bare `_state.MenuVisible`, not focus / `bypassHookDepth`,
  so background DI polling while alt-tabbed feeds stale mouse state into the overlay.
- **#7 LOW** — in the `blocking && feedOverlay` branches the buffer is only zeroed / drained on
  `SUCCEEDED(hr)`; the old fast path memset unconditionally. On `DIERR_NOTACQUIRED` /
  `DIERR_INPUTLOST` a game that reads the struct anyway briefly sees real button state with the
  menu open. `Passed` / `Blocked` counters also don't move on `hr` failure.
- **#8 LOW (security)** — `FeedOverlayMouseFromDirectInputStateLocked` treats bytes at offset 12
  of the caller's buffer as `rgbButtons[]` because the device is a system mouse, with no check
  that `SetDataFormat` was `c_dfDIMouse` / `c_dfDIMouse2` (`SetDataFormat` is not hooked). No OOB
  (count clamped to `MouseButtons.size()`, reads within `dataSize`) — phantom input, not memory
  unsafety.
- **#9 LOW (ownership-fit)** — `static bool shortcutArmed[256]` is never reset on focus loss /
  menu re-init / rebind; partially overlaps the pre-existing 1000 ms `debounceThreshold` gate in
  the same function.

**Scope decisions (resolved at approval, 2026-09-07 — user: "Fix all"):**
- **#8** — hardened, not accepted: hook `IDirectInputDevice::SetDataFormat` (vtable[11]),
  record the per-device format, and only feed the overlay from a recognised mouse format.
  Step 5 is the hook path.
- **#3 and #4** (ownership-fit refactors) — in scope this pass.

**Out of scope:** everything the parent plan put out of scope; the AC1 `mode:window` menu-mouse
bug (separate, pre-existing); any new overlay behaviour.

**Verification reality:** unchanged — no automated tests (`has_tests:false`). Every behaviour
step = clean Debug|x64 + Release|x64 + manual in-game. DirectInput / raw-only title for the new
paths; NBA 2K26 for the `WM_INPUT` regression.

## Steps

1. [x] **#1 — release held overlay mouse buttons on menu-close and focus-loss.**
   `input_system_messages.cpp`: add `ReleaseHeldOverlayMouseButtonsLocked()` next to
   `ResetButtonBlockedStateLocked` — for each `_state.MouseButtons[]` with `.Down`, clear
   `.Down` / `.Pressed` / set `.Released` (so a consumer still sees one clean up edge), leave
   `.BlockedDown` to the existing reset. Declare in `input_system_internal.h`.
   Call it from (a) the `wasMenuVisible && !visible` branch of `ApplyMenuVisibilityChangeLocked`
   (`input_system.cpp:788-796`), after the existing drains, and (b) `HandleBlockingFocusLossLocked`
   (`input_system.cpp:89-100`), after `ResetButtonBlockedStateLocked`. ImGui side is already
   covered by `io.ClearInputMouse()` in `FeedImGui(false)`.
   — verify: Debug+Release build; **manual** (DI-only title) — hold LMB over a menu window, tap
   the toggle to close while still holding, reopen → no phantom/stuck click; repeat with an
   alt-tab (focus loss) instead of a close.

2. [x] **#3 — extract per-frame blocking policy out of the transition handler.**
   `input_system.cpp`: new anon-namespace `void UpdateBlockingPolicyLocked()` that does the
   `visible && ImGui::GetCurrentContext() != nullptr` → `WantCaptureMouse` /
   `WantCaptureKeyboard || WantTextInput` sampling and sets `_state.BlockMouse` /
   `_state.BlockKeyboard`; move the "ReShade-style conditional blocking" comment onto it.
   Call it from `EndFrame` (`input_system.cpp:~1356`) immediately before
   `ApplyMenuVisibilityChangeLocked(menuVisible)`, i.e. after `FeedImGui` + `ImGui::NewFrame()`
   where the one-frame-lag invariant actually holds.
   In `ApplyMenuVisibilityChangeLocked`: drop the per-call `Want*` sampling. On the
   `!wasMenuVisible && visible` transition seed `_state.BlockMouse = _state.BlockKeyboard = true`
   (assume capture until the next `EndFrame` policy tick refines it); on
   `wasMenuVisible && !visible` set both `false`. Keep `BlockCursor` / `BlockGamepad = visible`
   here. Fix the now-inaccurate "This runs from EndFrame" comment (the fn has 3 callers).
   — verify: build; **manual** — criterion 3 still holds (cursor over viewport, no text focus →
   game keys + mouse-look reach the game; cursor over a menu window → neither; text fields get
   typing); open the menu via the shortcut (the `SetMenuVisible` path) → no transient
   mis-block on the first frame.

3. [x] **#4 / #5 / #6 / #7 — DirectInput hook refactor + correctness (highest-risk step).**
   `input_system_directinput.cpp`:
   - Move `DirectInputMouseButtonsOffset` and `FeedOverlayMouseFromDirectInputStateLocked` into
     the file's anonymous namespace (#4).
   - Extract `BlockAndDrainDeviceDataLocked(original, device, objectDataSize, inOut)` — the
     `original(device, objectDataSize, nullptr, &INFINITE, 0)` flush + `*inOut = 0` + Blocked
     counter — and call it from every blocking `GetDeviceData` branch that currently inlines it
     (#4). **The drain must remain on every blocking path** (parent plan step 10c constraint).
   - Extract `FeedOverlayMouseButtonLevelLocked(button, down, time, blocking)` — the
     `if (down) SetMouseDown(button, time, blocking) else if (_state.MouseButtons[button].Down)
     SetMouseUpStateOnly(button, time)` — shared by the `GetDeviceState` feed and the
     `GetDeviceData` per-entry loop (#4).
   - Pass the computed `blocking` (not `_state.BlockMouse`) through the feed helpers (#5).
   - `feedOverlay` gates on `ShouldApplyBlockingPolicyLocked() && kind == Mouse && data != nullptr`
     (+ the existing `GetDeviceData` guards: `inOut`, non-`PEEK`, `objectDataSize`), i.e. via the
     helper that already encodes `bypassHookDepth == 0 && MenuVisible && Focused`, instead of bare
     `_state.MenuVisible` (#6).
   - In the blocking branches, on `FAILED(hr)` still `memset(data, 0, dataSize)` (GetDeviceState)
     / drain + `*inOut = 0` (GetDeviceData) and move the counters, before returning `hr` (#7).
   — verify: Debug+Release build; **manual in Assetto Corsa (+CSP)** — re-run the parent plan's
   10b/10c matrix: menu clicks land with the cursor over a menu window; the game still gets mouse
   state with the cursor over the car view; **no camera snap / click burst** when moving the
   cursor from a menu window back to the view (drain survived the refactor); NBA 2K26 unaffected.

4. [x] **#2 / #9 — per-key shortcut cooldown + arm reset.**
   `menu_common.cpp`:
   - `lastShortcutFireTick` → `static uint64_t lastShortcutFireTick[256] = {};`; the cooldown
     check becomes `currentTick - lastShortcutFireTick[vk] < shortcutCooldown` and the stamp
     `lastShortcutFireTick[vk] = currentTick`. Removes cross-shortcut suppression (#2) — a second
     *different* shortcut within 250 ms now fires; a <250 ms same-key repeat is still debounced
     (intended). Replace the "shared across all shortcuts" comment.
   - Reset `shortcutArmed` (and `lastShortcutFireTick`) on menu focus loss / re-init — find the
     cleanest existing hook during execution (`ResetMenuInputTransientState`, or a `MenuCommon`
     focus-loss path); if none is clean, a small `static` reset guarded on a focus-edge in
     `UpdateManualInput` (#9).
   - Add a comment distinguishing the two debounces: 1000 ms `canAcceptInputs` = key-capture /
     rebind guard; 250 ms per-key cooldown = multi-path double-edge suppression (#9).
   — verify: build; **manual** — one tap toggles the menu once; menu + FPS shortcuts tapped
   ~150 ms apart both register; same-key <250 ms double-tap still debounced; alt-tab away and
   back mid-hold → no spurious fire.

5. [x] **#8 — verify the DI mouse data format before reading buttons from the caller buffer.**
   `input_system_directinput.cpp`: hook `IDirectInputDevice8::SetDataFormat` (vtable index
   **11**) through `ResolveDirectInputMethodTrampolineLocked`, alongside the existing
   `GetDeviceState` (9) / `GetDeviceData` (10) hooks. In the hook, inspect the `LPCDIDATAFORMAT`:
   record per device (keyed by the device pointer, same map/structure the trampoline resolution
   already uses) whether the format is a mouse layout — `dwObjSize == sizeof(DIOBJECTDATAFORMAT)`
   and `dwDataSize` is `sizeof(DIMOUSESTATE)` (16) or `sizeof(DIMOUSESTATE2)` (20), with the
   axis/button offsets matching (or simply: pointer-equals `c_dfDIMouse` / `c_dfDIMouse2`, which
   covers the overwhelming majority — DirectX ships those as fixed globals). Call the original
   under `ScopedHookBypass`, return its `hr`.
   Gate `FeedOverlayMouseFromDirectInputStateLocked` (now in the anon namespace per step 3) on
   that per-device flag: unknown or non-mouse format → do not read buttons from the buffer, skip
   the feed for that device. Default when a device was never seen calling `SetDataFormat`
   (created before our hook, or via a path we don't see): treat as **mouse-format-assumed** only
   if `dataSize` is exactly 16 or 20, else skip — so the worst pre-existing case (arbitrary
   `dataSize`) is no longer fed.
   — verify: Debug+Release build; **manual** — Assetto Corsa (+CSP) overlay clicks still land
   (its mouse device resolves to a known mouse format); a title/tool using a non-mouse custom
   format on `GUID_SysMouse` (or the synthetic case: force the flag off) is not fed and produces
   no phantom clicks; no crash when `SetDataFormat` is called before hooks install.

6. [x] **Full build.** Debug|x64 and Release|x64, 0 errors, no new warnings beyond the
   pre-existing set (Magnifier C4244, C4250 dominance, C4744, LNK4098).
   — verify: both configurations build clean.

7. [x] **Re-run the Review Pass** (`core.dev-loop.sk`, `PLAN_EXECUTE` step 6) — done 2026-09-07,
   **independent-reviewer** branch (subagent), on the working-tree diff vs `2f9d2746`.
   Verdict: **merge with nits**. All 9 fixes verified landed and correct; no blocking regression.
   Findings:
   - **F1 (Low, regression)** — `RemoveDirectInputHooksLocked` early-out guard didn't cover the
     new `DirectInputSetDataFormatHooks` table → a latent "clear without detach" if the install
     grouping ever changed. **Folded in:** added
     `!HasDirectInputMethodHooksLocked(DirectInputSetDataFormatHooks)` to the guard.
   - **F3 (Nit, security)** — `DataFormatLooksLikeMouse` strode `rgodf` without checking
     `dwObjSize == sizeof(DIOBJECTDATAFORMAT)`. **Folded in:** added the check.
   - **F2 (Low, ownership-fit)** — the `SetDataFormat` hook is heavier blast radius than a LOW
     finding warrants; a `dwDataSize`/`dataSize` equality guard in the feed helpers would cover
     most of it without a 5th detour. **Retained the hook** per the user's explicit "Fix all"
     decision at approval; noted as an accepted cost.
   - **F4 (Nit)** — two short `_state.Mutex` sections in the `hkDirectInputGetDeviceState`
     feedOverlay path where pre-remediation had one. Left as-is (correct, no spanning invariant).
   - **F5 (informational, intended)** — one extra frame of mouse-block on menu-open from the #3
     seed. Documented intent, errs safe. No change.
   — verify: findings reported to the user; no unresolved HIGH.

8. [x] **Manual test matrix + sign-off** — user, 2026-09-07. (absorbs parent plan step 14). All 6 parent
   acceptance criteria, plus the review's regression scenarios:
   - close the menu — and separately alt-tab — with a mouse button physically held → no
     phantom / stuck click on reopen (#1);
   - two different shortcuts fired within 250 ms → both register; same-key <250 ms double-tap →
     debounced (#2);
   - toggle the menu at <15 fps → still single-toggle (the per-key arm + release-edge
     requirement carries this even when the 250 ms wall-clock cooldown is shorter than a frame);
   - Assetto Corsa (+CSP): no post-close camera snap after the step-3 DI refactor;
   - NBA 2K26 (`WM_INPUT`): menu open/close, clicks, drag, no doubled input, no stuck keys, no
     post-close burst.
   RESULT: Assetto Corsa (+CSP) OK -- clicks, close, Alt+F4, no post-close burst. NBA 2K26
   (WM_INPUT) OK -- no regression. Only AC1 menu-window hold-and-drag still broken; that is
   the pre-existing `mode:window` / CSP mouse-capture bug, explicitly out of scope here and
   tracked as its own task. All 6 parent acceptance criteria + the review regression
   scenarios pass. — verify: user confirmed in-game.

9. [x] **Commit; push + PR gate.** Committed 2026-09-07 as `122eaca3` (5 files, +269/-85);
   branch `fix/overlay-input-stack-agnostic` pushed to origin at the user's go-ahead (tip
   122eaca3, parent 2f9d2746). PR into main NOT opened -- user said "Push" only; open on request.
   — verify: `git ls-remote origin fix/overlay-input-stack-agnostic` resolves to 122eaca3.

## Notes / risks

- **Step 3 is the highest risk** — same warning as the parent plan's step 10c. The `INFINITE`
  drain must survive the extraction on *every* blocking `GetDeviceData` branch; per-device
  `vtable[9]/[10]` trampoline resolution stays. Re-express against the current body; verify the
  no-snap behaviour in AC specifically.
- **Step 2 behaviour delta:** seeding `BlockMouse/BlockKeyboard = true` on menu-open means the
  first frame after opening always blocks mouse/keyboard until `UpdateBlockingPolicyLocked`
  runs at the next `EndFrame` — one frame, matches ReShade, and is stricter (not looser) than
  today, so no game-input leak risk.
- **Step 4 behaviour delta:** per-key cooldown means a fast alternation between two shortcuts is
  no longer rate-limited against each other. That is the point (#2), but confirm in-game that
  no real double-fire returns on the multi-path stacks the shared tick was added for.
- No push / PR without explicit user go-ahead (carried from the parent plan).
