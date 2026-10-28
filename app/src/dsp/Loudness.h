#pragma once

#include "Limiter.h"
#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <vector>

namespace ksm
{
// Stereo BS.1770 K weighting, 400 ms integration blocks at 100 ms hops.
class LoudnessMeter
{
public:
    void prepare(double sampleRate, bool preserve = false);
    void reset() noexcept;
    void push(float left, float right) noexcept;
    float shortTerm() const noexcept;
    float integrated() const noexcept;
    double audibleSeconds() const noexcept { return static_cast<double>(audibleFrames) / rate; }
    static float fromEnergy(double energy) noexcept;
private:
    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        std::array<double, 2> z1 {}, z2 {};
        double process(double input, size_t channel) noexcept;
        void reset() noexcept { z1 = {}; z2 = {}; }
    } shelf, highpass;
    double rate = 48000.0, binEnergy = 0.0, rollingEnergy = 0.0;
    int hopFrames = 4800, binFrames = 0, binIndex = 0, completedBins = 0;
    uint64_t audibleFrames = 0;
    std::array<double, 30> bins {};
    // A learn lasts at most 20 s. The renderer may meter longer files.
    std::vector<double> histogramEnergy;
    std::vector<uint64_t> histogramCount;
};

class Loudness
{
public:
    enum class Mode { stream, cd };
    std::atomic<bool> enabled { false };
    std::atomic<Mode> mode { Mode::stream };
    std::atomic<float> learnedDecibels { 0.0f };
    void prepare(double sampleRate, bool preserve = false);
    void beginBlock() noexcept;
    void requestLearn() noexcept;
    void requestStop() noexcept;
    bool isLearning() const noexcept { const auto command = learnCommand.load(std::memory_order_acquire); return command != 3 && (learning.load(std::memory_order_acquire) || command == 1 || command == 4); }
    void cancelLearn() noexcept { learnCommand.store(3, std::memory_order_release); }
    bool stopPending() const noexcept { const auto command = learnCommand.load(std::memory_order_acquire); return command == 2 || command == 4; }
    bool tooQuiet() const noexcept { return quiet.load(std::memory_order_relaxed); }
    float learnSeconds() const noexcept { const auto command = learnCommand.load(std::memory_order_acquire); return command == 1 || command == 4 ? 0.0f : elapsed.load(std::memory_order_relaxed); }
    float shortTerm() const noexcept { return displayedLUFS.load(std::memory_order_relaxed); }
    float reductionDecibels() const noexcept { return displayedReduction.load(std::memory_order_relaxed); }
    uint32_t revision() const noexcept { return stateRevision.load(std::memory_order_acquire); }
    int latencySamples() const noexcept { return activeLatency.load(std::memory_order_relaxed); }
    // Only INST + VOCAL enter here; MASTER is included in the measurement only.
    void processBus(float& left, float& right, float masterGain) noexcept;
    // BGM + SE and MASTER have been applied before this call.
    void processOutput(float& left, float& right) noexcept;
    juce::var saveState() const;
    juce::Result restoreState(const juce::var&);
private:
    struct Ramp
    {
        float current = 0.0f, target = 0.0f, step = 0.0f;
        int remaining = 0;
        void set(float value, int samples) noexcept;
        float next() noexcept;
    } gainDb, wet, cd;
    struct Compressor
    {
        float envelope = 0.0f, attack = 0.0f, release = 0.0f;
        float process(float peak, float threshold, float ratio) noexcept;
    } first, second;
    Limiter limiter;
    LoudnessMeter learnMeter, outputMeter;
    double rate = 48000.0;
    uint64_t learnFrames = 0;
    int transitionFrames = 24000, publishFrames = 0;
    std::atomic<int> learnCommand { 0 };
    std::atomic<bool> learning { false }, quiet { false };
    std::atomic<float> elapsed { 0.0f }, displayedLUFS { -100.0f }, displayedReduction { 0.0f };
    std::atomic<uint32_t> stateRevision { 0 };
    std::atomic<int> activeLatency { 0 };
    float currentWet = 0.0f, currentCD = 0.0f, reductionPeak = 0.0f;
    void finishLearn() noexcept;
};
}
