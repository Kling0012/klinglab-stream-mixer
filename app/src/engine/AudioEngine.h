#pragma once

#include "Router.h"
#include "../io/SharedMemorySender.h"
#include "../io/SoundLibrary.h"
#include "../capture/ProcessTapCapture.h"
#include "../project/Settings.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

namespace ksm
{
struct DeviceInfo
{
    uint32_t id = 0;
    juce::String uid, name;
    unsigned inputs = 0, outputs = 0;
    double sampleRate = 48000.0;
};
struct DeviceSelection
{
    juce::String instMode = "APP", cableUID, micUID, monitorUID, streamUID, micMonUID;
    int micChannels = 1, bufferFrames = 128;
    double requestedRate = 0.0;
};

class AudioEngine : private juce::Timer
{
public:
    AudioEngine();
    ~AudioEngine() override;
    void start();
    void startDiagnostic();
    void stop();
    bool isRunning() const noexcept;
    const std::vector<DeviceInfo>& devices() const noexcept { return deviceList; }
    const DeviceSelection& selection() const noexcept { return selected; }
    juce::Result selectDevices(const DeviceSelection&, const CaptureSelection* capture = nullptr,
                               std::function<juce::Result()> commit = {});
    bool routeAvailable(int row) const;
    juce::String latencyText() const
    {
        return juce::String::fromUTF8("CoreAudio · ") + juce::String(sampleRate() / 1000.0, 1) + juce::String::fromUTF8("k · ")
               + juce::String(bufferFrames()) + juce::String::fromUTF8("smp · RT ") + (routeAvailable(1) && routeAvailable(2) ? juce::String(roundTripMs(), 1) + "ms" : "--");
    }
    juce::String statusText() const;
    double roundTripMs() const noexcept;
    float averageLoad() const noexcept;
    float maximumLoad() const noexcept;
    double sampleRate() const noexcept;
    int bufferFrames() const noexcept;
    bool microphoneAllowed() const;
    void requestMicrophone();
    std::function<void()> onChange;
    Router router;
    SharedMemorySender sender;
    ProcessTapCapture appCapture;
    SoundLibrary sounds { Settings().directory() };
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    DeviceSelection selected;
    std::vector<DeviceInfo> deviceList;
    bool started = false;
    void timerCallback() override;
    void refresh();
    void rebuild();
    void defaults();
};
}
