#pragma once

#include "../engine/ClockBridge.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace ksm
{
struct CaptureSelection
{
    juce::StringArray bundleIDs;
    bool muteDirect = true;
    juce::var toJson() const;
    static CaptureSelection fromJson(const juce::var&);
};
struct CaptureApp
{
    juce::String bundleID, name;
    juce::Image icon;
    std::vector<uint32_t> processes;
    bool hasAudio = false;
};

class ProcessTapCapture : private juce::Timer
{
public:
    ProcessTapCapture();
    ~ProcessTapCapture() override;
    void start();
    void startDiagnostic();
    void stop();
    juce::Result setClock(double rate, unsigned bufferFrames, bool enabled);
    const std::vector<CaptureApp>& apps() const noexcept;
    const CaptureSelection& selection() const noexcept;
    juce::Result validate(const CaptureSelection&) const;
    juce::Result select(const CaptureSelection&);
    bool isActive() const noexcept;
    juce::String statusText() const;
    bool needsPermissionHelp() const noexcept;
    void retryPermission();
    static void openPrivacySettings();
    std::function<void()> onChange;

    // One lease per main callback keeps the bridge alive during an APP reconnect.
    class ReadScope
    {
    public:
        explicit ReadScope(ProcessTapCapture&) noexcept;
        ~ReadScope();
        ReadScope(const ReadScope&) = delete;
        ReadScope& operator=(const ReadScope&) = delete;
        Stereo pull() noexcept { return bridge != nullptr ? bridge->pull() : Stereo{}; }
    private:
        ProcessTapCapture& owner;
        ClockBridge* bridge;
    };
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    std::atomic<ClockBridge*> readable { nullptr };
    std::atomic<unsigned> readers { 0 };
    void timerCallback() override;
};
}
