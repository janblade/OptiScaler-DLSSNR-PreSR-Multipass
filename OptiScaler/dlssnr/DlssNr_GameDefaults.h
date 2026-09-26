#pragma once

// The Automatic exposure defaults for the running game (DlssNr_AutoTrimDefault.h): the Trim and following the game's
// exposure, from the known-game list unless the user set them. Every D3D12, Vulkan and menu reader goes through here.

#include <Config.h>
#include <State.h>
#include <shaders/dlssnr/DlssNr_AutoTrimDefault.h>

#include <atomic>

namespace DlssNr
{
// Whether the running game is on the list of known unexposed games. The exe does not change within a session.
inline bool KnownUnexposedGame()
{
    static const bool known = DlssNrAutoTrim::IsKnownUnexposedGame(State::Instance().gameExe);
    return known;
}

// The Automatic Trim in force: the user's, else the game's default.
inline float AutoTrimEffective(const Config& cfg)
{
    return DlssNrAutoTrim::Effective(cfg.DlssNrAutoExposureTrim, KnownUnexposedGame());
}

// Whether Automatic follows the game's exposure: the user's choice, else on for a known unexposed game.
inline bool FollowGameOn(const Config& cfg)
{
    return DlssNrAutoTrim::FollowGame(cfg.DlssNrAutoExposureFollowGame, KnownUnexposedGame());
}

// Says once which defaults the game gets and why. Call it where Automatic is metered.
inline void ReportAutoExposureDefaults()
{
    static std::atomic<bool> said { false };

    if (said.exchange(true))
        return;

    const bool known = KnownUnexposedGame();
    LOG_INFO("DLSS-NR automatic exposure: {} ({}) -> default Trim {} ({}), following the game's exposure {} by default",
             known ? "known unexposed game" : "not a known unexposed game", State::Instance().gameExe,
             DlssNrAutoTrim::DefaultTrim(known), known ? "+4.3 EV" : "+2.3 EV", known ? "on" : "off");
}
} // namespace DlssNr
