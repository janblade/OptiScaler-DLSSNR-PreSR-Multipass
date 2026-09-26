#pragma once

// The Automatic exposure Trim a game gets while the user has not set one (ini AutoExposureTrim = auto), and whether it
// follows the game's own exposure by default.
//
// Two kinds of frame reach Automatic exposure, and they want different defaults (measured 2026-09-25 with FrameStats).
// Both are linear HDR; what differs is whether the game has already applied its own exposure before the upscaler:
// - unexposed (RDR2: raw scene luminance, the game's exposure of ~0.006-0.013 comes later): +4.3 EV (Trim 0.25) gave the
//   model a median of 0.77-0.80 and looked best; following the game's exposure keeps its fades and adaptation;
// - pre-exposed (NBA 2K27, Cyberpunk 2077, The Witcher 3): +4.3 EV put yellow highlights in NBA's player shadows,
//   +2.3 EV (Trim 1) looked right in all three; following would apply the game's exposure a second time.
//
// Which kind a game is cannot be read off the frame: a dark unexposed scene has the numbers of a pre-exposed one (RDR2
// read 12-116 in dim scenes on 2026-09-26, 900-1700 in daylight), and the DLSS inputs are the same (NBA passes an exposure
// texture, PreExposure 1 and the flag like RDR2). So the games measured to be unexposed are listed by exe, everything else
// gets the pre-exposed defaults, and the user's slider and Follow checkbox override both.
//
// Header-only and free of D3D/Vulkan types so it can be exercised on the host (tests/nr_auto_trim_smoke.cpp).

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>

namespace DlssNrAutoTrim
{
constexpr float kUnexposedTrim = 0.25f; // +4.3 EV on the menu's scale (neutral 5x)
constexpr float kPreExposedTrim = 1.0f;  // +2.3 EV

// Games measured to hand over unexposed frames, by exe name (lower case). Kept here rather than as a GameQuirk in
// misc/Quirks.h: upstream OptiScaler owns those lines (rdr2.exe already has compatibility quirks there), and a DLSS-NR
// flag on them would conflict on every sync. Add a game only with a FrameStats measurement behind it.
constexpr const char* kUnexposedGames[] = {
    "rdr2.exe",     // Red Dead Redemption 2: scene median 150-350 in daylight, game exposure ~0.006-0.013 (2026-09-25)
    "playrdr2.exe", // its launcher-started exe
};

// Whether the game's exe (name or full path, any case) is on kUnexposedGames.
inline bool IsKnownUnexposedGame(const std::string& exe)
{
    const size_t slash = exe.find_last_of("\\/");
    std::string name = slash == std::string::npos ? exe : exe.substr(slash + 1);
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return (char) std::tolower(c); });

    for (const char* known : kUnexposedGames)
        if (name == known)
            return true;

    return false;
}

inline float DefaultTrim(bool knownUnexposed) { return knownUnexposed ? kUnexposedTrim : kPreExposedTrim; }

// The Trim in force: the user's own when they set one, otherwise the game's default.
inline float Effective(const std::optional<float>& userTrim, bool knownUnexposed)
{
    return userTrim.has_value() ? *userTrim : DefaultTrim(knownUnexposed);
}

// Whether Automatic follows the game's exposure: the user's choice when they made one, otherwise on for a known
// unexposed game only.
inline bool FollowGame(const std::optional<bool>& userFollow, bool knownUnexposed)
{
    return userFollow.has_value() ? *userFollow : knownUnexposed;
}
} // namespace DlssNrAutoTrim
