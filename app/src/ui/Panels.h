#pragma once

#include "../engine/AudioEngine.h"
#include "NRPanel.h"
#include <deque>

namespace ksm
{
class PlaceholderPanel : public juce::Component
{
public:
    PlaceholderPanel(juce::String heading, juce::StringArray rows, juce::Colour accent = juce::Colour(0xff727b72));
    void paint(juce::Graphics&) override;
private:
    juce::String title;
    juce::StringArray lines;
    juce::Colour colour;
};

class DevicePanel final : public juce::Component
{
public:
    explicit DevicePanel(AudioEngine&);
    void refresh();
    void paint(juce::Graphics&) override;
    void resized() override;
    void showAppSelection();
    void closeAppSelection();
    juce::Component* appSelectionContent() const;
    std::function<void()> onEdited;
private:
    AudioEngine& engine;
    juce::TextButton apps { juce::String::fromUTF8("アプリを選択") };
    juce::Component::SafePointer<juce::DialogWindow> appWindow;
    std::array<juce::ComboBox, 5> devices;
    std::array<juce::StringArray, 5> uids;
    juce::ComboBox mode, micChannels;
    juce::Slider micMon;
    bool refreshing = false;
    void apply();
};

class SoundPanel; // implemented in SoundPanel.h

class MeterView final : public juce::Component, public juce::SettableTooltipClient
{
public:
    MeterView(Meter&, juce::Colour);
    void tick(double now);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
private:
    Meter& source;
    juce::Colour colour;
    std::array<float, 2> displayed {}, held {};
    std::array<std::deque<std::pair<double, float>>, 2> history;
};

class FaderControl final : public juce::Component
{
public:
    FaderControl(Fader&, juce::String name, double maximum, bool horizontal = false);
    void paint(juce::Graphics&) override;
    void resized() override;
    std::function<void()> onEdited;
    void refresh() { if (!slider.isMouseButtonDown()) slider.setValue(fader.getDecibels(), juce::dontSendNotification); repaint(); }
private:
    Fader& fader;
    juce::String title;
    juce::Slider slider;
    bool horizontal;
};

class MixerStrip final : public juce::Component
{
public:
    MixerStrip(int index, Router&);
    void paint(juce::Graphics&) override;
    void resized() override;
    void tick(double);
    void setMicAvailable(bool available) { if (nrPanel != nullptr) nrPanel->setInputAvailable(available); }
    void showNRProject() { if (nrPanel != nullptr) nrPanel->showProject(); }
    std::function<void()> onEdited;
private:
    int index;
    Router& router;
    juce::Colour accent;
    juce::String title;
    MeterView meter;
    std::unique_ptr<FaderControl> first, second, master;
    std::unique_ptr<NRPanel> nrPanel;
    juce::TextButton mon { "MON" }, minus { juce::String::fromUTF8("−") }, plus { "+" };
    juce::Label delay;
    juce::Slider wet;
};
}
