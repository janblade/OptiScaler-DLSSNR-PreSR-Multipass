#pragma once

// The Automatic exposure Trim a game gets while the user has not set one (ini AutoExposureTrim = auto).
//
// Two kinds of frame reach Automatic exposure, and they want different defaults (measured 2026-09-25 with FrameStats).
// Both are linear HDR, scene-referred in the colour sense and tone-mapped later; what differs is whether the game has
// already applied its own exposure before handing the frame to the upscaler:
// - unexposed (RDR2: raw scene luminance, the game's exposure of ~0.007 comes later; the meter's base white point sits
//   around 900-1700): +4.3 EV (Trim 0.25) looked best; the old 5x left the model a picture with a median of 0.29;
// - pre-exposed (NBA 2K27 ~0.2-0.8, Cyberpunk 2077 ~0.5 with 6.6 in the first seconds, The Witcher 3 ~2-6): +4.3 EV put
//   yellow highlights in NBA's player shadows, +2.3 EV (Trim 1) looked right in all three.
// The base white point (PreExposure / automatic exposure) tells them apart. kThreshold sits ~17x above the highest
// pre-exposed reading and ~9x below RDR2's gameplay (raised from 20 after The Witcher 3 reached 6).
//
// The verdict only ever moves towards unexposed. An unexposed game shows pre-exposed numbers on loading screens and
// menus (RDR2 read about 1 while loading), but a pre-exposed game never reads in the hundreds, so a sustained high
// reading is conclusive and a low one is only provisional. Evidence: one unexposed game, three pre-exposed.
// "Sustained" is kConfirmWindows windows in a row (about 6 s), so a few seconds of an unusually bright menu or
// cutscene in a pre-exposed game cannot latch it; any low window in between starts the count again. RDR2's gameplay
// windows (medians 900-1700) all pass, so it only reaches its default ~4 s later than with a single window.
//
// Header-only and free of D3D/Vulkan types so the latch can be exercised on the host (tests/nr_auto_trim_smoke.cpp).

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <optional>

namespace DlssNrAutoTrim
{
constexpr float kUnexposedTrim = 0.25f; // +4.3 EV on the menu's scale (neutral 5x)
constexpr float kPreExposedTrim = 1.0f;  // +2.3 EV
constexpr float kThreshold = 100.0f;        // base white point: RDR2 ~900-1700 in gameplay; pre-exposed games up to ~6 (The Witcher 3)
constexpr unsigned kWindow = 120;           // readings per decision (about 2 s)
constexpr unsigned kConfirmWindows = 3;     // high windows in a row before the verdict becomes Unexposed (about 6 s)

enum class Verdict
{
    Detecting,
    PreExposed, // provisional: can still become Unexposed
    Unexposed,  // final for the session
};

class Detector
{
  public:
    // One base white point per frame the Automatic meter produced. True when the verdict changed with this reading.
    bool Feed(float baseWhitePoint)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (verdict_ == Verdict::Unexposed || !std::isfinite(baseWhitePoint) || baseWhitePoint <= 0.0f)
            return false;

        window_[filled_ % kWindow] = baseWhitePoint;
        ++filled_;

        if (filled_ % kWindow != 0) // one decision per full window
            return false;

        std::array<float, kWindow> sorted = window_;
        std::nth_element(sorted.begin(), sorted.begin() + kWindow / 2, sorted.end());
        const float median = sorted[kWindow / 2];
        Verdict next = Verdict::PreExposed;

        if (median > kThreshold)
        {
            if (++highStreak_ < kConfirmWindows)
                return false; // not yet conclusive: keep the current verdict (Detecting or PreExposed)

            next = Verdict::Unexposed;
        }
        else
        {
            highStreak_ = 0;
        }

        if (next == verdict_)
            return false;

        verdict_ = next;
        median_ = median;
        return true;
    }

    Verdict Get() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return verdict_;
    }

    // The base white point the current verdict was decided on (0 while detecting).
    float DecidedOn() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return median_;
    }

    float DefaultTrim() const { return Get() == Verdict::Unexposed ? kUnexposedTrim : kPreExposedTrim; }

    void Reset()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        *this = Detector {};
    }

    Detector() = default;
    Detector& operator=(const Detector& other)
    {
        window_ = other.window_;
        filled_ = other.filled_;
        highStreak_ = other.highStreak_;
        verdict_ = other.verdict_;
        median_ = other.median_;
        return *this;
    }

  private:
    mutable std::mutex mutex_;
    std::array<float, kWindow> window_ {};
    unsigned long long filled_ = 0;
    unsigned highStreak_ = 0; // consecutive windows above kThreshold
    Verdict verdict_ = Verdict::Detecting;
    float median_ = 0.0f;
};

inline Detector& Instance()
{
    static Detector detector;
    return detector;
}

// The Trim in force: the user's own when they set one, otherwise the detected default.
inline float Effective(const std::optional<float>& userTrim)
{
    return userTrim.has_value() ? *userTrim : Instance().DefaultTrim();
}
} // namespace DlssNrAutoTrim
