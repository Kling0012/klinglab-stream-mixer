#pragma once

#include "../capture/ProcessTapCapture.h"

namespace ksm
{
class ProjectStore
{
public:
    explicit ProjectStore(juce::File settingsDirectory);
    juce::StringArray names() const;
    juce::Result save(const juce::String& name, const juce::var&) const;
    juce::Result load(const juce::String& name, juce::var&) const;
    juce::Result archive(const juce::String& name) const;
    bool exists(const juce::String& name) const { return fileFor(name).existsAsFile(); }
    juce::Result saveCapture(const juce::String& name, const CaptureSelection&) const;
    juce::Result loadCapture(const juce::String& name, CaptureSelection&) const;
    juce::Result savePlugins(const juce::String& name, const juce::var&) const;
    juce::Result loadPlugins(const juce::String& name, juce::var&) const;
    juce::Result saveNR(const juce::String& name, const juce::var&) const;
    juce::Result loadNR(const juce::String& name, juce::var&) const;
    juce::Result saveLoudness(const juce::String& name, const juce::var&) const;
    juce::Result loadLoudness(const juce::String& name, juce::var&) const;
private:
    juce::File directory;
    juce::File fileFor(const juce::String&) const;
};
}
