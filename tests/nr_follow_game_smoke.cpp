// Host check of DlssNr_FollowGame.h: the learned offset between Automatic's and the game's exposure. No GPU and no game needed.
// cl /std:c++20 /EHsc tests/nr_follow_game_smoke.cpp
#include "../OptiScaler/shaders/dlssnr/DlssNr_FollowGame.h"

#include <cstdio>

using namespace DlssNrFollowGame;

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

static bool Near(float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance; }

int main()
{
    // RDR2 gameplay: Automatic ~1400 against the game's ~150, a steady -3.2 EV the other way round (auto is 9.2x).
    {
        Calibration c;
        for (unsigned i = 0; i < kWindow - 1; ++i)
            CHECK(!c.Feed(1400.0f, 150.0f));
        CHECK(!c.Locked());
        CHECK(c.Scale() == 1.0f);
        CHECK(c.Feed(1400.0f, 150.0f));
        CHECK(c.Locked());
        CHECK(Near(c.OffsetEv(), std::log2(1400.0f / 150.0f), 1e-4f));
        CHECK(Near(c.Scale(), 1400.0f / 150.0f, 1e-3f));
    }
    // Locked is final: later readings do not move it.
    {
        Calibration c;
        for (unsigned i = 0; i < kWindow; ++i)
            c.Feed(1400.0f, 150.0f);
        const float locked = c.OffsetEv();
        for (unsigned i = 0; i < kWindow * 3; ++i)
            CHECK(!c.Feed(100.0f, 150.0f));
        CHECK(c.OffsetEv() == locked);
    }
    // A minority of loading-screen readings is outvoted by the median.
    {
        Calibration c;
        for (unsigned i = 0; i < kWindow; ++i)
            c.Feed(i % 5 == 0 ? 2.0f : 1400.0f, 150.0f);
        CHECK(Near(c.OffsetEv(), std::log2(1400.0f / 150.0f), 1e-4f));
    }
    // Broken readings are not taken: zero, negative, NaN, infinity, and offsets beyond kMaxEv.
    {
        Calibration c;
        c.Feed(0.0f, 150.0f);
        c.Feed(1400.0f, 0.0f);
        c.Feed(-1.0f, 150.0f);
        c.Feed(NAN, 150.0f);
        c.Feed(1400.0f, INFINITY);
        c.Feed(1e6f, 1.0f);
        CHECK(c.Readings() == 0);
        CHECK(!c.Locked());
    }
    // Reset starts over.
    {
        Calibration c;
        for (unsigned i = 0; i < kWindow; ++i)
            c.Feed(1400.0f, 150.0f);
        c.Reset();
        CHECK(!c.Locked());
        CHECK(c.Readings() == 0);
        CHECK(c.Scale() == 1.0f);
    }

    printf(fails ? "FAILED: %d\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
