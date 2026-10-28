#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ksm
{
class NR
{
public:
    static constexpr size_t bandCount = 24;
    static constexpr int latencySamples = 0;
    std::atomic<bool> enabled { false };
    std::atomic<float> reductionDecibels { 6.0f };

    void prepare(double sampleRate, bool preserve = false) noexcept;
    void beginBlock() noexcept;
    void process(float& left, float& right) noexcept;
    void requestLearn() noexcept { learnRequested.store(true, std::memory_order_release); }
    bool isLearning() const noexcept { return learning.load(std::memory_order_acquire) || learnRequested.load(std::memory_order_acquire); }
    bool hasProfile() const noexcept { return learned.load(std::memory_order_acquire); }
    float learnProgress() const noexcept { return progress.load(std::memory_order_relaxed); }
    uint32_t profileRevision() const noexcept { return revision.load(std::memory_order_acquire); }
    juce::var saveState() const;
    juce::Result restoreState(const juce::var&);
    double bandUpperFrequency(size_t band) const noexcept;

private:
    // Parallel lowpasses form complementary differences. Their sum is the current
    // input, not an allpass-delayed copy. Synthesis applies corrections to dry.
    std::array<float, bandCount - 1> coefficients {};
    std::array<std::array<float, bandCount - 1>, 2> lowpass {};
    std::array<float, bandCount> envelope {}, gains {}, noiseFloor {};
    std::array<double, bandCount> accumulation {};
    std::array<std::atomic<float>, bandCount> publishedFloor {};
    std::array<std::atomic<float>, bandCount> pendingFloor {};
    std::atomic<bool> learned { false }, learning { false }, learnRequested { false };
    std::atomic<float> progress { 0.0f };
    std::atomic<uint32_t> revision { 0 }, pendingRevision { 0 }, acknowledged { 0 }, publication { 0 };
    std::atomic<bool> pendingLearned { false };
    // Only the message thread serializes state. Keep its last complete snapshot
    // if the audio callback is publishing a new profile at that instant.
    mutable std::array<float, bandCount> savedFloor {};
    mutable bool savedLearned = false;
    uint32_t appliedRevision = 0;
    int learnFrames = 0, framesToLearn = 48000;
    double rate = 48000.0;
    float envelopeAttack = 0.0f, envelopeRelease = 0.0f, gainAttack = 0.0f, gainRelease = 0.0f, amountSmoothing = 0.0f;
    float currentAmount = 6.0f, blockAmount = 6.0f;
    bool blockEnabled = false, activeProfile = false, collecting = false;
};
}
