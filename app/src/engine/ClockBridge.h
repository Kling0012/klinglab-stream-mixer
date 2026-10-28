#pragma once

#include "Router.h"
#include <numbers>
#include <cstdint>

namespace ksm
{
// One producer and one consumer. A full ring drops new frames; only the consumer advances readPos.
class ClockBridge
{
public:
    static constexpr uint64_t capacity = 32768;
    void prepare(double sourceRate, double destinationRate, unsigned bufferFrames) noexcept
    {
        writePos.store(0); readPos.store(0);
        position = 0.0;
        baseRatio = sourceRate / destinationRate;
        ratio = baseRatio;
        target = std::clamp(static_cast<double>(bufferFrames) * 4.0 * std::max(1.0, baseRatio), 256.0, 8192.0);
        primed = false;
        overflows.store(0); underflows.store(0);
    }
    void push(Stereo sample) noexcept
    {
        const auto w = writePos.load(std::memory_order_relaxed);
        if (w - readPos.load(std::memory_order_acquire) >= capacity)
        {
            overflows.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        ring[w & (capacity - 1)] = sample;
        writePos.store(w + 1, std::memory_order_release);
    }
    Stereo pull() noexcept
    {
        const auto w = writePos.load(std::memory_order_acquire);
        auto r = readPos.load(std::memory_order_relaxed);
        const double available = static_cast<double>(w - r);
        if (!primed)
        {
            if (available < target + 32.0) return {};
            position = static_cast<double>(r + 8);
            primed = true;
        }
        if (available > target * 3.0 + 64.0)
        {
            r = w - static_cast<uint64_t>(target);
            position = static_cast<double>(r + 8);
            readPos.store(r, std::memory_order_release);
        }
        const auto centre = static_cast<uint64_t>(position);
        if (centre + 9 >= w)
        {
            primed = false;
            readPos.store(w, std::memory_order_release);
            underflows.fetch_add(1, std::memory_order_relaxed);
            return {};
        }
        const double error = (static_cast<double>(w) - position - target) / target;
        const double desired = baseRatio * (1.0 + std::clamp(error * 0.001, -0.005, 0.005));
        ratio += 0.00002 * (desired - ratio);
        Stereo out;
        double sum = 0.0;
        const double cutoff = std::min(1.0, 1.0 / baseRatio) * 0.92;
        for (int tap = -7; tap <= 8; ++tap)
        {
            const auto index = static_cast<uint64_t>(static_cast<int64_t>(centre) + tap);
            const double x = static_cast<double>(index) - position;
            const double sinc = std::abs(x) < 1.0e-9 ? cutoff : std::sin(std::numbers::pi * cutoff * x) / (std::numbers::pi * x);
            const double weight = sinc * (0.5 + 0.5 * std::cos(std::numbers::pi * x / 9.0));
            const auto s = ring[index & (capacity - 1)];
            out.l += s.l * static_cast<float>(weight);
            out.r += s.r * static_cast<float>(weight);
            sum += weight;
        }
        position += ratio;
        const auto release = static_cast<uint64_t>(position) - 8;
        readPos.store(std::min(release, w), std::memory_order_release);
        return out * static_cast<float>(1.0 / sum);
    }
    std::atomic<uint64_t> overflows { 0 }, underflows { 0 };
private:
    std::array<Stereo, capacity> ring {};
    alignas(64) std::atomic<uint64_t> writePos { 0 };
    alignas(64) std::atomic<uint64_t> readPos { 0 };
    double position = 0.0, baseRatio = 1.0, ratio = 1.0, target = 512.0;
    bool primed = false;
};
}
