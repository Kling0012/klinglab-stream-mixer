#include "PluginPanel.h"
#include "Guidance.h"
#include "../project/ProjectStore.h"
#include "../project/Settings.h"

namespace ksm
{
namespace
{
const juce::Colour ink(0xff30342f), surface(0xfff7f5ef), orange(0xffc56a15), green(0xff398529);
class AddPanel final : public juce::Component, private juce::ListBoxModel, private juce::Timer
{
public:
    AddPanel(PluginHost& h, PluginHost::Chain c, Scanner& s) : host(h), chain(c), scanner(s), list("plugins", this)
    {
        search.setTextToShowWhenEmpty(juce::String::fromUTF8("名前・メーカー・形式で検索"), juce::Colour(0xff686c63));
        search.onTextChange = [this] { filter(); };
        add.setButtonText(juce::String::fromUTF8("追加"));
        add.onClick = [this] { addSelected(); };
        list.setRowHeight(42); list.setColour(juce::ListBox::backgroundColourId, surface);
        list.setColour(juce::ListBox::outlineColourId, juce::Colour(0xffcfcac0)); list.setOutlineThickness(1);
        status.setColour(juce::Label::textColourId, ink);
        for (auto* component : std::array<juce::Component*, 4>{ &search, &list, &add, &status }) addAndMakeVisible(component);
        setSize(650, 460); filter(); startTimer(500);
    }
    void paint(juce::Graphics& g) override { g.fillAll(surface); }
    void resized() override
    {
        auto r = getLocalBounds().reduced(16); search.setBounds(r.removeFromTop(30)); r.removeFromTop(10);
        auto bottom = r.removeFromBottom(40); add.setBounds(bottom.removeFromRight(90).reduced(0, 4)); status.setBounds(bottom);
        list.setBounds(r);
    }
private:
    PluginHost& host;
    PluginHost::Chain chain;
    Scanner& scanner;
    juce::TextEditor search;
    juce::ListBox list;
    juce::TextButton add;
    juce::Label status;
    juce::Array<juce::PluginDescription> filtered;
    int lastCount = -1;
    int getNumRows() override { return filtered.size(); }
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (!juce::isPositiveAndBelow(row, filtered.size())) return;
        if (selected) g.fillAll(juce::Colour(0xffdcebd9));
        const auto& d = filtered.getReference(row);
        g.setColour(ink); g.setFont(juce::FontOptions(14.0f)); g.drawText(d.name, 10, 2, width - 20, 21, juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff686c63)); g.setFont(juce::FontOptions(11.0f));
        g.drawText(d.manufacturerName + "  /  " + d.pluginFormatName, 10, 23, width - 20, height - 23, juce::Justification::centredLeft);
    }
    void selectedRowsChanged(int) override { add.setEnabled(list.getSelectedRow() >= 0); }
    void listBoxItemDoubleClicked(int, const juce::MouseEvent&) override { addSelected(); }
    void filter()
    {
        filtered.clear(); const auto query = search.getText().trim();
        const auto all = scanner.plugins(); lastCount = all.size();
        for (const auto& d : all)
            if ((d.name + " " + d.manufacturerName + " " + d.pluginFormatName).containsIgnoreCase(query)) filtered.add(d);
        list.updateContent(); list.deselectAllRows(); add.setEnabled(false);
        status.setText(all.isEmpty() ? juce::String::fromUTF8("Scan でプラグインを検索してください。") : juce::String(filtered.size()) + juce::String::fromUTF8("件"), juce::dontSendNotification);
    }
    void addSelected()
    {
        const int row = list.getSelectedRow(); if (!juce::isPositiveAndBelow(row, filtered.size())) return;
        add.setEnabled(false); status.setText(juce::String::fromUTF8("読み込み中…"), juce::dontSendNotification);
        const juce::Component::SafePointer<AddPanel> safe(this);
        host.add(chain, filtered.getReference(row), [safe](juce::Result result)
        {
            if (safe == nullptr) return;
            safe->status.setText(result.wasOk() ? juce::String::fromUTF8("追加しました。") : result.getErrorMessage(), juce::dontSendNotification);
            safe->add.setEnabled(true);
        });
    }
    void timerCallback() override { if (scanner.plugins().size() != lastCount) filter(); }
};
class FxProjectPanel final : public juce::Component
{
public:
    explicit FxProjectPanel(PluginHost& p) : host(p), store(Settings().directory())
    {
        project.setTextWhenNothingSelected(juce::String::fromUTF8("保存済みプロジェクト"));
        name.setTextToShowWhenEmpty(juce::String::fromUTF8("プロジェクト名"), juce::Colour(0xff686c63));
        save.setButtonText(juce::String::fromUTF8("FXを保存")); load.setButtonText(juce::String::fromUTF8("FXを読み込む"));
        refresh();
        save.onClick = [this]
        {
            if (host.isRestoring()) { status.setText(juce::String::fromUTF8("読み込みが終わってから保存してください。"), juce::dontSendNotification); return; }
            auto result = store.savePlugins(name.getText().trim(), host.saveState());
            status.setText(result.wasOk() ? juce::String::fromUTF8("両方の列とプラグイン設定を保存しました。") : result.getErrorMessage(), juce::dontSendNotification); refresh();
        };
        load.onClick = [this]
        {
            juce::var state; auto result = store.loadPlugins(project.getText(), state);
            if (result.failed()) { status.setText(result.getErrorMessage(), juce::dontSendNotification); return; }
            name.setText(project.getText()); status.setText(juce::String::fromUTF8("読み込み中…"), juce::dontSendNotification);
            const juce::Component::SafePointer<FxProjectPanel> safe(this);
            host.restoreState(state, [safe](juce::Result r)
            {
                if (safe != nullptr) safe->status.setText(r.wasOk() ? juce::String::fromUTF8("両方の列を読み込みました。") : r.getErrorMessage(), juce::dontSendNotification);
            });
        };
        for (auto* c : std::array<juce::Component*, 5>{ &project, &name, &save, &load, &status }) addAndMakeVisible(c);
        setSize(620, 230);
    }
    void paint(juce::Graphics& g) override
    {
        g.fillAll(surface); g.setColour(ink); g.setFont(juce::FontOptions(13.0f));
        g.drawFittedText(juce::String::fromUTF8("FX CHAIN と STREAM FX の並び・バイパス・各設定を保存します。デバイスやフェーダーは変更しません。"), 16, 12, getWidth() - 32, 50, juce::Justification::centredLeft, 2);
    }
    void resized() override
    {
        project.setBounds(16, 74, 420, 30); load.setBounds(448, 74, 156, 30);
        name.setBounds(16, 116, 420, 30); save.setBounds(448, 116, 156, 30); status.setBounds(16, 160, 588, 54);
    }
private:
    PluginHost& host;
    ProjectStore store;
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
class ScanPanel final : public juce::Component, private juce::Timer
{
public:
    explicit ScanPanel(Scanner& s) : scanner(s)
    {
        stop.setButtonText(juce::String::fromUTF8("検索を中止")); stop.onClick = [this] { scanner.cancel(); update(); };
        details.setMultiLine(true); details.setReadOnly(true); details.setScrollbarsShown(true);
        details.setColour(juce::TextEditor::textColourId, ink); details.setColour(juce::TextEditor::backgroundColourId, surface);
        addAndMakeVisible(details); addAndMakeVisible(stop); setSize(650, 330); update(); startTimer(250);
    }
    void paint(juce::Graphics& g) override { g.fillAll(surface); }
    void resized() override
    {
        auto r = getLocalBounds().reduced(16); stop.setBounds(r.removeFromBottom(30).removeFromRight(120)); r.removeFromBottom(10); details.setBounds(r);
    }
private:
    Scanner& scanner;
    juce::TextEditor details;
    juce::TextButton stop;
    void update()
    {
        juce::String text = scanner.progress();
        if (!scanner.isRunning())
            text = juce::String::fromUTF8("検索結果：") + juce::String(scanner.plugins().size()) + juce::String::fromUTF8("件\n\n読めなかった項目（次回も検索します）\n")
                + (scanner.failures().isEmpty() ? juce::String::fromUTF8("なし") : scanner.failures().joinIntoString("\n"));
        details.setText(text, false); stop.setEnabled(scanner.isRunning());
    }
    void timerCallback() override { update(); }
};
}
class PluginChainPanel::Tile final : public juce::Component
{
public:
    Tile(PluginChainPanel& p, const PluginHost::Item& item) : owner(p), id(item.id)
    {
        name.setButtonText(item.name); bypass.setButtonText("B"); remove.setButtonText("X");
        name.setTooltip(item.name); bypass.setTooltip(item.enabled ? juce::String::fromUTF8("有効：押すとバイパス") : juce::String::fromUTF8("バイパス中：押すと有効"));
        bypass.setColour(juce::TextButton::buttonColourId, item.enabled ? orange : juce::Colour(0xffcfcac0));
        bypass.setColour(juce::TextButton::textColourOffId, item.enabled ? juce::Colours::white : ink);
        remove.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffad3434));
        name.onClick = [this] { owner.host.showEditor(owner.chain, id); };
        bypass.onClick = [this] { owner.host.toggle(owner.chain, id); };
        remove.onClick = [this, title = item.name]
        {
            const juce::Component::SafePointer<PluginChainPanel> safe(&owner); const auto pluginID = id;
            juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon,
                juce::String::fromUTF8("プラグインを削除"), title + juce::String::fromUTF8(" を削除しますか。このプラグインの設定も消えます。"),
                juce::String::fromUTF8("削除"), juce::String::fromUTF8("キャンセル"), nullptr,
                juce::ModalCallbackFunction::create([safe, pluginID](int answer) { if (safe != nullptr && answer == 1) safe->host.remove(safe->chain, pluginID); }));
        };
        const auto prefix = juce::String(owner.chain == PluginHost::Chain::vocal ? "vocal:" : "stream:") + id;
        Numbers::dynamicId(*this, prefix + ":drag"); Numbers::dynamicId(name, prefix + ":name");
        Numbers::dynamicId(bypass, prefix + ":bypass"); Numbers::dynamicId(remove, prefix + ":remove");
        for (auto* b : { &name, &bypass, &remove }) addAndMakeVisible(b);
    }
    void paint(juce::Graphics& g) override
    {
        g.setColour(juce::Colour(0xff686c63));
        for (int y : {10, 15, 20}) { g.fillEllipse(4.0f, static_cast<float>(y), 2.0f, 2.0f); g.fillEllipse(9.0f, static_cast<float>(y), 2.0f, 2.0f); }
    }
    void resized() override
    {
        auto r = getLocalBounds().withTrimmedLeft(16).reduced(0, 2);
        remove.setBounds(r.removeFromRight(24)); r.removeFromRight(2); bypass.setBounds(r.removeFromRight(26)); r.removeFromRight(2); name.setBounds(r);
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (e.getMouseDownX() < 16 && e.getDistanceFromDragStart() > 4)
            owner.startDragging(juce::String(owner.chain == PluginHost::Chain::vocal ? "vocal:" : "stream:") + id, this);
    }
