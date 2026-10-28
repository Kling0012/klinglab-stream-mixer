#pragma once
#include "../dsp/Loudness.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace ksm
{
class LoudPanel final : public juce::Component
{
public:
    explicit LoudPanel(Loudness&);
    ~LoudPanel() override;
    void refresh();
    void resized() override;
    void showProject();
    std::function<void()> onEdited;
private:
    Loudness& loudness;
    juce::Label level, amount;
    juce::TextButton toggle, learn { "LEARN" }, stream { "STREAM" }, cd { "CD" };
    juce::Component::SafePointer<juce::DialogWindow> projectWindow;
};
}
