#pragma once

#include "../plugins/PluginHost.h"
#include "../plugins/Scanner.h"

namespace ksm
{
class PluginChainPanel final : public juce::Component, public juce::DragAndDropContainer,
                               public juce::DragAndDropTarget, private juce::Timer
{
public:
    PluginChainPanel(PluginHost&, PluginHost::Chain, Scanner&);
    ~PluginChainPanel() override;
    void refresh();
    void showAdd();
    void showScanMenu();
    void showProject();
    juce::Component* addContent() const;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool isInterestedInDragSource(const SourceDetails&) override;
    void itemDropped(const SourceDetails&) override;
    std::function<void()> onEdited;
private:
    class Tile;
    PluginHost& host;
    PluginHost::Chain chain;
    Scanner& scanner;
    juce::Viewport viewport;
    juce::Component content;
    std::vector<std::unique_ptr<Tile>> tiles;
    juce::TextButton add { "+ VST" }, scan { "Scan" };
    juce::Component::SafePointer<juce::DialogWindow> addWindow, scanWindow, projectWindow;
    std::unique_ptr<juce::FileChooser> folderChooser;
    juce::String lastSignature;
    void showProgress();
    void timerCallback() override;
};
}
