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
    // Before a full window: detecting, and the pre-exposed default applies.
    {
        Detector d;
        FeedN(d, 1300.0f, kWindow - 1);
        CHECK(d.Get() == Verdict::Detecting);
        CHECK(d.DefaultTrim() == kPreExposedTrim);
    }
    // NBA-like readings: pre-exposed, Trim 1.
    {
        Detector d;
        FeedN(d, 0.8f, kWindow);
        CHECK(d.Get() == Verdict::PreExposed);
        CHECK(d.DefaultTrim() == kPreExposedTrim);
    }
    // RDR2-like readings: unexposed, Trim 0.25, but only after kConfirmWindows high windows (about 3 s).
    {
        Detector d;
        FeedN(d, 1300.0f, kWindow);
        CHECK(d.Get() == Verdict::Detecting); // one high window is not enough
        CHECK(d.DefaultTrim() == kPreExposedTrim);
        FeedN(d, 1300.0f, kWindow * (kConfirmWindows - 1) - 1);
        CHECK(d.Get() == Verdict::Detecting); // one reading short of the third window
        FeedN(d, 1300.0f, 1);
        CHECK(d.Get() == Verdict::Unexposed);
        CHECK(d.DefaultTrim() == kUnexposedTrim);
    }
    // The high windows need not be in a row: a low one in between is provisional pre-exposed and keeps the count.
    {
        Detector d;
        FeedN(d, 1300.0f, kWindow * (kConfirmWindows - 1));
        FeedN(d, 0.8f, kWindow);
        CHECK(d.Get() == Verdict::PreExposed);
        FeedN(d, 1300.0f, kWindow);
        CHECK(d.Get() == Verdict::Unexposed);
    }
    // A dim RDR2 scene hovering around the threshold (medians 77-116 measured 2026-09-26) is still recognised.
    {
        Detector d;
        for (unsigned w = 0; w < 6; ++w)
            FeedN(d, w % 2 ? 115.0f : 78.0f, kWindow);
        CHECK(d.Get() == Verdict::Unexposed);
    }
    // A pre-exposed game bright for two windows (a couple of seconds of a very bright menu) is not latched ...
    {
        Detector d;
        FeedN(d, 0.8f, kWindow);
        FeedN(d, 500.0f, kWindow * 2);
        FeedN(d, 0.8f, kWindow * 10);
        CHECK(d.Get() == Verdict::PreExposed);
        // ... a third bright second, however much later, is: the count is per session (the highest pre-exposed reading
        // measured is 6.6, far below kThreshold).
        FeedN(d, 500.0f, kWindow);
        CHECK(d.Get() == Verdict::Unexposed);
    }
    // Reset clears a half-built streak too.
    {
        Detector d;
        FeedN(d, 1300.0f, kWindow * (kConfirmWindows - 1));
        d.Reset();
        FeedN(d, 1300.0f, kWindow);
        CHECK(d.Get() == Verdict::Detecting);
    }
    // RDR2 loading screen first (reads ~1), then gameplay: provisional pre-exposed, then upgraded.
    {
        Detector d;
        FeedN(d, 0.9f, kWindow * 2);
        CHECK(d.Get() == Verdict::PreExposed);
        FeedN(d, 1500.0f, kWindow * kConfirmWindows);
        CHECK(d.Get() == Verdict::Unexposed);
        CHECK(d.DecidedOn() > kThreshold);
    }
    // Scene-referred is final: a later menu or dark stretch does not flip it back.
    {
        Detector d;
        FeedN(d, 1300.0f, kWindow * kConfirmWindows);
        FeedN(d, 0.5f, kWindow * 3);
        CHECK(d.Get() == Verdict::Unexposed);
    }
    // A few outliers in a pre-exposed game do not flip it (median of the window).
    {
        Detector d;
        for (unsigned i = 0; i < kWindow * 2; ++i)
            d.Feed(i % 10 == 0 ? 5000.0f : 0.8f);
        CHECK(d.Get() == Verdict::PreExposed);
    }
    // Measured pre-exposed games stay pre-exposed: The Witcher 3 up to 6, Cyberpunk 6.6 at start then 0.5,
    // and a reading between the old threshold (20) and the new one.
    {
        Detector d;
        FeedN(d, 6.0f, kWindow * 2);
        CHECK(d.Get() == Verdict::PreExposed);
        FeedN(d, 6.6f, kWindow);
        FeedN(d, 0.5f, kWindow);
        FeedN(d, 50.0f, kWindow);
        CHECK(d.Get() == Verdict::PreExposed);
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
        FeedN(Instance(), 1300.0f, kWindow * kConfirmWindows);
        CHECK(Effective(std::nullopt) == kUnexposedTrim);
        CHECK(Effective(std::optional<float>(3.0f)) == 3.0f);
        Instance().Reset();
        CHECK(Instance().Get() == Verdict::Detecting);
    }

    printf(fails ? "FAILED: %d\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
