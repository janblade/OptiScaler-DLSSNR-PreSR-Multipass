// Host check of DlssNr_AutoTrimDefault.h: the Automatic exposure defaults a game gets from the known-game list. No GPU
// and no game needed.
// cl /std:c++20 /EHsc tests/nr_auto_trim_smoke.cpp
#include "../OptiScaler/shaders/dlssnr/DlssNr_AutoTrimDefault.h"

#include <cstdio>

using namespace DlssNrAutoTrim;

static int fails = 0;
#define CHECK(c)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(c))                                                                                                      \
        {                                                                                                              \
            printf("FAIL line %d: %s\n", __LINE__, #c);                                                                \
            ++fails;                                                                                                   \
        }                                                                                                              \
    } while (0)

int main()
{
    // Known unexposed games, by exe name: any case, with or without a path; anything else is not on the list.
    {
        CHECK(IsKnownUnexposedGame("RDR2.exe"));
        CHECK(IsKnownUnexposedGame("rdr2.exe"));
        CHECK(IsKnownUnexposedGame("PlayRDR2.exe"));
        CHECK(IsKnownUnexposedGame("J:\\Games\\Red Dead Redemption 2\\RDR2.exe"));
        CHECK(IsKnownUnexposedGame("C:/Games/RDR2/rdr2.exe"));
        CHECK(!IsKnownUnexposedGame("rdr.exe")); // Red Dead Redemption 1: not measured
        CHECK(!IsKnownUnexposedGame("nba2k27.exe"));
        CHECK(!IsKnownUnexposedGame("xrdr2.exe"));
        CHECK(!IsKnownUnexposedGame("rdr2.exe.bak"));
        CHECK(!IsKnownUnexposedGame(""));
    }
    // Default Trim: +4.3 EV for a known unexposed game, +2.3 EV for every other game.
    {
        CHECK(DefaultTrim(true) == kUnexposedTrim);
        CHECK(DefaultTrim(false) == kPreExposedTrim);
    }
    // The user's Trim always wins; without one the game's default applies.
    {
        CHECK(Effective(std::nullopt, true) == kUnexposedTrim);
        CHECK(Effective(std::nullopt, false) == kPreExposedTrim);
        CHECK(Effective(std::optional<float>(3.0f), true) == 3.0f);
        CHECK(Effective(std::optional<float>(3.0f), false) == 3.0f);
    }
    // Following the game's exposure: on by default only for a known unexposed game; the user's choice wins both ways.
    {
        CHECK(FollowGame(std::nullopt, true));
        CHECK(!FollowGame(std::nullopt, false));
        CHECK(FollowGame(std::optional<bool>(true), false));  // ticked by hand for an unlisted unexposed game
        CHECK(!FollowGame(std::optional<bool>(false), true)); // unticked in RDR2
    }

    printf(fails ? "FAILED: %d\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
