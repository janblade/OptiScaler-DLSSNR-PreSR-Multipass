#pragma once

// Recognising an NVIDIA DLSS-G provider (the nvngx_dlssg snippet), wherever it was loaded from.
// Header-only and free of OptiScaler headers so tests/mfg_provider_smoke.cpp can compile it alone.
//
// The lookup by file name misses the copy NVIDIA's driver stores under
//   C:\ProgramData\NVIDIA\NGX\models\dlssg\versions\<n>\files\<hash>.bin
// and a game that renamed its snippet. Both are recognised here. Recognising a module never patches it:
// the signatures in MfgUnlock.cpp stay the only gate.
//
// Adapted from KleberMotta/OptiScaler-DLSS5-MFG-RTX40 74c3bac (mfgunlock/, MIT, from the RenoDX MFG
// Unlock addon by Dreamt / mavismmg). See Licenses/MFGUnlock_LICENSE.txt.

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace MfgUnlock::Provider
{
// A string only the DLSS-G snippet carries. Used for a module whose path says nothing.
inline constexpr std::string_view kMarker = "dlfg_kernel";

// Lower-cased, backslash-only, doubled separators collapsed, so the two spellings the loader hands us
// ("models//dlssg" and "models\dlssg") compare equal.
inline std::wstring NormalisePath(std::wstring_view path)
{
    std::wstring out;
    out.reserve(path.size());

    for (wchar_t c : path)
    {
        if (c == L'/')
            c = L'\\';
        else if (c >= L'A' && c <= L'Z')
            c = static_cast<wchar_t>(c - L'A' + L'a');

        if (c == L'\\' && !out.empty() && out.back() == L'\\')
            continue;

        out.push_back(c);
    }

    return out;
}

// The game's own nvngx_dlssg.dll, or anything in the driver's DLSS-G OTA store.
inline bool IsProviderPath(std::wstring_view path)
{
    const std::wstring normalised = NormalisePath(path);
    const std::wstring_view view = normalised;

    const auto slash = view.find_last_of(L'\\');
    const auto name = slash == std::wstring_view::npos ? view : view.substr(slash + 1);

    return name == L"nvngx_dlssg.dll" || view.find(L"\\models\\dlssg\\") != std::wstring_view::npos;
}

// Whether a mapped PE image holds `needle` inside the initialised part of one of its readable sections.
// Sections that claim to run past SizeOfImage are skipped rather than trusted. A module that is
// unloaded while this runs answers false instead of faulting.
inline bool ImageContains(const void* image, std::string_view needle)
{
    if (image == nullptr || needle.empty())
        return false;

    __try
    {
        const auto* base = static_cast<const uint8_t*>(image);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);

        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
            return false;

        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);

        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
            return false;

        const size_t imageSize = nt->OptionalHeader.SizeOfImage;
        const auto* section = IMAGE_FIRST_SECTION(nt);

        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section)
        {
            if (!(section->Characteristics & IMAGE_SCN_MEM_READ))
                continue;

            const size_t start = section->VirtualAddress;
            const size_t size = section->Misc.VirtualSize;

            if (size < needle.size() || start > imageSize || size > imageSize - start)
                continue;

            const auto* begin = base + start;
            const auto* end = begin + size;

            if (std::search(begin, end, needle.begin(), needle.end()) != end)
                return true;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }

    return false;
}
} // namespace MfgUnlock::Provider
