#include "AppSelectionPanel.h"

namespace ksm
{
namespace
{
class AppRow final : public juce::Component
{
public:
    juce::ToggleButton check;
    juce::Image icon;
    juce::String name, detail;
    AppRow() { addAndMakeVisible(check); }
    void resized() override { check.setBounds(6, 5, 28, getHeight() - 10); }
    void paint(juce::Graphics& g) override
    {
        if (icon.isValid()) g.drawImageWithin(icon, 42, 10, 32, 32, juce::RectanglePlacement::centred);
        else { g.setColour(juce::Colour(0xffdedbd2)); g.fillRoundedRectangle(42, 10, 32, 32, 6); }
        g.setColour(juce::Colour(0xff30342f)); g.setFont(juce::FontOptions(14));
        g.drawText(name, 86, 5, getWidth() - 100, 24, juce::Justification::centredLeft, true);
        g.setColour(juce::Colour(0xff686c63)); g.setFont(juce::FontOptions(11));
        g.drawText(detail, 86, 29, getWidth() - 100, 20, juce::Justification::centredLeft, true);
    }
};
}
AppSelectionPanel::AppSelectionPanel(ProcessTapCapture& source, juce::File directory, std::function<void()> onApplied)
    : capture(source), projects(directory), draft(source.selection()), applied(std::move(onApplied)), rows(source.apps())
{
    list.setModel(this); list.setRowHeight(54); list.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xfff7f5ef));
    list.setColour(juce::ListBox::outlineColourId, juce::Colour(0xffcfcac0)); list.setOutlineThickness(1);
    mute.setToggleState(draft.muteDirect, juce::dontSendNotification);
    mute.onClick = [this] { draft.muteDirect = mute.getToggleState(); };
    clear.onClick = [this] { feedback.clear(); draft.bundleIDs.clear(); list.updateContent(); updateSummary(); };
    cancel.onClick = [this] { close(); };
    apply.onClick = [this]
    {
        const auto result = capture.select(draft);
        if (result.failed()) { error.setText(result.getErrorMessage(), juce::dontSendNotification); return; }
        if (applied) applied();
        if (capture.needsPermissionHelp()) { error.setText(capture.statusText(), juce::dontSendNotification); return; }
        close();
    };
    privacy.onClick = [] { ProcessTapCapture::openPrivacySettings(); };
    retry.onClick = [this] { capture.retryPermission(); updateSummary(); };
    project.setTextWhenNothingSelected(juce::String::fromUTF8("保存済み APP 選択"));
    projectName.setTextToShowWhenEmpty(juce::String::fromUTF8("プロジェクト名"), juce::Colour(0xff686c63));
    auto refreshProjects = [this]
    {
        project.clear(juce::dontSendNotification); int id = 1;
        for (const auto& name : projects.names()) project.addItem(name, id++);
    };
    refreshProjects();
    save.onClick = [this, refreshProjects]
    {
        if (auto result = capture.validate(draft); result.failed()) { feedback = result.getErrorMessage(); updateSummary(); return; }
        auto name = projectName.getText().trim();
        const auto result = projects.saveCapture(name, draft);
        feedback = result.wasOk() ? juce::String::fromUTF8("APP 選択を保存しました") : result.getErrorMessage();
        if (result.wasOk()) refreshProjects();
        updateSummary();
    };
    load.onClick = [this]
    {
        CaptureSelection loaded;
        auto result = projects.loadCapture(project.getText(), loaded);
        if (result.wasOk()) result = capture.validate(loaded);
        if (result.failed()) { feedback = result.getErrorMessage(); updateSummary(); return; }
        feedback = juce::String::fromUTF8("APP 選択を読み込みました。取り込むには「適用」を押してください。");
        draft = loaded; mute.setToggleState(draft.muteDirect, juce::dontSendNotification);
        projectName.setText(project.getText());
        rows = capture.apps();
        for (const auto& id : draft.bundleIDs)
            if (std::none_of(rows.begin(), rows.end(), [&](const auto& app) { return app.bundleID == id; })) rows.push_back({id, id, {}, {}, false});
        list.updateContent(); updateSummary();
    };
    for (auto* c : std::array<juce::Component*, 13>{ &list, &mute, &apply, &clear, &cancel, &privacy, &retry, &summary, &error, &project, &projectName, &save, &load })
        addAndMakeVisible(c);
    summary.setFont(juce::FontOptions(12)); error.setFont(juce::FontOptions(12));
    error.setColour(juce::Label::textColourId, juce::Colour(0xffa93b35));
    error.setMinimumHorizontalScale(0.85f);
    updateSummary(); setSize(740, 620); startTimer(1000);
}
AppSelectionPanel::~AppSelectionPanel() { stopTimer(); list.setModel(nullptr); }
int AppSelectionPanel::getNumRows() { return static_cast<int>(rows.size()); }
void AppSelectionPanel::paintListBoxItem(int, juce::Graphics&, int, int, bool) {}
juce::Component* AppSelectionPanel::refreshComponentForRow(int index, bool, juce::Component* existing)
{
    auto* row = dynamic_cast<AppRow*>(existing);
    if (row == nullptr) row = new AppRow();
    if (index < 0 || index >= static_cast<int>(rows.size())) return row;
    const auto& app = rows[static_cast<size_t>(index)];
    row->icon = app.icon;
    row->name = app.name + (app.hasAudio ? "" : juce::String::fromUTF8(" [音声未検出]"));
    row->detail = app.bundleID + " / " + juce::String(app.processes.size()) + juce::String::fromUTF8("プロセス");
    row->check.onClick = nullptr; row->check.setToggleState(draft.bundleIDs.contains(app.bundleID), juce::dontSendNotification);
    row->check.setTooltip(row->name);
    row->check.onClick = [this, id = app.bundleID, row]
    {
        feedback.clear();
        if (row->check.getToggleState()) draft.bundleIDs.addIfNotAlreadyThere(id); else draft.bundleIDs.removeString(id);
        updateSummary();
    };
    row->repaint(); return row;
}
void AppSelectionPanel::updateSummary()
{
    size_t processes = 0;
    for (const auto& app : capture.apps()) if (draft.bundleIDs.contains(app.bundleID)) processes += app.processes.size();
    summary.setText(juce::String(draft.bundleIDs.size()) + juce::String::fromUTF8(" / 8 アプリ、") + juce::String(processes) + juce::String::fromUTF8(" / 16 プロセス。音声未検出のアプリも選べます。"), juce::dontSendNotification);
    const auto result = capture.validate(draft);
    apply.setEnabled(result.wasOk());
    error.setText(result.failed() ? result.getErrorMessage() : capture.needsPermissionHelp() ? capture.statusText() : feedback, juce::dontSendNotification);
}
void AppSelectionPanel::timerCallback()
{
    auto next = capture.apps();
    for (const auto& id : draft.bundleIDs)
        if (std::none_of(next.begin(), next.end(), [&](const auto& app) { return app.bundleID == id; })) next.push_back({id, id, {}, {}, false});
    rows = std::move(next); list.updateContent(); updateSummary();
}
void AppSelectionPanel::close() { if (auto* window = findParentComponentOfClass<juce::DialogWindow>()) window->exitModalState(0); }
void AppSelectionPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xfff7f5ef)); g.setColour(juce::Colour(0xff30342f)); g.setFont(juce::FontOptions(13));
    g.drawText(juce::String::fromUTF8("取り込むアプリを選択"), 18, 10, getWidth() - 36, 26, juce::Justification::centredLeft);
    g.setFont(juce::FontOptions(11));
    g.drawText(juce::String::fromUTF8("初回はシステムオーディオ録音の許可が必要です。拒否した場合はシステム設定で許可してください。"), 18, getHeight() - 139, getWidth() - 36, 22, juce::Justification::centredLeft);
    g.drawText(juce::String::fromUTF8("プロジェクトには、この画面の APP 選択と消音設定を保存します。"), 18, getHeight() - 251, getWidth() - 36, 22, juce::Justification::centredLeft);
}
void AppSelectionPanel::resized()
{
    list.setBounds(18, 40, getWidth() - 36, getHeight() - 326);
    summary.setBounds(18, getHeight() - 282, getWidth() - 36, 25);
    project.setBounds(18, getHeight() - 225, 240, 28); load.setBounds(266, getHeight() - 225, 90, 28);
    projectName.setBounds(365, getHeight() - 225, 202, 28); save.setBounds(575, getHeight() - 225, 147, 28);
    mute.setBounds(18, getHeight() - 188, getWidth() - 36, 30);
    privacy.setBounds(18, getHeight() - 112, 235, 28); retry.setBounds(263, getHeight() - 112, 180, 28);
    error.setBounds(18, getHeight() - 80, getWidth() - 36, 32);
    clear.setBounds(18, getHeight() - 42, 110, 28); cancel.setBounds(getWidth() - 238, getHeight() - 42, 110, 28); apply.setBounds(getWidth() - 128, getHeight() - 42, 110, 28);
}
}