private:
    PluginChainPanel& owner;
    juce::String id;
    juce::TextButton name, bypass, remove;
};
PluginChainPanel::PluginChainPanel(PluginHost& h, PluginHost::Chain c, Scanner& s) : host(h), chain(c), scanner(s)
{
    viewport.setViewedComponent(&content, false); viewport.setScrollBarsShown(false, true); viewport.setScrollBarThickness(5);
    addAndMakeVisible(viewport); addAndMakeVisible(add);
    add.setColour(juce::TextButton::buttonColourId, ink); add.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    add.onClick = [this] { showAdd(); }; scan.onClick = [this] { showScanMenu(); };
    if (chain == PluginHost::Chain::vocal) addAndMakeVisible(scan);
    Numbers::id(add, chain == PluginHost::Chain::vocal ? 201 : 211); Numbers::id(scan, 202);
    refresh(); startTimer(150);
}
PluginChainPanel::~PluginChainPanel()
{
    stopTimer(); if (addWindow != nullptr) delete addWindow.getComponent(); if (scanWindow != nullptr) delete scanWindow.getComponent();
    if (projectWindow != nullptr) delete projectWindow.getComponent();
    viewport.setViewedComponent(nullptr);
}
void PluginChainPanel::refresh()
{
    tiles.clear();
    for (const auto& item : host.items(chain)) { auto tile = std::make_unique<Tile>(*this, item); content.addAndMakeVisible(*tile); tiles.push_back(std::move(tile)); }
    resized(); repaint();
}
void PluginChainPanel::paint(juce::Graphics& g)
{
    g.setColour(ink); g.setFont(juce::FontOptions(11.0f));
    g.drawText(chain == PluginHost::Chain::vocal ? "FX CHAIN" : "STREAM FX", 0, 0, 85, getHeight(), juce::Justification::centredLeft);
    if (tiles.empty()) g.drawText("(empty)", 90, 0, std::max(0, viewport.getWidth() - 10), getHeight(), juce::Justification::centredLeft);
}
void PluginChainPanel::resized()
{
    Numbers::area(*this, chain == PluginHost::Chain::vocal ? 200 : 210, {0, 0, 85, getHeight()});
    Numbers::area(*this, chain == PluginHost::Chain::vocal ? 203 : 213, tiles.empty() ? juce::Rectangle<int>(90, 0, std::max(1, viewport.getWidth() - 10), getHeight()) : juce::Rectangle<int>());
    auto r = getLocalBounds(); r.removeFromLeft(85);
    if (chain == PluginHost::Chain::vocal) { scan.setBounds(r.removeFromRight(48).reduced(0, 2)); r.removeFromRight(5); }
    add.setBounds(r.removeFromRight(66).reduced(0, 2)); r.removeFromRight(6); viewport.setBounds(r);
    const int width = std::max(r.getWidth(), static_cast<int>(tiles.size()) * 210);
    content.setSize(std::max(1, width), std::max(1, r.getHeight() - 6));
    for (size_t i = 0; i < tiles.size(); ++i) tiles[i]->setBounds(static_cast<int>(i) * 210, 0, 204, content.getHeight());
}
void PluginChainPanel::showAdd()
{
    if (addWindow != nullptr) { addWindow->toFront(true); return; }
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = juce::String::fromUTF8("プラグインを追加") + (chain == PluginHost::Chain::vocal ? " · FX CHAIN" : " · STREAM FX");
    options.dialogBackgroundColour = surface; options.content.setOwned(new AddPanel(host, chain, scanner));
    options.componentToCentreAround = this; options.useNativeTitleBar = true; options.resizable = true;
    addWindow = options.launchAsync(); if (addWindow != nullptr) addWindow->setLookAndFeel(&getLookAndFeel());
}
juce::Component* PluginChainPanel::addContent() const { return addWindow != nullptr ? addWindow->getContentComponent() : nullptr; }
void PluginChainPanel::showProject()
{
    if (projectWindow != nullptr) { projectWindow->toFront(true); return; }
    juce::DialogWindow::LaunchOptions options; options.dialogTitle = juce::String::fromUTF8("プロジェクトのプラグイン設定");
    options.dialogBackgroundColour = surface; options.content.setOwned(new FxProjectPanel(host)); options.componentToCentreAround = this;
    options.useNativeTitleBar = true; options.resizable = false; projectWindow = options.launchAsync();
    if (projectWindow != nullptr) projectWindow->setLookAndFeel(&getLookAndFeel());
}
void PluginChainPanel::showProgress()
{
    if (scanWindow != nullptr) { scanWindow->toFront(true); return; }
    juce::DialogWindow::LaunchOptions options; options.dialogTitle = juce::String::fromUTF8("プラグインの検索");
    options.dialogBackgroundColour = surface; options.content.setOwned(new ScanPanel(scanner)); options.componentToCentreAround = this;
    options.useNativeTitleBar = true; options.resizable = true; scanWindow = options.launchAsync();
    if (scanWindow != nullptr) scanWindow->setLookAndFeel(&getLookAndFeel());
}
void PluginChainPanel::showScanMenu()
{
    if (scanner.isRunning()) { showProgress(); return; }
    juce::PopupMenu menu; menu.addItem(1, juce::String::fromUTF8("すべて検索し直す")); menu.addItem(2, juce::String::fromUTF8("フォルダを指定して追加で検索"));
    const juce::Component::SafePointer<PluginChainPanel> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(scan), [safe](int answer)
    {
        if (safe == nullptr) return;
        if (answer == 1) { safe->scanner.start(true); safe->showProgress(); }
        if (answer == 2)
        {
            safe->folderChooser = std::make_unique<juce::FileChooser>(juce::String::fromUTF8("プラグインのフォルダを選択"));
            safe->folderChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                [safe](const juce::FileChooser& chooser)
                {
                    if (safe == nullptr || !chooser.getResult().isDirectory()) return;
                    safe->scanner.start(false, chooser.getResult()); safe->showProgress();
                });
        }
    });
}
bool PluginChainPanel::isInterestedInDragSource(const SourceDetails& details)
{
    return details.description.toString().startsWith(chain == PluginHost::Chain::vocal ? "vocal:" : "stream:");
}
void PluginChainPanel::itemDropped(const SourceDetails& details)
{
    const int x = details.localPosition.x - viewport.getX() + viewport.getViewPositionX();
    host.move(chain, details.description.toString().fromFirstOccurrenceOf(":", false, false), std::max(0, (x + 105) / 210));
}
void PluginChainPanel::timerCallback()
{
    const auto items = host.items(chain);
    bool changed = items.size() != tiles.size();
    juce::String signature;
    for (const auto& item : items) signature += item.id + (item.enabled ? "1" : "0");
    if (signature != lastSignature) changed = true;
    if (changed) { lastSignature = signature; refresh(); }
}
}
