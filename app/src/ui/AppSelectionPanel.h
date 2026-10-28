#pragma once

#include "../capture/ProcessTapCapture.h"
#include "../project/ProjectStore.h"

namespace ksm
{
class AppSelectionPanel final : public juce::Component, private juce::ListBoxModel, private juce::Timer
{
public:
    AppSelectionPanel(ProcessTapCapture&, juce::File settingsDirectory, std::function<void()> onApplied);
    ~AppSelectionPanel() override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    ProcessTapCapture& capture;
    ProjectStore projects;
    CaptureSelection draft;
    std::function<void()> applied;
    std::vector<CaptureApp> rows;
    juce::ListBox list;
    juce::ToggleButton mute { juce::String::fromUTF8("取り込んだアプリの音をスピーカーから消す") };
    juce::TextButton apply { juce::String::fromUTF8("適用") }, clear { juce::String::fromUTF8("全解除") }, cancel { juce::String::fromUTF8("キャンセル") };
    juce::TextButton privacy { juce::String::fromUTF8("録音のシステム設定を開く") }, retry { juce::String::fromUTF8("取り込みを再試行") };
    juce::ComboBox project;
    juce::TextEditor projectName;
    juce::TextButton save { juce::String::fromUTF8("APP 選択を保存") }, load { juce::String::fromUTF8("読み込む") };
    juce::Label summary, error;
    juce::String feedback;
    int getNumRows() override;
    void paintListBoxItem(int, juce::Graphics&, int, int, bool) override;
    juce::Component* refreshComponentForRow(int, bool, juce::Component*) override;
    void timerCallback() override;
    void updateSummary();
    void close();
};
}
