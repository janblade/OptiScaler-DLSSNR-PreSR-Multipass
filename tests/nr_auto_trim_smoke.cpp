// Host check of DlssNr_AutoTrimDefault.h: the Automatic exposure default Trim chosen from the frame type. No GPU and no game needed.
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

static void FeedN(Detector& d, float v, unsigned n)
{
    for (unsigned i = 0; i < n; ++i)
        d.Feed(v);
}

int main()
{
    // Before a full window: detecting, and the display-scaled default applies.
    {
        Detector d;
        FeedN(d, 1300.0f, kWindow - 1);
        CHECK(d.Get() == Verdict::Detecting);
        CHECK(d.DefaultTrim() == kDisplayScaledTrim);
    }
    // NBA-like readings: display-scaled, Trim 1.
    {
        Detector d;
        FeedN(d, 0.8f, kWindow);
        CHECK(d.Get() == Verdict::DisplayScaled);
        CHECK(d.DefaultTrim() == kDisplayScaledTrim);
    }
    // RDR2-like readings: scene-referred, Trim 0.25.
    {
        Detector d;
        FeedN(d, 1300.0f, kWindow);
        CHECK(d.Get() == Verdict::SceneReferred);
        CHECK(d.DefaultTrim() == kSceneReferredTrim);
    }
    // RDR2 loading screen first (reads ~1), then gameplay: provisional display-scaled, then upgraded.
    {
        Detector d;
        FeedN(d, 0.9f, kWindow * 2);
        CHECK(d.Get() == Verdict::DisplayScaled);
        FeedN(d, 1500.0f, kWindow);
        CHECK(d.Get() == Verdict::SceneReferred);
        CHECK(d.DecidedOn() > kThreshold);
    }
    // Scene-referred is final: a later menu or dark stretch does not flip it back.
    {
        Detector d;
        FeedN(d, 1300.0f, kWindow);
        FeedN(d, 0.5f, kWindow * 3);
        CHECK(d.Get() == Verdict::SceneReferred);
    }
    // A few outliers in a display-scaled game do not flip it (median of the window).
    {
        Detector d;
        for (unsigned i = 0; i < kWindow * 2; ++i)
            d.Feed(i % 10 == 0 ? 5000.0f : 0.8f);
        CHECK(d.Get() == Verdict::DisplayScaled);
    }
    // Garbage readings are ignored.
    {
        Detector d;
        FeedN(d, NAN, kWindow);
        FeedN(d, -1.0f, kWindow);
        FeedN(d, 0.0f, kWindow);
        CHECK(d.Get() == Verdict::Detecting);
    }
    // The user's value always wins; without one the detected default is used.
    {
        Instance().Reset();
        FeedN(Instance(), 1300.0f, kWindow);
        CHECK(Effective(std::nullopt) == kSceneReferredTrim);
        CHECK(Effective(std::optional<float>(3.0f)) == 3.0f);
        Instance().Reset();
        CHECK(Instance().Get() == Verdict::Detecting);
    }

    printf(fails ? "FAILED: %d\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
