#include "LoudPanel.h"
#include "Guidance.h"
#include "../project/ProjectStore.h"
#include "../project/Settings.h"

namespace ksm
{
namespace
{
class LoudProjectPanel final : public juce::Component
{
public:
    LoudProjectPanel(Loudness& processor, std::function<void()> callback)
        : loudness(processor), store(Settings().directory()), edited(std::move(callback))
    {
        project.setTextWhenNothingSelected(juce::String::fromUTF8("保存済みプロジェクト"));
        name.setTextToShowWhenEmpty(juce::String::fromUTF8("プロジェクト名"), juce::Colour(0xff686c63));
        save.setButtonText(juce::String::fromUTF8("LOUD を保存")); load.setButtonText(juce::String::fromUTF8("LOUD を読み込む"));
        project.onChange = [this] { name.setText(project.getText()); };
        save.onClick = [this]
        {
            if (loudness.isLearning()) { status.setText(juce::String::fromUTF8("学習が終わってから保存してください。"), juce::dontSendNotification); return; }
            const auto result = store.saveLoudness(name.getText().trim(), loudness.saveState());
            status.setText(result.wasOk() ? juce::String::fromUTF8("LOUD の状態と覚えた量を保存しました。") : result.getErrorMessage(), juce::dontSendNotification);
            refresh();
        };
        load.onClick = [this]
        {
            if (loudness.isLearning()) { status.setText(juce::String::fromUTF8("学習を止めてから読み込んでください。"), juce::dontSendNotification); return; }
            juce::var state; auto result = store.loadLoudness(project.getText(), state);
            if (result.wasOk()) result = loudness.restoreState(state);
            status.setText(result.wasOk() ? juce::String::fromUTF8("LOUD の状態と覚えた量を読み込みました。") : result.getErrorMessage(), juce::dontSendNotification);
            if (result.wasOk() && edited) edited();
        };
        for (auto* c : std::array<juce::Component*, 5>{&project, &name, &save, &load, &status}) addAndMakeVisible(c);
        refresh(); setSize(620, 230);
    }
    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xfff7f5ef)); g.setColour(juce::Colour(0xff30342f)); g.setFont(juce::FontOptions(13.0f));
        g.drawFittedText(juce::String::fromUTF8("LOUD の ON/OFF、STREAM/CD、覚えた量をプロジェクトに保存します。"), 16, 12, getWidth() - 32, 50, juce::Justification::centredLeft, 2);
    }
    void resized() override
    {
        project.setBounds(16, 74, 420, 30); load.setBounds(448, 74, 156, 30);
        name.setBounds(16, 116, 420, 30); save.setBounds(448, 116, 156, 30); status.setBounds(16, 160, 588, 54);
    }
private:
    Loudness& loudness;
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
LoudPanel::LoudPanel(Loudness& processor) : loudness(processor)
{
    for (auto* button : {&toggle, &stream, &cd})
    {
        button->setClickingTogglesState(true);
        button->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff30342f));
        button->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    }
    toggle.onClick = [this] { loudness.enabled.store(toggle.getToggleState()); if (onEdited) onEdited(); refresh(); };
    stream.onClick = [this] { loudness.mode.store(Loudness::Mode::stream); if (onEdited) onEdited(); refresh(); };
    cd.onClick = [this] { loudness.mode.store(Loudness::Mode::cd); if (onEdited) onEdited(); refresh(); };
    learn.onClick = [this] { if (loudness.isLearning()) loudness.requestStop(); else loudness.requestLearn(); };
    learn.setTooltip(juce::String::fromUTF8("曲の大きな部分を入力し、最大20秒測ります。もう一度押すと終了します。BGM・SE は学習しません。"));
    level.setJustificationType(juce::Justification::centred); level.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    amount.setJustificationType(juce::Justification::centred); amount.setFont(juce::FontOptions(11.0f));
    for (auto* c : std::array<juce::Component*, 6>{&level, &amount, &toggle, &learn, &stream, &cd}) addAndMakeVisible(c);
    int number = 300;
    for (auto* c : std::array<juce::Component*, 6>{&level, &amount, &toggle, &learn, &stream, &cd}) Numbers::id(*c, number++);
    refresh();
}
LoudPanel::~LoudPanel() { if (projectWindow != nullptr) delete projectWindow.getComponent(); }
void LoudPanel::refresh()
{
    const bool compact = loudness.mode.load() == Loudness::Mode::cd;
    const bool warning = loudness.enabled.load() && loudness.reductionDecibels() > (compact ? 7.5f : 3.0f);
    const auto lufs = loudness.shortTerm();
    level.setText(loudness.tooQuiet() ? "TOO QUIET" : (lufs <= -99.0f ? "-inf" : juce::String(lufs, 1)) + " LUFS", juce::dontSendNotification);
    level.setColour(juce::Label::textColourId, warning ? juce::Colour(0xffa25009) : juce::Colour(0xff30342f));
    level.setTooltip(warning ? juce::String::fromUTF8("天井で大きく削っています。MASTER を 0.0 dB に戻すか、入力を下げて LEARN し直してください。")
        : juce::String::fromUTF8("配信出力の直近3秒のラウドネス。返しには LOUD の処理も先読み遅延も入りません。"));
    const auto gain = loudness.learnedDecibels.load();
    amount.setText((gain >= 0.0f ? "+" : "") + juce::String(gain, 1) + " dB", juce::dontSendNotification);
    amount.setTooltip(juce::String::fromUTF8("覚えた量。CD はさらに +4.5 dB。天井の削り量：") + juce::String(loudness.reductionDecibels(), 1) + " dB");
    toggle.setButtonText(loudness.enabled.load() ? "LOUD ON" : "LOUD OFF");
    toggle.setToggleState(loudness.enabled.load(), juce::dontSendNotification);
    stream.setToggleState(!compact, juce::dontSendNotification); cd.setToggleState(compact, juce::dontSendNotification);
    learn.setButtonText(loudness.isLearning() ? "STOP " + juce::String(loudness.learnSeconds(), 1) : "LEARN");
}
void LoudPanel::resized()
{
    auto row = getLocalBounds(); auto display = row.removeFromLeft(90);
    level.setBounds(display.removeFromTop(19)); amount.setBounds(display);
    row.removeFromLeft(4); toggle.setBounds(row.removeFromLeft(80)); row.removeFromLeft(4);
    learn.setBounds(row.removeFromLeft(80)); row.removeFromLeft(4);
    stream.setBounds(row.removeFromLeft(65)); row.removeFromLeft(3); cd.setBounds(row);
}
void LoudPanel::showProject()
{
    if (projectWindow != nullptr) { projectWindow->toFront(true); return; }
    juce::DialogWindow::LaunchOptions options; options.dialogTitle = juce::String::fromUTF8("プロジェクトの LOUD 設定");
    options.dialogBackgroundColour = juce::Colour(0xfff7f5ef);
    const juce::Component::SafePointer<LoudPanel> safe(this);
    options.content.setOwned(new LoudProjectPanel(loudness, [safe] { if (safe != nullptr) { safe->refresh(); if (safe->onEdited) safe->onEdited(); } }));
    options.componentToCentreAround = this; options.useNativeTitleBar = true; options.resizable = false;
    projectWindow = options.launchAsync(); if (projectWindow != nullptr) projectWindow->setLookAndFeel(&getLookAndFeel());
}
}
