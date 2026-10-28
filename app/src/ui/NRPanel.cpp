#include "NRPanel.h"
#include "Guidance.h"
#include "../project/ProjectStore.h"
#include "../project/Settings.h"

namespace ksm
{
namespace
{
class NRProjectPanel final : public juce::Component
{
public:
    NRProjectPanel(NR& processor, std::function<void()> callback) : nr(processor), store(Settings().directory()), edited(std::move(callback))
    {
        project.setTextWhenNothingSelected(juce::String::fromUTF8("保存済みプロジェクト"));
        name.setTextToShowWhenEmpty(juce::String::fromUTF8("プロジェクト名"), juce::Colour(0xff686c63));
        save.setButtonText(juce::String::fromUTF8("NR を保存")); load.setButtonText(juce::String::fromUTF8("NR を読み込む"));
        project.onChange = [this] { name.setText(project.getText()); };
        save.onClick = [this]
        {
            if (nr.isLearning()) { status.setText(juce::String::fromUTF8("学習が終わってから保存してください。"), juce::dontSendNotification); return; }
            const auto result = store.saveNR(name.getText().trim(), nr.saveState());
            status.setText(result.wasOk() ? juce::String::fromUTF8("NR の状態と学習結果を保存しました。") : result.getErrorMessage(), juce::dontSendNotification);
            refresh();
        };
        load.onClick = [this]
        {
            juce::var state; auto result = store.loadNR(project.getText(), state);
            if (result.wasOk()) result = nr.restoreState(state);
            status.setText(result.wasOk() ? juce::String::fromUTF8("NR の状態と学習結果を読み込みました。") : result.getErrorMessage(), juce::dontSendNotification);
            if (result.wasOk() && edited) edited();
        };
        for (auto* c : std::array<juce::Component*, 5>{&project, &name, &save, &load, &status}) addAndMakeVisible(c);
        refresh(); setSize(620, 230);
    }
    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xfff7f5ef)); g.setColour(juce::Colour(0xff30342f)); g.setFont(juce::FontOptions(13.0f));
        g.drawFittedText(juce::String::fromUTF8("NR の ON/OFF、除去量、学習したノイズをプロジェクトに保存します。"), 16, 12, getWidth() - 32, 50, juce::Justification::centredLeft, 2);
    }
    void resized() override
    {
        project.setBounds(16, 74, 420, 30); load.setBounds(448, 74, 156, 30);
        name.setBounds(16, 116, 420, 30); save.setBounds(448, 116, 156, 30); status.setBounds(16, 160, 588, 54);
    }
