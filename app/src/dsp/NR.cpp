#include "NR.h"

namespace ksm
{
double NR::bandUpperFrequency(size_t band) const noexcept
{
    if (band >= bandCount - 1) return rate * 0.5;
    return 80.0 * std::pow(std::min(16000.0, rate * 0.42) / 80.0, static_cast<double>(band) / static_cast<double>(bandCount - 2));
}
void NR::prepare(double sampleRate, bool preserve) noexcept
{
    const double previousRate = rate;
    rate = std::isfinite(sampleRate) && sampleRate >= 8000.0 ? sampleRate : 48000.0;
    for (size_t b = 0; b < coefficients.size(); ++b)
        coefficients[b] = static_cast<float>(1.0 - std::exp(-2.0 * juce::MathConstants<double>::pi * bandUpperFrequency(b) / rate));
    if (!preserve)
    {
        for (auto& channel : lowpass) channel.fill(0.0f);
        envelope.fill(0.0f); gains.fill(1.0f); accumulation.fill(0.0);
    }
    else if (collecting)
    {
        const double ratio = rate / previousRate;
        learnFrames = static_cast<int>(std::round(learnFrames * ratio));
        for (auto& value : accumulation) value *= ratio;
    }
    auto timeCoefficient = [this](double seconds) { return static_cast<float>(1.0 - std::exp(-1.0 / (rate * seconds))); };
    envelopeAttack = timeCoefficient(0.002); envelopeRelease = timeCoefficient(0.030);
    gainAttack = timeCoefficient(0.001); gainRelease = timeCoefficient(0.060); amountSmoothing = timeCoefficient(0.020);
    framesToLearn = static_cast<int>(std::round(rate));
    if (!preserve)
    {
        collecting = false; learnFrames = 0; learning.store(false, std::memory_order_release); progress.store(0.0f);
        currentAmount = reductionDecibels.load(std::memory_order_relaxed);
    }
}
void NR::beginBlock() noexcept
{
    const auto pending = pendingRevision.load(std::memory_order_seq_cst);
    if (pending != appliedRevision && (pending & 1u) == 0)
    {
        std::array<float, bandCount> copy;
        for (size_t b = 0; b < bandCount; ++b) copy[b] = pendingFloor[b].load(std::memory_order_seq_cst);
        const bool valid = pendingLearned.load(std::memory_order_seq_cst);
        if (pendingRevision.load(std::memory_order_seq_cst) == pending)
        {
            noiseFloor = copy; activeProfile = valid; appliedRevision = pending;
            publication.fetch_add(1, std::memory_order_seq_cst);
            for (size_t b = 0; b < bandCount; ++b) publishedFloor[b].store(copy[b], std::memory_order_seq_cst);
            learned.store(valid, std::memory_order_seq_cst);
            publication.fetch_add(1, std::memory_order_seq_cst);
            acknowledged.store(pending, std::memory_order_release);
            collecting = false; learning.store(false, std::memory_order_release); progress.store(0.0f);
            gains.fill(1.0f); revision.fetch_add(1, std::memory_order_release);
        }
    }
    if (learnRequested.exchange(false, std::memory_order_acq_rel))
    {
        accumulation.fill(0.0); learnFrames = 0; collecting = true;
        progress.store(0.0f); learning.store(true, std::memory_order_release);
    }
    blockEnabled = enabled.load(std::memory_order_relaxed);
    const float value = reductionDecibels.load(std::memory_order_relaxed);
    blockAmount = std::isfinite(value) ? std::clamp(value, 0.0f, 18.0f) : 0.0f;
}
void NR::process(float& left, float& right) noexcept
{
    const std::array<float, 2> input { std::isfinite(left) ? left : 0.0f, std::isfinite(right) ? right : 0.0f };
    std::array<std::array<float, bandCount>, 2> bands;
    for (size_t c = 0; c < 2; ++c)
    {
        float previous = 0.0f;
        for (size_t b = 0; b < coefficients.size(); ++b)
        {
            auto& state = lowpass[c][b];
            state += coefficients[b] * (input[c] - state);
            if (std::abs(state) < 1.0e-20f) state = 0.0f;
            bands[c][b] = state - previous; previous = state;
        }
        bands[c][bandCount - 1] = input[c] - previous;
    }
    currentAmount += amountSmoothing * (blockAmount - currentAmount);
    const float minimumGain = std::pow(10.0f, -currentAmount / 20.0f);
    auto output = input;
    for (size_t b = 0; b < bandCount; ++b)
    {
        const float power = std::max(bands[0][b] * bands[0][b], bands[1][b] * bands[1][b]);
        envelope[b] += (power > envelope[b] ? envelopeAttack : envelopeRelease) * (power - envelope[b]);
        if (collecting) accumulation[b] += power;
        float target = 1.0f;
        if (blockEnabled && activeProfile && !collecting && noiseFloor[b] > 1.0e-10f)
        {
            const float ratio = std::sqrt(std::max(0.0f, envelope[b])) / noiseFloor[b];
            float open = std::clamp((ratio - 1.5f) / 2.5f, 0.0f, 1.0f);
            open = open * open * (3.0f - 2.0f * open);
            target = minimumGain + (1.0f - minimumGain) * open;
        }
        gains[b] += (target > gains[b] ? gainAttack : gainRelease) * (target - gains[b]);
        for (size_t c = 0; c < 2; ++c) output[c] += (gains[b] - 1.0f) * bands[c][b];
    }
    if (collecting && ++learnFrames >= framesToLearn)
    {
        publication.fetch_add(1, std::memory_order_seq_cst);
        for (size_t b = 0; b < bandCount; ++b)
        {
            noiseFloor[b] = static_cast<float>(std::sqrt(accumulation[b] / static_cast<double>(learnFrames)));
            publishedFloor[b].store(noiseFloor[b], std::memory_order_seq_cst);
        }
        activeProfile = true; collecting = false;
        learned.store(true, std::memory_order_seq_cst);
        publication.fetch_add(1, std::memory_order_seq_cst);
        revision.fetch_add(1, std::memory_order_release);
        progress.store(1.0f); learning.store(false, std::memory_order_release);
    }
    else if (collecting && (learnFrames % 256) == 0)
        progress.store(static_cast<float>(learnFrames) / static_cast<float>(framesToLearn), std::memory_order_relaxed);
    // OFF and exactly zero are direct bypasses, including while learning.
    if (blockEnabled && blockAmount > 0.0f && activeProfile && !collecting)
    {
        left = std::isfinite(output[0]) ? output[0] : input[0];
        right = std::isfinite(output[1]) ? output[1] : input[1];
    }
}
juce::var NR::saveState() const
{
    auto* state = new juce::DynamicObject();
    state->setProperty("version", 1); state->setProperty("enabled", enabled.load());
    state->setProperty("reductionDb", reductionDecibels.load());
    const auto pending = pendingRevision.load(std::memory_order_seq_cst);
    const bool usePending = (pending & 1u) == 0 && pending != acknowledged.load(std::memory_order_acquire);
    if (usePending)
    {
        for (size_t b = 0; b < bandCount; ++b) savedFloor[b] = pendingFloor[b].load();
        savedLearned = pendingLearned.load();
    }
    else
    {
        const auto before = publication.load(std::memory_order_seq_cst);
        if ((before & 1u) == 0)
        {
            std::array<float, bandCount> copy;
            for (size_t b = 0; b < bandCount; ++b) copy[b] = publishedFloor[b].load(std::memory_order_seq_cst);
            const bool valid = learned.load(std::memory_order_seq_cst);
            if (publication.load(std::memory_order_seq_cst) == before) { savedFloor = copy; savedLearned = valid; }
        }
    }
    state->setProperty("learned", savedLearned);
    juce::Array<juce::var> floors;
    for (const auto value : savedFloor) floors.add(value);
    state->setProperty("noiseRms", floors);
    return juce::var(state);
}
juce::Result NR::restoreState(const juce::var& state)
{
    if (state.isVoid()) return juce::Result::ok();
    if (!state.isObject() || static_cast<int>(state["version"]) != 1)
        return juce::Result::fail(juce::String::fromUTF8("NR の保存形式を読み込めません。"));
    const double amount = static_cast<double>(state["reductionDb"]);
    const auto* floors = state["noiseRms"].getArray();
    if (!std::isfinite(amount) || amount < 0.0 || amount > 18.0 || floors == nullptr || floors->size() != static_cast<int>(bandCount))
        return juce::Result::fail(juce::String::fromUTF8("NR の除去量または学習結果が不正です。"));
    std::array<float, bandCount> copy;
    for (size_t b = 0; b < bandCount; ++b)
    {
        const double value = static_cast<double>(floors->getReference(static_cast<int>(b)));
        if (!std::isfinite(value) || value < 0.0 || value > 1.0e6)
            return juce::Result::fail(juce::String::fromUTF8("NR の学習結果が不正です。"));
        copy[b] = static_cast<float>(value);
    }
    pendingRevision.fetch_add(1, std::memory_order_seq_cst);
    for (size_t b = 0; b < bandCount; ++b) pendingFloor[b].store(copy[b], std::memory_order_seq_cst);
    pendingLearned.store(static_cast<bool>(state["learned"]), std::memory_order_seq_cst);
    enabled.store(static_cast<bool>(state["enabled"])); reductionDecibels.store(static_cast<float>(amount));
    pendingRevision.fetch_add(1, std::memory_order_seq_cst);
    return juce::Result::ok();
}
}
