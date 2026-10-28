#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <algorithm>

namespace ksm
{
class Meter
{
public:
    void push(float l, float r) noexcept
    {
        const std::array<float, 2> values { std::abs(l), std::abs(r) };
        for (size_t c = 0; c < 2; ++c)
        {
            auto old = peaks[c].load(std::memory_order_relaxed);
            while (old < values[c] && !peaks[c].compare_exchange_weak(old, values[c], std::memory_order_relaxed)) {}
            if (values[c] >= 1.0f) clipped.store(true, std::memory_order_relaxed);
        }
    }
    std::array<float, 2> take() noexcept
    {
        return { peaks[0].exchange(0.0f, std::memory_order_relaxed),
                 peaks[1].exchange(0.0f, std::memory_order_relaxed) };
    }
    std::array<float, 2> current() const noexcept
    {
        return { peaks[0].load(std::memory_order_relaxed), peaks[1].load(std::memory_order_relaxed) };
    }
    bool isClipped() const noexcept { return clipped.load(std::memory_order_relaxed); }
    void clearClip() noexcept { clipped.store(false, std::memory_order_relaxed); }
private:
    std::array<std::atomic<float>, 2> peaks {};
    std::atomic<bool> clipped { false };
};
}