private:
    NR& nr;
    ProjectStore store;
    std::function<void()> edited;
    juce::ComboBox project;
    juce::TextEditor name;
    juce::TextButton save, load;
    juce::Label status;
    void refresh()
    {
        const auto selected = project.getText(); project.clear(juce::dontSendNotification); int id = 1;
        for (const auto& entry : store.names()) project.addItem(entry, id++);
        project.setText(selected, juce::dontSendNotification);
    }
};
}
NRPanel::NRPanel(NR& processor) : nr(processor)
{
    amount.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag); amount.setRange(0.0, 18.0, 0.1);
    amount.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 22); amount.setNumDecimalPlacesToDisplay(1); amount.setTextValueSuffix(" dB");
    amount.setDoubleClickReturnValue(true, 6.0);
    amount.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xff30342f));
    amount.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xfff7f5ef));
    amount.setTooltip(juce::String::fromUTF8("MIC NR：0〜18 dB。0 dB は原音をそのまま通します。"));
    amount.onValueChange = [this] { nr.reductionDecibels.store(static_cast<float>(amount.getValue())); if (onEdited) onEdited(); };
    toggle.setClickingTogglesState(true); toggle.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff30342f));
    toggle.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    toggle.onClick = [this] { nr.enabled.store(toggle.getToggleState()); if (onEdited) onEdited(); refresh(); };
    learn.onClick = [this] { nr.requestLearn(); refresh(); };
    learn.setTooltip(juce::String::fromUTF8("約1秒、声を出さずに定常ノイズだけを入力してください。学習中は原音を通します。"));
    status.setFont(juce::FontOptions(11.0f)); status.setJustificationType(juce::Justification::centred);
    status.setMinimumHorizontalScale(0.85f);
    for (auto* c : std::array<juce::Component*, 4>{&amount, &toggle, &learn, &status}) addAndMakeVisible(c);
    Numbers::id(amount, 1601); Numbers::id(toggle, 1602); Numbers::id(learn, 1603); Numbers::id(status, 1604);
    refresh();
}
NRPanel::~NRPanel() { if (projectWindow != nullptr) delete projectWindow.getComponent(); }
void NRPanel::refresh()
{
    const bool learning = nr.isLearning();
    amount.setValue(nr.reductionDecibels.load(), juce::dontSendNotification);
    toggle.setToggleState(nr.enabled.load(), juce::dontSendNotification);
    toggle.setButtonText(nr.enabled.load() ? "NR ON" : "NR OFF");
    learn.setButtonText(learning ? "LEARNING " + juce::String(static_cast<int>(nr.learnProgress() * 100.0f)) + "%" : nr.hasProfile() ? "RELEARN" : "LEARN");
    learn.setEnabled(!learning && inputAvailable);
    const auto message = learning ? juce::String::fromUTF8("声を出さないでください") : !inputAvailable ? juce::String::fromUTF8("MIC INPUT を確認")
        : !nr.hasProfile() ? juce::String::fromUTF8("LEARN でノイズを学習") : juce::String::fromUTF8("学習済み · 先読みなし");
    status.setText(message, juce::dontSendNotification); status.setTooltip(message);
    repaint();
}
void NRPanel::paint(juce::Graphics& g)
{
    g.setColour(juce::Colour(0xffcfcac0)); g.drawHorizontalLine(0, 0.0f, static_cast<float>(getWidth()));
    g.setColour(juce::Colour(0xff30342f)); g.setFont(juce::FontOptions(12.0f));
    g.drawText("MIC NR", 2, 4, getWidth() - 4, 22, juce::Justification::centredLeft);
}
void NRPanel::resized()
{
    Numbers::area(*this, 1600, {2, 4, getWidth() - 4, 22});
    amount.setBounds(0, 24, getWidth(), 28);
    if (getWidth() < 100)
    {
        amount.setTextBoxStyle(juce::Slider::TextBoxBelow, false, getWidth(), 22);
        amount.setBounds(0, 26, getWidth(), 66);
        toggle.setBounds(0, 97, getWidth(), 25); learn.setBounds(0, 126, getWidth(), 25); status.setBounds(0, 156, getWidth(), 44);
    }
    else
    {
        amount.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 22);
        toggle.setBounds(0, 55, getWidth() / 2 - 2, 24); learn.setBounds(getWidth() / 2 + 2, 55, getWidth() / 2 - 2, 24);
        status.setBounds(0, 82, getWidth(), 25);
    }
}
void NRPanel::showProject()
{
    if (projectWindow != nullptr) { projectWindow->toFront(true); return; }
    juce::DialogWindow::LaunchOptions options; options.dialogTitle = juce::String::fromUTF8("プロジェクトの NR 設定");
    options.dialogBackgroundColour = juce::Colour(0xfff7f5ef);
    const juce::Component::SafePointer<NRPanel> safe(this);
    options.content.setOwned(new NRProjectPanel(nr, [safe] { if (safe != nullptr) { safe->refresh(); if (safe->onEdited) safe->onEdited(); } }));
    options.componentToCentreAround = this; options.useNativeTitleBar = true; options.resizable = false;
    projectWindow = options.launchAsync(); if (projectWindow != nullptr) projectWindow->setLookAndFeel(&getLookAndFeel());
}
}
