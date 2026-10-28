#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>

namespace ksm
{
class Fader
{
public:
    explicit Fader(float initial = 0.0f, float upper = 6.0f) : decibels(initial), targetGain(toGain(initial)), maximum(upper) {}
    void setDecibels(float value) noexcept
    {
        const auto db = std::isfinite(value) ? std::clamp(value, -60.0f, maximum) : -60.0f;
        decibels.store(db, std::memory_order_relaxed);
        targetGain.store(toGain(db), std::memory_order_relaxed);
    }
    float getDecibels() const noexcept { return decibels.load(std::memory_order_relaxed); }
    void prepare(double rate, bool preserve = false) noexcept
    {
        smoothing = 1.0f - std::exp(-1.0f / static_cast<float>(rate * 0.020));
        if (!preserve) current = gain();
    }
    float next() noexcept
    {
        current += smoothing * (gain() - current);
        if (current < 1.0e-12f) current = 0.0f;
        return current;
    }
    static float toGain(float db) noexcept { return db <= -60.0f ? 0.0f : std::pow(10.0f, db / 20.0f); }

private:
    float gain() const noexcept { return targetGain.load(std::memory_order_relaxed); }
    std::atomic<float> decibels, targetGain;
    float maximum, current = 1.0f, smoothing = 0.001f;
};
}
