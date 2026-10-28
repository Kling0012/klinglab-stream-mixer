#include "Limiter.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace ksm
{
void TruePeakDetector::prepare() noexcept
{
    for (size_t phase = 0; phase < coefficients.size(); ++phase)
    {
        double sum = 0.0;
        for (int tap = 0; tap < taps; ++tap)
        {
            const double x = static_cast<double>(tap - groupDelay) - static_cast<double>(phase) / 4.0;
            const double sinc = std::abs(x) < 1.0e-12 ? 1.0 : std::sin(std::numbers::pi * x) / (std::numbers::pi * x);
            const double window = 0.42 - 0.5 * std::cos(2.0 * std::numbers::pi * tap / (taps - 1))
                + 0.08 * std::cos(4.0 * std::numbers::pi * tap / (taps - 1));
            coefficients[phase][static_cast<size_t>(tap)] = sinc * window;
            sum += sinc * window;
        }
        for (auto& coefficient : coefficients[phase]) coefficient /= sum;
    }
    reset();
}
void TruePeakDetector::reset() noexcept { history = {}; position = 0; }
float TruePeakDetector::push(float left, float right) noexcept
{
    history[0][static_cast<size_t>(position)] = left; history[1][static_cast<size_t>(position)] = right;
    double peak = std::max(std::abs(left), std::abs(right));
    for (const auto& phase : coefficients)
    {
        std::array<double, 2> value {};
        int index = position;
        for (size_t tap = 0; tap < phase.size(); ++tap)
        {
            value[0] += history[0][static_cast<size_t>(index)] * phase[tap];
            value[1] += history[1][static_cast<size_t>(index)] * phase[tap];
            if (--index < 0) index = taps - 1;
        }
        peak = std::max({peak, std::abs(value[0]), std::abs(value[1])});
    }
    position = (position + 1) % taps;
    return static_cast<float>(peak);
}
void Limiter::prepare(double sampleRate, bool preserve)
{
    const auto old = preserve ? delay : std::vector<Frame>();
    const int oldPosition = position;
    float pendingPeak = 0.0f;
    if (preserve)
        for (size_t i = head; i != tail; i = (i + 1) % maxima.size())
            pendingPeak = std::max(pendingPeak, maxima[i].value);
    delayFrames = static_cast<int>(std::ceil(sampleRate * 0.004));
    delay.assign(static_cast<size_t>(delayFrames + 1), {});
    if (!old.empty())
        for (size_t i = 0; i < delay.size(); ++i)
        {
            const double cursor = static_cast<double>(i) * static_cast<double>(old.size()) / static_cast<double>(delay.size());
            const auto a = static_cast<size_t>(cursor);
            const auto first = old[(static_cast<size_t>(oldPosition) + a) % old.size()];
            const auto second = old[(static_cast<size_t>(oldPosition) + a + 1) % old.size()];
            const float fraction = static_cast<float>(cursor - static_cast<double>(a));
            delay[i] = { first.left + (second.left - first.left) * fraction, first.right + (second.right - first.right) * fraction };
        }
    maxima.assign(static_cast<size_t>(delayFrames + 2), {});
    position = 0; head = tail = 0; frame = 0;
    if (pendingPeak > 0.0f) { maxima[0] = {0, pendingPeak}; tail = 1; }
    if (!preserve) { gain = 1.0f; detector.prepare(); }
    release = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * 0.040)));
}
void Limiter::process(float& left, float& right) noexcept
{
    if (delay.empty()) return;
    const float peak = detector.push(left, right);
    const auto next = [this](size_t index) { return (index + 1) % maxima.size(); };
    const auto previous = [this](size_t index) { return (index + maxima.size() - 1) % maxima.size(); };
    while (head != tail && frame > static_cast<uint64_t>(delayFrames)
           && maxima[head].frame < frame - static_cast<uint64_t>(delayFrames)) head = next(head);
    while (head != tail && maxima[previous(tail)].value <= peak) tail = previous(tail);
    maxima[tail] = {frame, peak}; tail = next(tail);
    // Reserve 0.15 dB for reconstruction and the slowly changing release envelope.
    constexpr float ceiling = 0.8759917f; // -1.15 dBFS
    const float target = maxima[head].value > ceiling ? ceiling / maxima[head].value : 1.0f;
    gain = target < gain ? target : std::min(target, gain + release * (1.0f - gain));
    delay[static_cast<size_t>(position)] = {left, right};
    position = (position + 1) % static_cast<int>(delay.size());
    const auto output = delay[static_cast<size_t>(position)];
    left = output.left * gain; right = output.right * gain;
    ++frame;
}
float Limiter::reductionDecibels() const noexcept { return -20.0f * std::log10(std::max(1.0e-12f, gain)); }
}
