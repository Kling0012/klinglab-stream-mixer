#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace ksm
{
class Scanner final : private juce::Timer
{
public:
    explicit Scanner(juce::File settingsDirectory);
    ~Scanner() override;
    bool isRunning() const noexcept { return running; }
    void start(bool full, juce::File folder = {});
    void cancel();
    juce::String progress() const;
    const juce::StringArray& failures() const noexcept { return failed; }
    juce::Array<juce::PluginDescription> plugins() const
    {
        juce::Array<juce::PluginDescription> result;
        for (const auto& type : known.getTypes())
            if (!type.isInstrument && type.numInputChannels > 0 && type.numOutputChannels > 0) result.add(type);
        return result;
    }
    std::function<void()> onChange;
    std::function<void()> onFinished;
    static int worker(const juce::StringArray& arguments);
private:
    struct Candidate { juce::String format, identifier; };
    juce::File root, resultFile;
    juce::KnownPluginList known;
    juce::AudioPluginFormatManager formats;
    juce::ChildProcess child;
    std::vector<Candidate> pending;
    juce::StringArray failed, retryIdentifiers;
    Candidate current;
    size_t index = 0;
    double began = 0.0;
    bool running = false, enumerating = false, rescanAll = false;
    void timerCallback() override;
    bool launch(const juce::StringArray&);
    void next();
    void finish();
    void save();
};
}
