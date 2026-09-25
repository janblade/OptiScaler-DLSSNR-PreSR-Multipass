#pragma once

// The Automatic exposure Trim a game gets while the user has not set one (ini AutoExposureTrim = auto).
//
// Two kinds of frame reach Automatic exposure, and they want different defaults (measured 2026-09-25 with FrameStats):
// - scene-referred (RDR2: the frame is in the game's own scene units, far above 1, and the game exposes it later; the
//   meter's base white point sits around 1000-1700): +4.3 EV (Trim 0.25) looked best; the old 5x left the model a
//   picture with a median of 0.29;
// - display-scaled (NBA 2K27: the frame never goes above 1; base white point around 0.8): +4.3 EV put yellow highlights
//   in the players' shadows, +2.3 EV (Trim 1) looked right.
// The base white point (PreExposure / automatic exposure) tells them apart with a 50x margin either side of kThreshold.
//
// The verdict only ever moves towards scene-referred. A scene-referred game shows display-range numbers on loading
// screens and menus (RDR2 read about 1 while loading), but a display-scaled game never reads in the hundreds, so a
// sustained high reading is conclusive and a low one is only provisional. Evidence: one game of each kind.
//
// Header-only and free of D3D/Vulkan types so the latch can be exercised on the host (tests/nr_auto_trim_smoke.cpp).

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <optional>

namespace DlssNrAutoTrim
{
constexpr float kSceneReferredTrim = 0.25f; // +4.3 EV on the menu's scale (neutral 5x)
constexpr float kDisplayScaledTrim = 1.0f;  // +2.3 EV
constexpr float kThreshold = 20.0f;         // base white point: RDR2 ~1000-1700, NBA 2K27 ~0.8
constexpr unsigned kWindow = 120;           // readings per decision (about 2 s)

enum class Verdict
{
    Detecting,
    DisplayScaled, // provisional: can still become SceneReferred
    SceneReferred, // final for the session
};

class Detector
{
  public:
    // One base white point per frame the Automatic meter produced. True when the verdict changed with this reading.
    bool Feed(float baseWhitePoint)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (verdict_ == Verdict::SceneReferred || !std::isfinite(baseWhitePoint) || baseWhitePoint <= 0.0f)
            return false;

        window_[filled_ % kWindow] = baseWhitePoint;
        ++filled_;

        if (filled_ < kWindow)
            return false;

        std::array<float, kWindow> sorted = window_;
        std::nth_element(sorted.begin(), sorted.begin() + kWindow / 2, sorted.end());
        const float median = sorted[kWindow / 2];
        const Verdict next = median > kThreshold ? Verdict::SceneReferred : Verdict::DisplayScaled;

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

    float DefaultTrim() const { return Get() == Verdict::SceneReferred ? kSceneReferredTrim : kDisplayScaledTrim; }

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
        verdict_ = other.verdict_;
        median_ = other.median_;
        return *this;
    }

  private:
    mutable std::mutex mutex_;
    std::array<float, kWindow> window_ {};
    unsigned long long filled_ = 0;
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
