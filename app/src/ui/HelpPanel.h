#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <memory>

namespace ksm
{
class HelpPanel final : public juce::Component
{
public:
    HelpPanel();
    ~HelpPanel() override;
    void openChapter(int chapter, const juce::String& heading = {});
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    class Document;
    std::unique_ptr<Document> document;
    juce::Viewport viewport;
    std::array<juce::TextButton, 12> chapters;
    juce::Label title, status;
    juce::TextButton install;
    int currentChapter = 1;
};
}
