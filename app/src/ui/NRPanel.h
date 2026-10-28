#pragma once

#include "../dsp/NR.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace ksm
{
class NRPanel final : public juce::Component
{
public:
    explicit NRPanel(NR&);
    ~NRPanel() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh();
    void setInputAvailable(bool available) { inputAvailable = available; }
    void showProject();
    std::function<void()> onEdited;
private:
    NR& nr;
    juce::Slider amount;
    juce::TextButton toggle, learn;
    juce::Label status;
    juce::Component::SafePointer<juce::DialogWindow> projectWindow;
    bool inputAvailable = false;
};
}
