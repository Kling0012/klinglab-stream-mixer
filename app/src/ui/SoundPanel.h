#pragma once

#include "../engine/AudioEngine.h"

namespace ksm
{
// One column of the sound area: a heading with the count and STOP, plus either
// BGM rows or a 3-column SE tile grid. Accepts file drops (import) and internal
// drags (reordering).
class SoundSection final : public juce::Component, public juce::DragAndDropContainer,
                           public juce::DragAndDropTarget, public juce::FileDragAndDropTarget
{
public:
    SoundSection(SoundLibrary&, SoundLibrary::Kind);
    ~SoundSection() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void import(const juce::StringArray& files, std::function<void()> onFinished = nullptr);
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void fileDragEnter(const juce::StringArray&, int, int) override;
    void fileDragExit(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int, int) override;
    bool isInterestedInDragSource(const SourceDetails&) override;
    void itemDropped(const SourceDetails&) override;
    void rebuild();
    void tick();
    std::function<void()> onEdited;

private:
    class Entry;
    SoundLibrary& library;
    SoundLibrary::Kind kind;
    juce::TextButton stop { "STOP" };
    juce::Viewport viewport;
    juce::Component content;
    std::vector<std::unique_ptr<juce::Component>> entries;
    bool fileHover = false;
    int dropIndexAt(juce::Point<int> position) const;
};

class SoundPanel final : public juce::Component, private juce::Timer
{
public:
    explicit SoundPanel(SoundLibrary&);
    ~SoundPanel() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void importForRun(const juce::StringArray& paths, SoundLibrary::Kind, std::function<void()> onFinished = nullptr);
    void playForRun(SoundLibrary::Kind, int number);                      // --play-sound: 1-based list number
    std::function<void()> onEdited;

private:
    SoundLibrary& library;
    SoundSection bgm, se;
    void timerCallback() override;
};
}
