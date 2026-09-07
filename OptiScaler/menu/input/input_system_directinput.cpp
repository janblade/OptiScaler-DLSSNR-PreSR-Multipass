#include "pch.h"
#include "input_system_internal.h"

#include <detours/detours.h>

#include <cstring>

namespace OptiInput
{
namespace
{
constexpr wchar_t DirectInput8ModuleName[] = L"dinput8.dll";
constexpr wchar_t DirectInputLegacyModuleName[] = L"dinput.dll";

constexpr char DirectInput8CreateExportName[] = "DirectInput8Create";
constexpr char DirectInputCreateAExportName[] = "DirectInputCreateA";
constexpr char DirectInputCreateWExportName[] = "DirectInputCreateW";
constexpr char DirectInputCreateExExportName[] = "DirectInputCreateEx";

constexpr GUID DirectInputSysKeyboardGuid = {
    0x6f1d2b61, 0xd5a0, 0x11cf, { 0xbf, 0xc7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 }
};
constexpr GUID DirectInputSysMouseGuid = {
    0x6f1d2b60, 0xd5a0, 0x11cf, { 0xbf, 0xc7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 }
};

constexpr std::size_t MaxDirectInputMethodHooks = 8;

template <typename T> struct DirectInputMethodHookSlot
{
    bool InUse = false;
    T Target = nullptr;
    T Trampoline = nullptr;
};

std::array<DirectInputMethodHookSlot<DirectInputCreateDevice_t>, MaxDirectInputMethodHooks>
    DirectInputCreateDeviceHooks {};
std::array<DirectInputMethodHookSlot<DirectInputDeviceRelease_t>, MaxDirectInputMethodHooks> DirectInputReleaseHooks {};
std::array<DirectInputMethodHookSlot<DirectInputGetDeviceState_t>, MaxDirectInputMethodHooks>
    DirectInputGetDeviceStateHooks {};
std::array<DirectInputMethodHookSlot<DirectInputGetDeviceData_t>, MaxDirectInputMethodHooks>
    DirectInputGetDeviceDataHooks {};
std::array<DirectInputMethodHookSlot<DirectInputSetDataFormat_t>, MaxDirectInputMethodHooks>
    DirectInputSetDataFormatHooks {};

// Byte offset of rgbButtons[] inside DIMOUSESTATE / DIMOUSESTATE2 (lX, lY, lZ come first).
constexpr DWORD DirectInputMouseButtonsOffset = 3 * sizeof(LONG);

// A data format is a "standard mouse layout" for our purposes when its data size is
// sizeof(DIMOUSESTATE) / DIMOUSESTATE2 and it carries a button object at
// DirectInputMouseButtonsOffset -- i.e. reading rgbButtons at that offset is well defined.
bool DataFormatLooksLikeMouse(LPCDIDATAFORMAT format)
{
    if (format == nullptr || format->dwSize != sizeof(DIDATAFORMAT) ||
        format->dwObjSize != sizeof(DIOBJECTDATAFORMAT) || format->rgodf == nullptr)
        return false;

    if (format->dwDataSize != sizeof(DIMOUSESTATE) && format->dwDataSize != sizeof(DIMOUSESTATE2))
        return false;

    for (DWORD i = 0; i < format->dwNumObjs; i++)
    {
        const DIOBJECTDATAFORMAT& obj = format->rgodf[i];
        if (obj.dwOfs == DirectInputMouseButtonsOffset && (obj.dwType & DIDFT_BUTTON) != 0)
            return true;
    }

    return false;
}

bool IsDirectInputKeyboardGuid(REFGUID guid) { return IsEqualGUID(guid, DirectInputSysKeyboardGuid) != FALSE; }

bool IsDirectInputMouseGuid(REFGUID guid) { return IsEqualGUID(guid, DirectInputSysMouseGuid) != FALSE; }

DirectInputDeviceKind GetDirectInputDeviceKind(REFGUID guid)
{
    if (IsDirectInputKeyboardGuid(guid))
        return DirectInputDeviceKind::Keyboard;

    if (IsDirectInputMouseGuid(guid))
        return DirectInputDeviceKind::Mouse;

    return DirectInputDeviceKind::Other;
}

bool ShouldBlockDirectInputKeyboardLocked()
{
    return _state.Initialized && _state.Focused && ShouldBlockKeyboardInputLocked();
}

bool ShouldBlockDirectInputMouseLocked()
{
    return _state.Initialized && _state.Focused && ShouldBlockMouseInputLocked();
}

bool ShouldBlockDirectInputOtherLocked()
{
    // Gamepads / wheels / other non-pointer DirectInput devices stay blocked for as long as the
    // overlay is visible (BlockGamepad tracks visibility), not gated on the conditional
    // mouse/keyboard block.
    return _state.Initialized && ShouldBlockGamepadInputLocked();
}

bool ShouldBlockDirectInputDeviceLocked(DirectInputDeviceKind kind)
{
    switch (kind)
    {
    case DirectInputDeviceKind::Keyboard:
        return ShouldBlockDirectInputKeyboardLocked();

    case DirectInputDeviceKind::Mouse:
        return ShouldBlockDirectInputMouseLocked();

    case DirectInputDeviceKind::Other:
    default:
        return ShouldBlockDirectInputOtherLocked();
    }
}

const char* DirectInputDeviceKindName(DirectInputDeviceKind kind)
{
    switch (kind)
    {
    case DirectInputDeviceKind::Keyboard:
        return "keyboard";

    case DirectInputDeviceKind::Mouse:
        return "mouse";

    case DirectInputDeviceKind::Other:
    default:
        return "other";
    }
}

template <typename T, std::size_t N>
DirectInputMethodHookSlot<T>*
FindDirectInputMethodHookByTargetLocked(std::array<DirectInputMethodHookSlot<T>, N>& hooks, T target)
{
    if (target == nullptr)
        return nullptr;

    for (auto& slot : hooks)
    {
        if (slot.InUse && slot.Target == target)
            return &slot;
    }

    return nullptr;
}

template <typename T, std::size_t N>
DirectInputMethodHookSlot<T>* PrepareDirectInputMethodHookLocked(std::array<DirectInputMethodHookSlot<T>, N>& hooks,
                                                                 T target, bool* needsAttach)
{
    if (needsAttach != nullptr)
        *needsAttach = false;

    if (target == nullptr)
        return nullptr;

    if (auto* existing = FindDirectInputMethodHookByTargetLocked(hooks, target); existing != nullptr)
        return existing;

    for (auto& slot : hooks)
    {
        if (slot.InUse)
            continue;

        slot.InUse = true;
        slot.Target = target;
        slot.Trampoline = target;

        if (needsAttach != nullptr)
            *needsAttach = true;

        return &slot;
    }

    return nullptr;
}

template <typename T, std::size_t N>
bool HasDirectInputMethodHooksLocked(const std::array<DirectInputMethodHookSlot<T>, N>& hooks)
{
    for (const auto& slot : hooks)
    {
        if (slot.InUse)
            return true;
    }

    return false;
}

template <typename T, std::size_t N>
T FirstDirectInputMethodTrampolineLocked(const std::array<DirectInputMethodHookSlot<T>, N>& hooks)
{
    for (const auto& slot : hooks)
    {
        if (slot.InUse && slot.Trampoline != nullptr)
            return slot.Trampoline;
    }

    return nullptr;
}

template <typename T, std::size_t N>
T ResolveDirectInputMethodTrampolineLocked(const std::array<DirectInputMethodHookSlot<T>, N>& hooks, T target)
{
    if (target == nullptr)
        return nullptr;

    for (const auto& slot : hooks)
    {
        if (slot.InUse && slot.Target == target)
            return slot.Trampoline;
    }

    return nullptr;
}

void RefreshDirectInputDeviceHookStateLocked()
{
    _state.DirectInputDeviceReleaseHookInstalled = HasDirectInputMethodHooksLocked(DirectInputReleaseHooks);
    _state.DirectInputGetDeviceStateHookInstalled = HasDirectInputMethodHooksLocked(DirectInputGetDeviceStateHooks);
    _state.DirectInputGetDeviceDataHookInstalled = HasDirectInputMethodHooksLocked(DirectInputGetDeviceDataHooks);

    // Keep the legacy globals valid for diagnostics/compatibility, but do not use them to identify a target.
    o_DirectInputCreateDeviceA = FirstDirectInputMethodTrampolineLocked(DirectInputCreateDeviceHooks);
    o_DirectInputCreateDeviceW = o_DirectInputCreateDeviceA;
    o_DirectInputDeviceRelease = FirstDirectInputMethodTrampolineLocked(DirectInputReleaseHooks);
    o_DirectInputDeviceGetDeviceState = FirstDirectInputMethodTrampolineLocked(DirectInputGetDeviceStateHooks);
    o_DirectInputDeviceGetDeviceData = FirstDirectInputMethodTrampolineLocked(DirectInputGetDeviceDataHooks);
}

void ClearDirectInputMethodHooksLocked()
{
    DirectInputCreateDeviceHooks = {};
    _state.DirectInputCreateDeviceAHookInstalled = false;
    _state.DirectInputCreateDeviceWHookInstalled = false;
    DirectInputReleaseHooks = {};
    DirectInputGetDeviceStateHooks = {};
    DirectInputGetDeviceDataHooks = {};
    DirectInputSetDataFormatHooks = {};
    RefreshDirectInputDeviceHookStateLocked();
}

void MarkDirectInputDeviceKindSeenLocked(DirectInputDeviceKind kind)
{
    if (kind == DirectInputDeviceKind::Keyboard)
        _state.DirectInputKeyboardDeviceSeen = true;
    else if (kind == DirectInputDeviceKind::Mouse)
        _state.DirectInputMouseDeviceSeen = true;
    else
        _state.DirectInputOtherDeviceSeen = true;
}

HMODULE FindLoadedDirectInput8Module() { return GetModuleHandleW(DirectInput8ModuleName); }

HMODULE FindLoadedDirectInputLegacyModule() { return GetModuleHandleW(DirectInputLegacyModuleName); }

void ClearDirectInputHookPointersLocked()
{
    o_DirectInput8Create = nullptr;
    o_DirectInputCreateA = nullptr;
    o_DirectInputCreateW = nullptr;
    o_DirectInputCreateEx = nullptr;
    o_DirectInputCreateDeviceA = nullptr;
    o_DirectInputCreateDeviceW = nullptr;

    _state.DirectInput8CreateHookInstalled = false;
    _state.DirectInputCreateAHookInstalled = false;
    _state.DirectInputCreateWHookInstalled = false;
    _state.DirectInputCreateExHookInstalled = false;
    _state.DirectInputCreateDeviceAHookInstalled = false;
    _state.DirectInputCreateDeviceWHookInstalled = false;

    ClearDirectInputMethodHooksLocked();
}

std::size_t FindDirectInputDeviceSlotLocked(void* device)
{
    if (device == nullptr)
        return MaxTrackedDirectInputDevices;

    for (std::size_t i = 0; i < _state.DirectInputDeviceSlots.size(); ++i)
    {
        if (_state.DirectInputDeviceSlots[i].InUse && _state.DirectInputDeviceSlots[i].Device == device)
            return i;
    }

    return MaxTrackedDirectInputDevices;
}

// True when the overlay may read mouse buttons out of this device's caller buffer. Permissive
// until we have actually seen the game's SetDataFormat call (matches pre-hardening behaviour for
// devices created before the hook attached); once seen, only a standard mouse layout qualifies.
bool DirectInputMouseFormatUsableLocked(void* device)
{
    const std::size_t slot = FindDirectInputDeviceSlotLocked(device);
    if (slot >= MaxTrackedDirectInputDevices)
        return true;

    const DirectInputDeviceSlot& deviceSlot = _state.DirectInputDeviceSlots[slot];
    return !deviceSlot.MouseFormatChecked || deviceSlot.MouseFormatUsable;
}

DirectInputDeviceKind GetDirectInputDeviceKindLocked(void* device)
{
    const std::size_t slot = FindDirectInputDeviceSlotLocked(device);

    if (slot >= MaxTrackedDirectInputDevices)
        return DirectInputDeviceKind::Other;

    return _state.DirectInputDeviceSlots[slot].Kind;
}

void ClearDirectInputDeviceSlotLocked(std::size_t slot)
{
    if (slot >= MaxTrackedDirectInputDevices)
        return;

    if (_state.DirectInputDeviceSlots[slot].InUse && _state.DirectInputTrackedDeviceCount > 0)
        _state.DirectInputTrackedDeviceCount--;

    _state.DirectInputDeviceSlots[slot] = {};
}

void ClearAllDirectInputDeviceSlotsLocked()
{
    _state.DirectInputDeviceSlots = {};
    _state.DirectInputTrackedDeviceCount = 0;
}

void TrackDirectInputDeviceLocked(void* device, DirectInputDeviceKind kind)
{
    if (device == nullptr)
        return;

    std::size_t freeSlot = MaxTrackedDirectInputDevices;

    for (std::size_t i = 0; i < _state.DirectInputDeviceSlots.size(); ++i)
    {
        auto& slot = _state.DirectInputDeviceSlots[i];

        if (slot.InUse && slot.Device == device)
        {
            // A later CreateDevice call may use an instance GUID that we cannot classify and
            // therefore reports Other. Never downgrade a known keyboard/mouse classification.
            if (slot.Kind == DirectInputDeviceKind::Other && kind != DirectInputDeviceKind::Other)
            {
                slot.Kind = kind;
                MarkDirectInputDeviceKindSeenLocked(kind);
                LOG_INFO("DirectInput device reclassified device:{} kind:{}", device, DirectInputDeviceKindName(kind));
            }
            else if (slot.Kind != DirectInputDeviceKind::Other && kind != DirectInputDeviceKind::Other &&
                     slot.Kind != kind)
            {
                LOG_WARN("DirectInput device kind mismatch device:{} existing:{} new:{}; preserving existing kind",
                         device, DirectInputDeviceKindName(slot.Kind), DirectInputDeviceKindName(kind));
            }

            return;
        }

        if (!slot.InUse && freeSlot >= MaxTrackedDirectInputDevices)
            freeSlot = i;
    }

    if (freeSlot >= MaxTrackedDirectInputDevices)
    {
        LOG_WARN("DirectInput device tracking table is full device:{} kind:{}", device,
                 DirectInputDeviceKindName(kind));
        return;
    }

    auto& slot = _state.DirectInputDeviceSlots[freeSlot];
    slot.InUse = true;
    slot.Device = device;
    slot.Kind = kind;

    _state.DirectInputTrackedDeviceCount++;

    MarkDirectInputDeviceKindSeenLocked(kind);

    LOG_INFO("DirectInput device captured device:{} kind:{}", device, DirectInputDeviceKindName(kind));
}

bool HookDirectInputDeviceLocked(void* device, DirectInputDeviceKind kind)
{
    if (device == nullptr)
        return false;

    PVOID* vtable = *reinterpret_cast<PVOID**>(device);

    auto release = reinterpret_cast<DirectInputDeviceRelease_t>(vtable[2]);
    auto getDeviceState = reinterpret_cast<DirectInputGetDeviceState_t>(vtable[9]);
    auto getDeviceData = reinterpret_cast<DirectInputGetDeviceData_t>(vtable[10]);
    auto setDataFormat = reinterpret_cast<DirectInputSetDataFormat_t>(vtable[11]);

    bool attachRelease = false;
    bool attachGetDeviceState = false;
    bool attachGetDeviceData = false;
    bool attachSetDataFormat = false;

    auto* releaseHook = PrepareDirectInputMethodHookLocked(DirectInputReleaseHooks, release, &attachRelease);
    auto* getDeviceStateHook =
        PrepareDirectInputMethodHookLocked(DirectInputGetDeviceStateHooks, getDeviceState, &attachGetDeviceState);
    auto* getDeviceDataHook =
        PrepareDirectInputMethodHookLocked(DirectInputGetDeviceDataHooks, getDeviceData, &attachGetDeviceData);
    auto* setDataFormatHook =
        PrepareDirectInputMethodHookLocked(DirectInputSetDataFormatHooks, setDataFormat, &attachSetDataFormat);

    bool completeCoverage = true;

    if (release != nullptr && releaseHook == nullptr)
    {
        LOG_WARN("DirectInput Release hook table is full, device:{} target:{}", device,
                 reinterpret_cast<void*>(release));
        completeCoverage = false;
    }

    if (getDeviceState != nullptr && getDeviceStateHook == nullptr)
    {
        LOG_WARN("DirectInput GetDeviceState hook table is full, device:{} target:{}", device,
                 reinterpret_cast<void*>(getDeviceState));
        completeCoverage = false;
    }

    if (getDeviceData != nullptr && getDeviceDataHook == nullptr)
    {
        LOG_WARN("DirectInput GetDeviceData hook table is full, device:{} target:{}", device,
                 reinterpret_cast<void*>(getDeviceData));
        completeCoverage = false;
    }

    if (setDataFormat != nullptr && setDataFormatHook == nullptr)
    {
        LOG_WARN("DirectInput SetDataFormat hook table is full, device:{} target:{}", device,
                 reinterpret_cast<void*>(setDataFormat));
        completeCoverage = false;
    }

    if (!attachRelease && !attachGetDeviceState && !attachGetDeviceData && !attachSetDataFormat)
    {
        TrackDirectInputDeviceLocked(device, kind);
        return completeCoverage;
    }

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (attachRelease)
        DetourAttach(reinterpret_cast<PVOID*>(&releaseHook->Trampoline), hkDirectInputDeviceRelease);

    if (attachGetDeviceState)
        DetourAttach(reinterpret_cast<PVOID*>(&getDeviceStateHook->Trampoline), hkDirectInputGetDeviceState);

    if (attachGetDeviceData)
        DetourAttach(reinterpret_cast<PVOID*>(&getDeviceDataHook->Trampoline), hkDirectInputGetDeviceData);

    if (attachSetDataFormat)
        DetourAttach(reinterpret_cast<PVOID*>(&setDataFormatHook->Trampoline), hkDirectInputSetDataFormat);

    const LONG result = DetourTransactionCommit();

    if (result != NO_ERROR)
    {
        LOG_ERROR("DirectInput device hook installation failed result:{} device:{} kind:{}", result, device,
                  DirectInputDeviceKindName(kind));

        if (attachRelease)
            *releaseHook = {};

        if (attachGetDeviceState)
            *getDeviceStateHook = {};

        if (attachGetDeviceData)
            *getDeviceDataHook = {};

        if (attachSetDataFormat)
            *setDataFormatHook = {};

        RefreshDirectInputDeviceHookStateLocked();
        return false;
    }

    RefreshDirectInputDeviceHookStateLocked();

    if (attachRelease)
        LOG_INFO("DirectInput Release target detoured target:{} device:{}", reinterpret_cast<void*>(release), device);

    if (attachGetDeviceState)
        LOG_INFO("DirectInput GetDeviceState target detoured target:{} device:{}",
                 reinterpret_cast<void*>(getDeviceState), device);

    if (attachGetDeviceData)
        LOG_INFO("DirectInput GetDeviceData target detoured target:{} device:{}",
                 reinterpret_cast<void*>(getDeviceData), device);

    if (attachSetDataFormat)
        LOG_INFO("DirectInput SetDataFormat target detoured target:{} device:{}",
                 reinterpret_cast<void*>(setDataFormat), device);

    TrackDirectInputDeviceLocked(device, kind);
    return completeCoverage;
}

bool HookDirectInputInterfaceLocked(void* directInput, bool wide)
{
    if (directInput == nullptr)
        return false;

    PVOID* vtable = *reinterpret_cast<PVOID**>(directInput);
    auto createDevice = reinterpret_cast<DirectInputCreateDevice_t>(vtable[3]);

    if (createDevice == nullptr)
        return false;

    bool needsAttach = false;
    auto* slot = PrepareDirectInputMethodHookLocked(DirectInputCreateDeviceHooks, createDevice, &needsAttach);

    if (slot == nullptr)
    {
        LOG_ERROR("DirectInput CreateDevice hook table full wide:{} target:{}", wide ? 1 : 0,
                  reinterpret_cast<void*>(createDevice));
        return false;
    }

    if (needsAttach)
    {
        // ANSI and Unicode CreateDevice have the same ABI. Route every unique
        // implementation through one detour so a shared A/W implementation is
        // never attached twice. The interface's vtable identifies the correct
        // per-target trampoline at call time.
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(reinterpret_cast<PVOID*>(&slot->Trampoline), hkDirectInputCreateDeviceA);

        const LONG result = DetourTransactionCommit();

        if (result != NO_ERROR)
        {
            LOG_ERROR("DirectInput CreateDevice hook installation failed result:{} wide:{} target:{}", result,
                      wide ? 1 : 0, reinterpret_cast<void*>(createDevice));
            *slot = {};
            RefreshDirectInputDeviceHookStateLocked();
            return false;
        }

        LOG_INFO("DirectInput CreateDevice target detoured target:{}", reinterpret_cast<void*>(createDevice));
    }

    if (wide)
        _state.DirectInputCreateDeviceWHookInstalled = true;
    else
        _state.DirectInputCreateDeviceAHookInstalled = true;

    RefreshDirectInputDeviceHookStateLocked();
    return true;
}

bool TryGetDirectInputInterfaceWidth(REFIID riid, bool* wide)
{
    if (wide == nullptr)
        return false;

    if (IsEqualGUID(riid, IID_IDirectInput8W) || IsEqualGUID(riid, IID_IDirectInput7W) ||
        IsEqualGUID(riid, IID_IDirectInput2W) || IsEqualGUID(riid, IID_IDirectInputW))
    {
        *wide = true;
        return true;
    }

    if (IsEqualGUID(riid, IID_IDirectInput8A) || IsEqualGUID(riid, IID_IDirectInput7A) ||
        IsEqualGUID(riid, IID_IDirectInput2A) || IsEqualGUID(riid, IID_IDirectInputA))
    {
        *wide = false;
        return true;
    }

    return false;
}

void HandleDirectInputCreatedLocked(REFIID riid, void** out, const char* source)
{
    if (out == nullptr || *out == nullptr)
        return;

    bool wide = false;
    if (TryGetDirectInputInterfaceWidth(riid, &wide))
    {
        HookDirectInputInterfaceLocked(*out, wide);
        return;
    }

    OPTIINPUT_LOG_VERBOSE("{} returned unsupported riid directInput:{} riid:{}",
                          source != nullptr ? source : "DirectInput", *out, static_cast<const void*>(&riid));
}

void HandleLegacyDirectInputCreatedLocked(void** out, bool wide)
{
    if (out == nullptr || *out == nullptr)
        return;

    HookDirectInputInterfaceLocked(*out, wide);
}

bool InstallDirectInputExportHookLocked(HMODULE module, const char* exportName, void** original, void* hook,
                                        bool* installed)
{
    if (module == nullptr || exportName == nullptr || original == nullptr || hook == nullptr || installed == nullptr)
        return false;

    if (*installed)
        return true;

    *original = reinterpret_cast<void*>(GetProcAddress(module, exportName));

    if (*original == nullptr)
        return false;

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(reinterpret_cast<PVOID*>(original), hook);

    const LONG result = DetourTransactionCommit();

    if (result != NO_ERROR)
    {
        LOG_ERROR("{} hook installation failed result:{}", exportName, result);
        *original = nullptr;
        return false;
    }

    *installed = true;
    LOG_INFO("{} hook installed module:{}", exportName, static_cast<void*>(module));
    return true;
}

HRESULT CallDirectInputCreateDeviceOriginal(DirectInputCreateDevice_t original, void* directInput, REFGUID guid,
                                            void** device, LPUNKNOWN outer)
{
    if (original == nullptr)
        return DIERR_GENERIC;

    ScopedHookBypass bypass;
    return original(directInput, guid, device, outer);
}
} // namespace

void UpdateDirectInputIntegrationLocked()
{
    HMODULE module8 = FindLoadedDirectInput8Module();
    HMODULE legacyModule = FindLoadedDirectInputLegacyModule();

    _state.DirectInputModule = module8;
    _state.DirectInputLegacyModule = legacyModule;
    _state.DirectInputModuleLoaded = module8 != nullptr || legacyModule != nullptr;
    _state.DirectInputLegacyModuleLoaded = legacyModule != nullptr;

    if (module8 != nullptr)
    {
        if (!InstallDirectInputExportHookLocked(module8, DirectInput8CreateExportName,
                                                reinterpret_cast<void**>(&o_DirectInput8Create), hkDirectInput8Create,
                                                &_state.DirectInput8CreateHookInstalled))
        {
            if (o_DirectInput8Create == nullptr)
            {
                OPTIINPUT_LOG_VERBOSE("DirectInput8Create export was not found module:{}", static_cast<void*>(module8));
            }
        }
    }

    if (legacyModule != nullptr)
    {
        InstallDirectInputExportHookLocked(legacyModule, DirectInputCreateAExportName,
                                           reinterpret_cast<void**>(&o_DirectInputCreateA), hkDirectInputCreateA,
                                           &_state.DirectInputCreateAHookInstalled);

        InstallDirectInputExportHookLocked(legacyModule, DirectInputCreateWExportName,
                                           reinterpret_cast<void**>(&o_DirectInputCreateW), hkDirectInputCreateW,
                                           &_state.DirectInputCreateWHookInstalled);

        InstallDirectInputExportHookLocked(legacyModule, DirectInputCreateExExportName,
                                           reinterpret_cast<void**>(&o_DirectInputCreateEx), hkDirectInputCreateEx,
                                           &_state.DirectInputCreateExHookInstalled);
    }
}

bool RemoveDirectInputHooksLocked()
{
    if (!_state.DirectInput8CreateHookInstalled && !_state.DirectInputCreateAHookInstalled &&
        !_state.DirectInputCreateWHookInstalled && !_state.DirectInputCreateExHookInstalled &&
        !_state.DirectInputCreateDeviceAHookInstalled && !_state.DirectInputCreateDeviceWHookInstalled &&
        !_state.DirectInputGetDeviceStateHookInstalled && !_state.DirectInputGetDeviceDataHookInstalled &&
        !_state.DirectInputDeviceReleaseHookInstalled &&
        !HasDirectInputMethodHooksLocked(DirectInputSetDataFormatHooks))
    {
        ClearDirectInputHookPointersLocked();
        ClearAllDirectInputDeviceSlotsLocked();
        return true;
    }

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (_state.DirectInput8CreateHookInstalled && o_DirectInput8Create != nullptr)
        DetourDetach(reinterpret_cast<PVOID*>(&o_DirectInput8Create), hkDirectInput8Create);

    if (_state.DirectInputCreateAHookInstalled && o_DirectInputCreateA != nullptr)
        DetourDetach(reinterpret_cast<PVOID*>(&o_DirectInputCreateA), hkDirectInputCreateA);

    if (_state.DirectInputCreateWHookInstalled && o_DirectInputCreateW != nullptr)
        DetourDetach(reinterpret_cast<PVOID*>(&o_DirectInputCreateW), hkDirectInputCreateW);

    if (_state.DirectInputCreateExHookInstalled && o_DirectInputCreateEx != nullptr)
        DetourDetach(reinterpret_cast<PVOID*>(&o_DirectInputCreateEx), hkDirectInputCreateEx);

    for (auto& slot : DirectInputCreateDeviceHooks)
    {
        if (slot.InUse && slot.Trampoline != nullptr)
            DetourDetach(reinterpret_cast<PVOID*>(&slot.Trampoline), hkDirectInputCreateDeviceA);
    }

    for (auto& slot : DirectInputGetDeviceStateHooks)
    {
        if (slot.InUse && slot.Trampoline != nullptr)
            DetourDetach(reinterpret_cast<PVOID*>(&slot.Trampoline), hkDirectInputGetDeviceState);
    }

    for (auto& slot : DirectInputGetDeviceDataHooks)
    {
        if (slot.InUse && slot.Trampoline != nullptr)
            DetourDetach(reinterpret_cast<PVOID*>(&slot.Trampoline), hkDirectInputGetDeviceData);
    }

    for (auto& slot : DirectInputSetDataFormatHooks)
    {
        if (slot.InUse && slot.Trampoline != nullptr)
            DetourDetach(reinterpret_cast<PVOID*>(&slot.Trampoline), hkDirectInputSetDataFormat);
    }

    for (auto& slot : DirectInputReleaseHooks)
    {
        if (slot.InUse && slot.Trampoline != nullptr)
            DetourDetach(reinterpret_cast<PVOID*>(&slot.Trampoline), hkDirectInputDeviceRelease);
    }

    const LONG result = DetourTransactionCommit();

    if (result != NO_ERROR)
    {
        LOG_WARN("DirectInput hook removal failed result:{}; retaining trampoline tables for a safe retry", result);
        return false;
    }

    ClearDirectInputHookPointersLocked();
    ClearAllDirectInputDeviceSlotsLocked();
    return true;
}

void DrainDirectInputBufferedDataLocked()
{
    for (DirectInputDeviceSlot& deviceSlot : _state.DirectInputDeviceSlots)
    {
        if (!deviceSlot.InUse || deviceSlot.Device == nullptr)
            continue;

        PVOID* vtable = *reinterpret_cast<PVOID**>(deviceSlot.Device);
        auto target = reinterpret_cast<DirectInputGetDeviceData_t>(vtable[10]);
        DirectInputGetDeviceData_t original =
            ResolveDirectInputMethodTrampolineLocked(DirectInputGetDeviceDataHooks, target);

        if (original == nullptr)
            continue;

        const DWORD objectDataSize =
            deviceSlot.LastObjectDataSize != 0 ? deviceSlot.LastObjectDataSize : sizeof(DIDEVICEOBJECTDATA);
        DWORD flushCount = INFINITE;

        ScopedHookBypass bypass;
        original(deviceSlot.Device, objectDataSize, nullptr, &flushCount, 0);
    }
}

HRESULT WINAPI hkDirectInput8Create(HINSTANCE instance, DWORD version, REFIID riid, LPVOID* out, LPUNKNOWN outer)
{
    HRESULT result = DIERR_GENERIC;

    if (o_DirectInput8Create != nullptr)
    {
        ScopedHookBypass bypass;
        result = o_DirectInput8Create(instance, version, riid, out, outer);
    }

    {
        std::unique_lock lock(_state.Mutex);
        _state.DirectInputCreateCallCount++;

        if (SUCCEEDED(result))
        {
            _state.DirectInputCreateSucceededCount++;
            HandleDirectInputCreatedLocked(riid, reinterpret_cast<void**>(out), "DirectInput8Create");
        }
        else
        {
            _state.DirectInputCreateFailedCount++;
        }
    }

    return result;
}

HRESULT WINAPI hkDirectInputCreateA(HINSTANCE instance, DWORD version, void** out, LPUNKNOWN outer)
{
    HRESULT result = DIERR_GENERIC;

    if (o_DirectInputCreateA != nullptr)
    {
        ScopedHookBypass bypass;
        result = o_DirectInputCreateA(instance, version, out, outer);
    }

    {
        std::unique_lock lock(_state.Mutex);
        _state.DirectInputCreateCallCount++;

        if (SUCCEEDED(result))
        {
            _state.DirectInputCreateSucceededCount++;
            HandleLegacyDirectInputCreatedLocked(out, false);
        }
        else
        {
            _state.DirectInputCreateFailedCount++;
        }
    }

    return result;
}

HRESULT WINAPI hkDirectInputCreateW(HINSTANCE instance, DWORD version, void** out, LPUNKNOWN outer)
{
    HRESULT result = DIERR_GENERIC;

    if (o_DirectInputCreateW != nullptr)
    {
        ScopedHookBypass bypass;
        result = o_DirectInputCreateW(instance, version, out, outer);
    }

    {
        std::unique_lock lock(_state.Mutex);
        _state.DirectInputCreateCallCount++;

        if (SUCCEEDED(result))
        {
            _state.DirectInputCreateSucceededCount++;
            HandleLegacyDirectInputCreatedLocked(out, true);
        }
        else
        {
            _state.DirectInputCreateFailedCount++;
        }
    }

    return result;
}

HRESULT WINAPI hkDirectInputCreateEx(HINSTANCE instance, DWORD version, REFIID riid, LPVOID* out, LPUNKNOWN outer)
{
    HRESULT result = DIERR_GENERIC;

    if (o_DirectInputCreateEx != nullptr)
    {
        ScopedHookBypass bypass;
        result = o_DirectInputCreateEx(instance, version, riid, out, outer);
    }

    {
        std::unique_lock lock(_state.Mutex);
        _state.DirectInputCreateCallCount++;

        if (SUCCEEDED(result))
        {
            _state.DirectInputCreateSucceededCount++;
            HandleDirectInputCreatedLocked(riid, reinterpret_cast<void**>(out), "DirectInputCreateEx");
        }
        else
        {
            _state.DirectInputCreateFailedCount++;
        }
    }

    return result;
}

HRESULT WINAPI hkDirectInputCreateDeviceA(void* directInput, REFGUID guid, void** device, LPUNKNOWN outer)
{
    DirectInputCreateDevice_t original = nullptr;

    {
        std::unique_lock lock(_state.Mutex);

        if (directInput != nullptr)
        {
            PVOID* vtable = *reinterpret_cast<PVOID**>(directInput);
            auto target = reinterpret_cast<DirectInputCreateDevice_t>(vtable[3]);
            original = ResolveDirectInputMethodTrampolineLocked(DirectInputCreateDeviceHooks, target);
        }
    }

    HRESULT result = CallDirectInputCreateDeviceOriginal(original, directInput, guid, device, outer);

    {
        std::unique_lock lock(_state.Mutex);
        _state.DirectInputCreateDeviceCallCount++;

        if (SUCCEEDED(result) && device != nullptr && *device != nullptr)
        {
            _state.DirectInputCreateDeviceSucceededCount++;
            HookDirectInputDeviceLocked(*device, GetDirectInputDeviceKind(guid));
        }
        else if (FAILED(result))
        {
            _state.DirectInputCreateDeviceFailedCount++;
        }
    }

    return result;
}

HRESULT WINAPI hkDirectInputCreateDeviceW(void* directInput, REFGUID guid, void** device, LPUNKNOWN outer)
{
    DirectInputCreateDevice_t original = nullptr;

    {
        std::unique_lock lock(_state.Mutex);

        if (directInput != nullptr)
        {
            PVOID* vtable = *reinterpret_cast<PVOID**>(directInput);
            auto target = reinterpret_cast<DirectInputCreateDevice_t>(vtable[3]);
            original = ResolveDirectInputMethodTrampolineLocked(DirectInputCreateDeviceHooks, target);
        }
    }

    HRESULT result = CallDirectInputCreateDeviceOriginal(original, directInput, guid, device, outer);

    {
        std::unique_lock lock(_state.Mutex);
        _state.DirectInputCreateDeviceCallCount++;

        if (SUCCEEDED(result) && device != nullptr && *device != nullptr)
        {
            _state.DirectInputCreateDeviceSucceededCount++;
            HookDirectInputDeviceLocked(*device, GetDirectInputDeviceKind(guid));
        }
        else if (FAILED(result))
        {
            _state.DirectInputCreateDeviceFailedCount++;
        }
    }

    return result;
}

namespace
{

// Apply one mouse-button level to the overlay's ImGui state from a DirectInput read. index
// 0=left, 1=right, 2=middle -- exactly our MouseButtons layout. `blocking` is the real per-call
// block decision (not _state.BlockMouse): BlockedDown is only set when the game is actually
// being denied this press, so a later real WM_*BUTTONUP does not suppress a release the game is
// owed. Called with _state.Mutex held.
void FeedOverlayMouseButtonLevelLocked(int button, bool down, DWORD time, bool blocking)
{
    if (button < 0 || button >= static_cast<int>(_state.MouseButtons.size()))
        return;

    if (down)
        SetMouseDown(button, time, blocking);
    else if (_state.MouseButtons[button].Down)
        SetMouseUpStateOnly(button, time);
}

// Feed the overlay's ImGui mouse-button state from a DirectInput immediate-state (GetDeviceState)
// read. A button is down when the high bit of its byte is set. Buttons only -- movement and wheel
// are left to the raw / message paths, so there is no wheel/delta double-count. The caller has
// already confirmed the device's data format places rgbButtons at DirectInputMouseButtonsOffset
// (SetDataFormat hook). Called with _state.Mutex held.
void FeedOverlayMouseFromDirectInputStateLocked(const void* data, DWORD dataSize, bool blocking)
{
    if (data == nullptr || dataSize <= DirectInputMouseButtonsOffset)
        return;

    const BYTE* buttons = static_cast<const BYTE*>(data) + DirectInputMouseButtonsOffset;
    DWORD count = dataSize - DirectInputMouseButtonsOffset;
    if (count > _state.MouseButtons.size())
        count = static_cast<DWORD>(_state.MouseButtons.size());

    const DWORD time = GetTickCount();

    for (DWORD i = 0; i < count; i++)
        FeedOverlayMouseButtonLevelLocked(static_cast<int>(i), (buttons[i] & 0x80) != 0, time, blocking);
}

// Flush the device's buffered event queue and present an empty successful read. Used on every
// GetDeviceData path where the menu owns the mouse -- without the flush, menu-time events replay
// into the game on the first frame after the overlay closes. `original` is the per-device
// vtable[10] trampoline. Does not touch _state; call without holding _state.Mutex (as the old
// inline flush did).
void BlockAndDrainDeviceData(DirectInputGetDeviceData_t original, void* device, DWORD objectDataSize, LPDWORD inOut)
{
    if (original != nullptr)
    {
        DWORD flushCount = INFINITE;
        ScopedHookBypass bypass;
        original(device, objectDataSize, nullptr, &flushCount, 0);
    }

    if (inOut != nullptr)
        *inOut = 0;
}

} // namespace

HRESULT WINAPI hkDirectInputSetDataFormat(void* device, LPCDIDATAFORMAT format)
{
    DirectInputSetDataFormat_t original = nullptr;

    {
        std::unique_lock lock(_state.Mutex);

        const std::size_t slot = FindDirectInputDeviceSlotLocked(device);
        if (slot < MaxTrackedDirectInputDevices)
        {
            _state.DirectInputDeviceSlots[slot].MouseFormatChecked = true;
            _state.DirectInputDeviceSlots[slot].MouseFormatUsable = DataFormatLooksLikeMouse(format);
        }

        if (device != nullptr)
        {
            PVOID* vtable = *reinterpret_cast<PVOID**>(device);
            auto target = reinterpret_cast<DirectInputSetDataFormat_t>(vtable[11]);
            original = ResolveDirectInputMethodTrampolineLocked(DirectInputSetDataFormatHooks, target);
        }
    }

    if (original == nullptr)
        return DIERR_GENERIC;

    ScopedHookBypass bypass;
    return original(device, format);
}

HRESULT WINAPI hkDirectInputGetDeviceState(void* device, DWORD dataSize, LPVOID data)
{
    DirectInputGetDeviceState_t original = nullptr;
    bool blocking = false;
    bool feedOverlay = false;

    {
        std::unique_lock lock(_state.Mutex);
        const DirectInputDeviceKind kind = GetDirectInputDeviceKindLocked(device);
        _state.DirectInputGetDeviceStateCallCount++;

        blocking = ShouldBlockDirectInputDeviceLocked(kind);

        // Feed the overlay only while it actually owns input (focused, menu up, not inside a
        // hook-bypass) and only when this device's data format really is a standard mouse
        // layout -- otherwise bytes at DirectInputMouseButtonsOffset are not rgbButtons.
        feedOverlay = kind == DirectInputDeviceKind::Mouse && data != nullptr && _state.Initialized &&
                      _state.Focused && ShouldApplyBlockingPolicyLocked() &&
                      DirectInputMouseFormatUsableLocked(device);

        // Fast path: blocking and the overlay does not need this device -- zero it and return
        // without ever calling the real read, exactly as before.
        if (blocking && !feedOverlay)
        {
            if (data != nullptr && dataSize > 0)
                std::memset(data, 0, dataSize);

            _state.DirectInputGetDeviceStateBlockedCount++;
            OPTIINPUT_LOG_VERBOSE("blocking DirectInput GetDeviceState device:{} kind:{} size:{}", device,
                                  DirectInputDeviceKindName(kind), dataSize);
            return DI_OK;
        }

        if (!blocking)
            _state.DirectInputGetDeviceStatePassedCount++;

        if (device != nullptr)
        {
            PVOID* vtable = *reinterpret_cast<PVOID**>(device);
            auto target = reinterpret_cast<DirectInputGetDeviceState_t>(vtable[9]);
            original = ResolveDirectInputMethodTrampolineLocked(DirectInputGetDeviceStateHooks, target);
        }
    }

    if (original == nullptr)
        return DIERR_GENERIC;

    HRESULT hr;
    {
        ScopedHookBypass bypass;
        hr = original(device, dataSize, data);
    }

    // The game reads its mouse through DirectInput (common in Assetto Corsa + CSP). Read the real
    // state first so the overlay learns the click, then hide it from the game if the menu owns the
    // mouse this frame. GetDeviceState is immediate (no buffered queue), so there is nothing to
    // drain -- zeroing the returned struct is enough.
    if (feedOverlay)
    {
        if (SUCCEEDED(hr))
        {
            std::unique_lock lock(_state.Mutex);
            FeedOverlayMouseFromDirectInputStateLocked(data, dataSize, blocking);
        }

        // Hide real button state from the game whether or not the read succeeded -- on failure
        // the buffer is undefined and a game that reads it anyway would otherwise briefly see
        // live state with the overlay up.
        if (blocking)
        {
            if (data != nullptr && dataSize > 0)
                std::memset(data, 0, dataSize);

            std::unique_lock lock(_state.Mutex);
            _state.DirectInputGetDeviceStateBlockedCount++;
        }
    }

    return hr;
}

HRESULT WINAPI hkDirectInputGetDeviceData(void* device, DWORD objectDataSize, LPDIDEVICEOBJECTDATA data, LPDWORD inOut,
                                          DWORD flags)
{
    DirectInputGetDeviceData_t original = nullptr;
    DirectInputDeviceKind kind = DirectInputDeviceKind::Other;
    bool blocking = false;
    bool feedOverlay = false;

    {
        std::unique_lock lock(_state.Mutex);
        kind = GetDirectInputDeviceKindLocked(device);
        _state.DirectInputGetDeviceDataCallCount++;
        blocking = ShouldBlockDirectInputDeviceLocked(kind);

        // Only mirror real buffered reads (data != null, not a PEEK): a PEEK leaves the events in
        // the buffer for a later real read, and feeding both would double-count the click. Also
        // gate on the overlay actually owning input (focused, menu up, not inside a hook-bypass)
        // and on this device's data format being a standard mouse layout.
        feedOverlay = kind == DirectInputDeviceKind::Mouse && data != nullptr && inOut != nullptr &&
                      (flags & DIGDD_PEEK) == 0 && objectDataSize >= 2 * sizeof(DWORD) && _state.Initialized &&
                      _state.Focused && ShouldApplyBlockingPolicyLocked() && DirectInputMouseFormatUsableLocked(device);

        if (blocking && !feedOverlay)
            _state.DirectInputGetDeviceDataBlockedCount++;
        else if (!blocking)
            _state.DirectInputGetDeviceDataPassedCount++;

        if (device != nullptr)
        {
            const std::size_t deviceSlotIndex = FindDirectInputDeviceSlotLocked(device);
            if (deviceSlotIndex < MaxTrackedDirectInputDevices && objectDataSize != 0)
                _state.DirectInputDeviceSlots[deviceSlotIndex].LastObjectDataSize = objectDataSize;

            PVOID* vtable = *reinterpret_cast<PVOID**>(device);
            auto target = reinterpret_cast<DirectInputGetDeviceData_t>(vtable[10]);
            original = ResolveDirectInputMethodTrampolineLocked(DirectInputGetDeviceDataHooks, target);
        }
    }

    // Not blocking and the overlay does not need it: straight passthrough, exactly as before.
    if (!blocking && !feedOverlay)
    {
        if (original == nullptr)
            return DIERR_GENERIC;

        ScopedHookBypass bypass;
        return original(device, objectDataSize, data, inOut, flags);
    }

    // Blocking and the overlay does not need it: GetDeviceData is backed by a buffered event queue,
    // so returning zero without touching the real queue lets menu-time events replay after closing
    // the overlay. Flush the device buffer, then present an empty successful read.
    if (blocking && !feedOverlay)
    {
        BlockAndDrainDeviceData(original, device, objectDataSize, inOut);

        OPTIINPUT_LOG_VERBOSE("blocking DirectInput GetDeviceData device:{} kind:{} flags:{}", device,
                              DirectInputDeviceKindName(kind), flags);
        return DI_OK;
    }

    // feedOverlay: read the real buffered data so the overlay learns the clicks, then, if the menu
    // owns the mouse this frame, drain whatever is left and hide it from the game.
    if (original == nullptr)
        return DIERR_GENERIC;

    HRESULT hr;
    {
        ScopedHookBypass bypass;
        hr = original(device, objectDataSize, data, inOut, flags);
    }

    if (SUCCEEDED(hr) && inOut != nullptr)
    {
        std::unique_lock lock(_state.Mutex);

        const DWORD entries = *inOut;
        const DWORD time = GetTickCount();

        for (DWORD i = 0; i < entries; i++)
        {
            const auto* entry = reinterpret_cast<const DIDEVICEOBJECTDATA*>(
                reinterpret_cast<const BYTE*>(data) + static_cast<size_t>(i) * objectDataSize);

            // Mouse button offsets are DIMOFS_BUTTON0..7 == rgbButtons[] offset + index.
            if (entry->dwOfs < DirectInputMouseButtonsOffset ||
                entry->dwOfs >= DirectInputMouseButtonsOffset + _state.MouseButtons.size())
                continue;

            FeedOverlayMouseButtonLevelLocked(static_cast<int>(entry->dwOfs - DirectInputMouseButtonsOffset),
                                              (entry->dwData & 0x80) != 0, time, blocking);
        }
    }

    // Drain the remainder and hide the read from the game whether or not the real read above
    // succeeded -- the queue must not survive to replay after the overlay closes, and *inOut
    // must be zeroed even on a failed read.
    if (blocking)
    {
        BlockAndDrainDeviceData(original, device, objectDataSize, inOut);

        std::unique_lock lock(_state.Mutex);
        _state.DirectInputGetDeviceDataBlockedCount++;
    }

    return hr;
}

ULONG WINAPI hkDirectInputDeviceRelease(void* device)
{
    DirectInputDeviceRelease_t original = nullptr;

    {
        std::unique_lock lock(_state.Mutex);

        if (device != nullptr)
        {
            PVOID* vtable = *reinterpret_cast<PVOID**>(device);
            auto target = reinterpret_cast<DirectInputDeviceRelease_t>(vtable[2]);
            original = ResolveDirectInputMethodTrampolineLocked(DirectInputReleaseHooks, target);
        }
    }

    ULONG result = 0;

    if (original != nullptr)
    {
        ScopedHookBypass bypass;
        result = original(device);
    }

    if (result == 0)
    {
        std::unique_lock lock(_state.Mutex);
        const std::size_t slot = FindDirectInputDeviceSlotLocked(device);

        if (slot < MaxTrackedDirectInputDevices)
            ClearDirectInputDeviceSlotLocked(slot);
    }

    return result;
}

} // namespace OptiInput
