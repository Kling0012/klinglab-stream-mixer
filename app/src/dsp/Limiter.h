#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace ksm
{
// Four polyphase windowed-sinc phases. Phase zero is the original sample.
class TruePeakDetector
{
public:
    static constexpr int taps = 33, groupDelay = 16;
    void prepare() noexcept;
    void reset() noexcept;
    float push(float left, float right) noexcept;
private:
    std::array<std::array<double, taps>, 4> coefficients {};
    std::array<std::array<float, taps>, 2> history {};
    int position = 0;
};

class Limiter
{
public:
    void prepare(double sampleRate, bool preserve = false);
    void process(float& left, float& right) noexcept;
    int latencySamples() const noexcept { return delayFrames; }
    float reductionDecibels() const noexcept;
private:
    struct Frame { float left = 0.0f, right = 0.0f; };
    struct Peak { uint64_t frame = 0; float value = 0.0f; };
    TruePeakDetector detector;
    std::vector<Frame> delay;
    std::vector<Peak> maxima;
    int delayFrames = 0, position = 0;
    size_t head = 0, tail = 0;
    uint64_t frame = 0;
    float gain = 1.0f, release = 0.0f;
};
}
