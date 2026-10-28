#include "Loudness.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace ksm
{
double LoudnessMeter::Biquad::process(double input, size_t channel) noexcept
{
    const double output = b0 * input + z1[channel];
    z1[channel] = b1 * input - a1 * output + z2[channel];
    z2[channel] = b2 * input - a2 * output;
    return output;
}
void LoudnessMeter::prepare(double sampleRate, bool preserve)
{
    const double ratio = sampleRate / rate;
    if (preserve)
    {
        binEnergy *= ratio;
        binFrames = static_cast<int>(std::round(binFrames * ratio));
        audibleFrames = static_cast<uint64_t>(std::round(static_cast<double>(audibleFrames) * ratio));
    }
    rate = sampleRate; hopFrames = static_cast<int>(std::round(rate * 0.1));
    // BS.1770 shelf and RLB highpass, bilinear-transformed for the actual rate.
    const double k = std::tan(std::numbers::pi * 1681.974450955533 / rate);
    const double vh = std::pow(10.0, 3.999843853973347 / 20.0), vb = std::pow(vh, 0.4996667741545416);
    const double q = 0.7071752369554196, denominator = 1.0 + k / q + k * k;
    shelf.b0 = (vh + vb * k / q + k * k) / denominator;
    shelf.b1 = 2.0 * (k * k - vh) / denominator;
    shelf.b2 = (vh - vb * k / q + k * k) / denominator;
    shelf.a1 = 2.0 * (k * k - 1.0) / denominator;
    shelf.a2 = (1.0 - k / q + k * k) / denominator;
    const double h = std::tan(std::numbers::pi * 38.13547087602444 / rate), hq = 0.5003270373238773;
    const double hd = 1.0 + h / hq + h * h;
    highpass.b0 = 1.0; highpass.b1 = -2.0; highpass.b2 = 1.0;
    highpass.a1 = 2.0 * (h * h - 1.0) / hd;
    highpass.a2 = (1.0 - h / hq + h * h) / hd;
    if (!preserve)
    {
        histogramEnergy.assign(20001, 0.0); histogramCount.assign(20001, 0);
        reset();
    }
}
void LoudnessMeter::reset() noexcept
{
    shelf.reset(); highpass.reset(); bins = {}; binEnergy = rollingEnergy = 0.0;
    binFrames = binIndex = completedBins = 0; audibleFrames = 0;
    std::fill(histogramEnergy.begin(), histogramEnergy.end(), 0.0);
    std::fill(histogramCount.begin(), histogramCount.end(), 0);
}
float LoudnessMeter::fromEnergy(double energy) noexcept
{
    return energy > 1.0e-20 ? static_cast<float>(-0.691 + 10.0 * std::log10(energy)) : -100.0f;
}
void LoudnessMeter::push(float left, float right) noexcept
{
    const double l = highpass.process(shelf.process(left, 0), 0), r = highpass.process(shelf.process(right, 1), 1);
    binEnergy += l * l + r * r;
    if (++binFrames < hopFrames) return;
    const double energy = binEnergy / hopFrames;
    if (fromEnergy(energy) > -70.0f) audibleFrames += static_cast<uint64_t>(hopFrames);
    rollingEnergy += energy - bins[static_cast<size_t>(binIndex)];
    bins[static_cast<size_t>(binIndex)] = energy;
    binIndex = (binIndex + 1) % 30;
    completedBins = std::min(30, completedBins + 1);
    if (completedBins >= 4)
    {
        double block = 0.0;
        for (int n = 1; n <= 4; ++n) block += bins[static_cast<size_t>((binIndex + 30 - n) % 30)];
        block *= 0.25;
        const float level = fromEnergy(block);
        if (level > -70.0f && !histogramEnergy.empty())
        {
            const auto index = static_cast<size_t>(std::clamp(static_cast<int>(std::round((level + 100.0f) * 100.0f)), 0, 20000));
            histogramEnergy[index] += block; ++histogramCount[index];
        }
    }
    binEnergy = 0.0; binFrames = 0;
}
float LoudnessMeter::shortTerm() const noexcept { return fromEnergy(std::max(0.0, rollingEnergy) / 30.0); }
float LoudnessMeter::integrated() const noexcept
{
    double sum = 0.0; uint64_t count = 0;
    for (size_t n = 0; n < histogramEnergy.size(); ++n) { sum += histogramEnergy[n]; count += histogramCount[n]; }
    if (count == 0) return -100.0f;
    const float gate = std::max(-70.0f, fromEnergy(sum / static_cast<double>(count)) - 10.0f);
    sum = 0.0; count = 0;
    for (size_t n = 0; n < histogramEnergy.size(); ++n)
        if (static_cast<double>(n) * 0.01 - 100.0 > gate)
        { sum += histogramEnergy[n]; count += histogramCount[n]; }
    return count > 0 ? fromEnergy(sum / static_cast<double>(count)) : -100.0f;
}
void Loudness::Ramp::set(float value, int samples) noexcept
{
    if (juce::exactlyEqual(value, target)) return;
    target = value; remaining = samples;
    step = (target - current) / static_cast<float>(samples);
}
float Loudness::Ramp::next() noexcept
{
    if (remaining > 0) { current += step; if (--remaining == 0) current = target; }
    return current;
}
float Loudness::Compressor::process(float peak, float threshold, float ratio) noexcept
{
    const float level = 20.0f * std::log10(std::max(1.0e-12f, peak));
    // 6 dB soft knee, stereo-linked envelope.
    const float over = level - threshold;
    const float reduction = over <= -3.0f ? 0.0f : over >= 3.0f ? over * (1.0f - 1.0f / ratio)
        : (over + 3.0f) * (over + 3.0f) * (1.0f - 1.0f / ratio) / 12.0f;
    envelope += (reduction > envelope ? attack : release) * (reduction - envelope);
    return std::pow(10.0f, -envelope / 20.0f);
}
void Loudness::prepare(double sampleRate, bool preserve)
{
    if (preserve && std::abs(sampleRate - rate) < 0.5) return;
    const double ratio = sampleRate / rate;
    rate = sampleRate; transitionFrames = static_cast<int>(std::round(rate * 0.5));
    limiter.prepare(rate, preserve); learnMeter.prepare(rate, preserve); outputMeter.prepare(rate, preserve);
    const bool on = enabled.load(); const bool compact = mode.load() == Mode::cd;
    if (preserve)
    {
        learnFrames = static_cast<uint64_t>(std::round(static_cast<double>(learnFrames) * ratio));
        publishFrames = static_cast<int>(std::round(publishFrames * ratio));
        for (auto* ramp : { &gainDb, &wet, &cd })
        {
            ramp->remaining = static_cast<int>(std::round(ramp->remaining * ratio));
            if (ramp->remaining > 0) ramp->step = (ramp->target - ramp->current) / static_cast<float>(ramp->remaining);
            else { ramp->current = ramp->target; ramp->step = 0.0f; }
        }
    }
    else
    {
        gainDb = {}; gainDb.current = gainDb.target = on ? learnedDecibels.load() + (compact ? 4.5f : 0.0f) : 0.0f;
        wet = {}; wet.current = wet.target = on ? 1.0f : 0.0f;
        cd = {}; cd.current = cd.target = on && compact ? 1.0f : 0.0f;
        first = {}; second = {};
        learning.store(false); learnCommand.store(0); elapsed.store(0.0f); quiet.store(false);
        currentWet = wet.current; currentCD = cd.current; reductionPeak = 0.0f; publishFrames = 0;
    }
    first.attack = static_cast<float>(1.0 - std::exp(-1.0 / (rate * 0.010)));
    first.release = static_cast<float>(1.0 - std::exp(-1.0 / (rate * 0.180)));
    second.attack = static_cast<float>(1.0 - std::exp(-1.0 / (rate * 0.002)));
    second.release = static_cast<float>(1.0 - std::exp(-1.0 / (rate * 0.080)));
    activeLatency.store(on ? limiter.latencySamples() : 0);
}
void Loudness::requestLearn() noexcept { learnCommand.store(1, std::memory_order_release); }
void Loudness::requestStop() noexcept
{
    int command = learnCommand.load(std::memory_order_acquire);
    while (command != 2 && command != 3 && command != 4)
        if (learnCommand.compare_exchange_weak(command, command == 1 ? 4 : 2, std::memory_order_acq_rel)) break;
}
void Loudness::beginBlock() noexcept
{
    int command = learnCommand.load(std::memory_order_acquire);
    if (command == 1 || command == 4)
    {
        learnMeter.reset(); learnFrames = 0; elapsed.store(0.0f); quiet.store(false); learning.store(true, std::memory_order_release);
        if (command == 4) finishLearn();
    }
    else if (command == 2 && isLearning()) finishLearn();
    else if (command == 3) { learning.store(false, std::memory_order_release); quiet.store(false); elapsed.store(0.0f); }
    // Keep the command visible until its result is published to the message thread.
    learnCommand.compare_exchange_strong(command, 0, std::memory_order_acq_rel);
    const bool on = enabled.load(std::memory_order_relaxed), compact = mode.load(std::memory_order_relaxed) == Mode::cd;
    const auto learned = learnedDecibels.load(std::memory_order_relaxed);
    gainDb.set(on ? learned + (compact ? 4.5f : 0.0f) : 0.0f, transitionFrames);
    wet.set(on ? 1.0f : 0.0f, transitionFrames); cd.set(on && compact ? 1.0f : 0.0f, transitionFrames);
}
void Loudness::finishLearn() noexcept
{
    if (learnMeter.audibleSeconds() + 1.0e-6 < 3.0)
        quiet.store(true, std::memory_order_relaxed);
    else
    {
        learnedDecibels.store(-11.5f - learnMeter.integrated(), std::memory_order_relaxed);
        quiet.store(false, std::memory_order_relaxed);
        stateRevision.fetch_add(1, std::memory_order_release);
    }
    learning.store(false, std::memory_order_release);
}
void Loudness::processBus(float& left, float& right, float masterGain) noexcept
{
    if (isLearning())
    {
        learnMeter.push(left * masterGain, right * masterGain);
        ++learnFrames;
        if (learnFrames % static_cast<uint64_t>(std::max(1, static_cast<int>(rate * 0.05))) == 0)
            elapsed.store(static_cast<float>(static_cast<double>(learnFrames) / rate), std::memory_order_relaxed);
        if (learnFrames >= static_cast<uint64_t>(std::round(rate * 20.0))) finishLearn();
    }
    currentWet = wet.next(); currentCD = cd.next();
    const float gain = std::pow(10.0f, gainDb.next() / 20.0f);
    left *= gain; right *= gain;
    const float firstGain = first.process(std::max(std::abs(left), std::abs(right)), -5.0f, 1.4f);
    const float secondGain = second.process(std::max(std::abs(left), std::abs(right)) * firstGain, -2.0f, 1.8f);
    const float compression = 1.0f + currentCD * (firstGain * secondGain - 1.0f);
    left *= compression; right *= compression;
}
void Loudness::processOutput(float& left, float& right) noexcept
{
    float limitedL = left, limitedR = right;
    limiter.process(limitedL, limitedR);
    if (currentWet >= 1.0f) { left = limitedL; right = limitedR; }
    else if (currentWet > 0.0f)
    { left += currentWet * (limitedL - left); right += currentWet * (limitedR - right); }
    activeLatency.store(currentWet > 0.0f ? limiter.latencySamples() : 0, std::memory_order_relaxed);
    outputMeter.push(left, right);
    reductionPeak = std::max(reductionPeak, currentWet * limiter.reductionDecibels());
    if (++publishFrames >= static_cast<int>(rate * 0.033))
    {
        displayedLUFS.store(outputMeter.shortTerm(), std::memory_order_relaxed);
        displayedReduction.store(reductionPeak, std::memory_order_relaxed);
        reductionPeak = 0.0f; publishFrames = 0;
    }
}
juce::var Loudness::saveState() const
{
    auto* data = new juce::DynamicObject();
    data->setProperty("version", 1); data->setProperty("enabled", enabled.load());
    data->setProperty("mode", mode.load() == Mode::cd ? "CD" : "STREAM");
    data->setProperty("learnedDecibels", learnedDecibels.load());
    return juce::var(data);
}
juce::Result Loudness::restoreState(const juce::var& state)
{
    if (state.isVoid()) return juce::Result::ok();
    const auto value = state["learnedDecibels"];
    const double db = static_cast<double>(value); const auto text = state["mode"].toString();
    if (!state.isObject() || static_cast<int>(state["version"]) != 1 || !state["enabled"].isBool()
        || !(value.isDouble() || value.isInt() || value.isInt64()) || !std::isfinite(db) || db < -120.0 || db > 120.0
        || (text != "STREAM" && text != "CD"))
        return juce::Result::fail(juce::String::fromUTF8("LOUD の保存データを読めません。現在の設定を続けます。"));
    learnedDecibels.store(static_cast<float>(db)); mode.store(text == "CD" ? Mode::cd : Mode::stream);
    enabled.store(static_cast<bool>(state["enabled"])); stateRevision.fetch_add(1, std::memory_order_release);
    return juce::Result::ok();
}
}
