#pragma once

#include <juce_core/juce_core.h>

namespace ksm
{
class Settings
{
public:
    Settings();
    juce::Result createDirectories() const;
    const juce::File& directory() const noexcept { return root; }
    juce::File logFile() const { return root.getChildFile("logs/startup.log"); }
    juce::Result rotateLogs() const;
    bool previousRunInterrupted() const;
    juce::Result writeRunState(bool running) const;
    juce::var load() const;
    juce::Result save(const juce::var&) const;

private:
    juce::File root;
};
}
