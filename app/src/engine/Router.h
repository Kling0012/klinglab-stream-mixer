#pragma once

#include "Fader.h"
#include "Meter.h"
#include "../plugins/PluginHost.h"
#include "../dsp/NR.h"
#include "../dsp/Loudness.h"
#include <array>

namespace ksm
{
struct Stereo
{
    float l = 0.0f, r = 0.0f;
    Stereo operator+(Stereo b) const noexcept { return { l + b.l, r + b.r }; }
    Stereo operator*(float g) const noexcept { return { l * g, r * g }; }
};

class Router
{
public:
    Fader instMonitor { 0.0f, 0.0f }, streamInst, streamVocal;
    Fader vocalMonitor { 0.0f, 0.0f };
    Fader bgmStream { -6.0f }, bgmMonitor { -6.0f };
    Fader seStream { -3.1f }, seMonitor { -3.1f }, master;
    std::atomic<bool> vocalMonEnabled { true }, bgmMonEnabled { true }, seMonEnabled { true };
    std::atomic<float> monWet { 1.0f }, micMonGain { 1.0f };
    std::atomic<int> manualDelayMs { 0 };
    Meter inputMeter, instMonMeter, streamMeter, vocalMeter, bgmMeter, seMeter;
    PluginHost plugins;
    NR nr;
    Loudness loudness;
    static constexpr int maximumBlock = 1024;
    struct Outputs { Stereo monitor, stream, mic; };
    void prepare(double sampleRate, bool preserve = false, bool preserveRestore = false)
    {
        if (!preserve || std::abs(preparedRate - sampleRate) > 0.5)
            plugins.prepare(sampleRate, maximumBlock, preserveRestore);
        preparedRate = sampleRate;
        nr.prepare(sampleRate, preserve);
        loudness.prepare(sampleRate, preserve);
        for (auto* b : { &instBuffer, &vocalBuffer, &monitorBuffer, &streamBuffer }) b->setSize(2, maximumBlock);
        if (!preserve)
        {
            currentMicGain = micMonGain.load();
            switchGain = vocalMonEnabled.load() ? 1.0f : 0.0f;
            bgmSwitchGain = bgmMonEnabled.load() ? 1.0f : 0.0f;
            seSwitchGain = seMonEnabled.load() ? 1.0f : 0.0f;
        }
        for (auto* f : faders()) f->prepare(sampleRate, preserve);
        smoothing = 1.0f - std::exp(-1.0f / static_cast<float>(sampleRate * 0.020));
    }
    std::array<Fader*, 9> faders() noexcept
    {
        return { &instMonitor, &streamInst, &streamVocal, &vocalMonitor, &bgmStream,
                 &bgmMonitor, &seStream, &seMonitor, &master };
    }
    // Device callbacks split larger buffers before entering here. All storage is prepared above.
    void processBlock(const Stereo* inst, const Stereo* mic, Outputs* output, int frames,
                      const Stereo* bgm = nullptr, const Stereo* se = nullptr) noexcept
    {
        PluginHost::ReadScope scope(plugins);
        nr.beginBlock();
        loudness.beginBlock();
        for (int f = 0; f < frames; ++f)
        {
            inputMeter.push(inst[f].l, inst[f].r);
            instBuffer.setSample(0, f, inst[f].l); instBuffer.setSample(1, f, inst[f].r);
            auto vocal = mic[f];
            nr.process(vocal.l, vocal.r);
            vocalBuffer.setSample(0, f, vocal.l); vocalBuffer.setSample(1, f, vocal.r);
        }
        plugins.processVocal(scope.get(), vocalBuffer, monitorBuffer, frames, monWet.load(std::memory_order_relaxed));
        plugins.processInst(scope.get(), instBuffer, frames, manualDelayMs.load(std::memory_order_relaxed));
        for (int f = 0; f < frames; ++f)
        {
            const auto vocal = Stereo {vocalBuffer.getSample(0, f), vocalBuffer.getSample(1, f)};
            const auto monitorVocal = Stereo {monitorBuffer.getSample(0, f), monitorBuffer.getSample(1, f)};
            const auto instMon = inst[f] * instMonitor.next();
            switchGain += smoothing * ((vocalMonEnabled.load(std::memory_order_relaxed) ? 1.0f : 0.0f) - switchGain);
            const auto vocalMon = monitorVocal * (vocalMonitor.next() * switchGain);
            instMonMeter.push(instMon.l, instMon.r); vocalMeter.push(vocal.l, vocal.r);
            const auto bus = Stereo {instBuffer.getSample(0, f), instBuffer.getSample(1, f)} * streamInst.next() + vocal * streamVocal.next();
            streamBuffer.setSample(0, f, bus.l); streamBuffer.setSample(1, f, bus.r);
            currentMicGain += smoothing * (micMonGain.load(std::memory_order_relaxed) - currentMicGain);
            output[f] = {instMon + vocalMon, {}, vocal * currentMicGain};
        }
        plugins.processStream(scope.get(), streamBuffer, frames);
        for (int f = 0; f < frames; ++f)
        {
            const auto bgmInput = bgm != nullptr ? bgm[f] : Stereo{};
            const auto seInput = se != nullptr ? se[f] : Stereo{};
            const auto bgmOut = bgmInput * bgmStream.next(), seOut = seInput * seStream.next();
            bgmMeter.push(bgmOut.l, bgmOut.r); seMeter.push(seOut.l, seOut.r);
            auto bus = Stereo {streamBuffer.getSample(0, f), streamBuffer.getSample(1, f)};
            const auto masterGain = master.next();
            loudness.processBus(bus.l, bus.r, masterGain);
            output[f].stream = (bus + bgmOut + seOut) * masterGain;
            loudness.processOutput(output[f].stream.l, output[f].stream.r);
            streamMeter.push(output[f].stream.l, output[f].stream.r);
            bgmSwitchGain += smoothing * ((bgmMonEnabled.load(std::memory_order_relaxed) ? 1.0f : 0.0f) - bgmSwitchGain);
            seSwitchGain += smoothing * ((seMonEnabled.load(std::memory_order_relaxed) ? 1.0f : 0.0f) - seSwitchGain);
            output[f].monitor = output[f].monitor + bgmInput * (bgmMonitor.next() * bgmSwitchGain) + seInput * (seMonitor.next() * seSwitchGain);
        }
    }
private:
    double preparedRate = 0.0;
    float currentMicGain = 1.0f, switchGain = 1.0f, bgmSwitchGain = 1.0f, seSwitchGain = 1.0f, smoothing = 0.001f;
    juce::AudioBuffer<float> instBuffer, vocalBuffer, monitorBuffer, streamBuffer;
};
}
