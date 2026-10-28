#include "MainComponent.h"
#include <limits>

namespace ksm
{
namespace
{
const std::array<const char*, 9> faderNames { "instMonitor", "streamInst", "streamVocal", "vocalMonitor",
    "bgmStream", "bgmMonitor", "seStream", "seMonitor", "master" };
class AudioPanel final : public juce::Component
{
public:
    AudioPanel(AudioEngine& e, std::function<void()> callback, std::function<void()> showHelp) : engine(e), edited(std::move(callback))
    {
        for (int size : {32, 64, 128, 256, 512, 1024}) buffer.addItem(juce::String(size) + " samples", size);
        for (int hz : {44100, 48000, 96000}) rate.addItem(juce::String(hz) + " Hz", hz);
        buffer.setSelectedId(e.selection().bufferFrames, juce::dontSendNotification);
        rate.setSelectedId(static_cast<int>(e.selection().requestedRate > 0 ? e.selection().requestedRate : e.sampleRate()), juce::dontSendNotification);
        apply.setButtonText(juce::String::fromUTF8("適用"));
        apply.onClick = [this]
        {
            auto s = engine.selection(); s.bufferFrames = buffer.getSelectedId(); s.requestedRate = rate.getSelectedId();
            if (s.bufferFrames == 0 || s.requestedRate <= 0.0) return;
            const auto result = engine.selectDevices(s);
            if (result.wasOk() && edited) edited();
            repaint();
        };
        help.setButtonText(juce::String::fromUTF8("遅れの表示について")); help.onClick = std::move(showHelp);
        addAndMakeVisible(buffer); addAndMakeVisible(rate); addAndMakeVisible(apply); addAndMakeVisible(help);
        setSize(430, 230);
    }
    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xfff7f5ef)); g.setColour(juce::Colour(0xff30342f)); g.setFont(juce::FontOptions(13.0f));
        g.drawText(juce::String::fromUTF8("バッファサイズ"), 18, 18, 140, 30, juce::Justification::centredLeft);
        g.drawText(juce::String::fromUTF8("サンプルレート"), 18, 58, 140, 30, juce::Justification::centredLeft);
        g.drawFittedText(juce::String::fromUTF8("MIC INPUT のクロックに従います。対応しない値はデバイスの実際の値で動作します。"), 18, 104, 390, 42, juce::Justification::centredLeft, 2);
        g.drawText(engine.latencyText(), 18, 155, 390, 25, juce::Justification::centredLeft);
    }
    void resized() override
    {
        buffer.setBounds(165, 18, 240, 30); rate.setBounds(165, 58, 240, 30); apply.setBounds(315, 185, 90, 30); help.setBounds(18, 185, 190, 30);
    }
private:
    AudioEngine& engine;
    std::function<void()> edited;
    juce::ComboBox buffer, rate;
    juce::TextButton apply, help;
};
}
MainComponent::MainComponent(bool previewOnly, bool diagnostic) : scanner(settings.directory()),
    vocalPlugins(engine.router.plugins, PluginHost::Chain::vocal, scanner),
    streamPlugins(engine.router.plugins, PluginHost::Chain::stream, scanner), devices(engine)
{
    const juce::Colour ink(0xff30342f), surface(0xfff7f5ef), border(0xffcfcac0), accent(0xff477aaf);
    lookAndFeel.setColour(juce::Label::textColourId, ink);
    lookAndFeel.setColour(juce::ComboBox::backgroundColourId, surface);
    lookAndFeel.setColour(juce::ComboBox::textColourId, ink);
    lookAndFeel.setColour(juce::ComboBox::arrowColourId, ink);
    lookAndFeel.setColour(juce::ComboBox::outlineColourId, border);
    lookAndFeel.setColour(juce::PopupMenu::backgroundColourId, surface);
    lookAndFeel.setColour(juce::PopupMenu::textColourId, ink);
    lookAndFeel.setColour(juce::PopupMenu::highlightedBackgroundColourId, accent);
    lookAndFeel.setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    lookAndFeel.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffe4e0d6));
    lookAndFeel.setColour(juce::TextButton::textColourOffId, ink);
    lookAndFeel.setColour(juce::Slider::backgroundColourId, border);
    lookAndFeel.setColour(juce::Slider::trackColourId, accent);
    lookAndFeel.setColour(juce::Slider::thumbColourId, ink);
    lookAndFeel.setColour(juce::Slider::textBoxTextColourId, ink);
    lookAndFeel.setColour(juce::Slider::textBoxBackgroundColourId, surface);
    lookAndFeel.setColour(juce::Slider::textBoxOutlineColourId, border);
    lookAndFeel.setColour(juce::TextEditor::textColourId, ink);
    lookAndFeel.setColour(juce::TextEditor::backgroundColourId, surface);
    lookAndFeel.setColour(juce::TextEditor::outlineColourId, border);
    lookAndFeel.setColour(juce::ToggleButton::textColourId, ink);
    lookAndFeel.setColour(juce::ToggleButton::tickColourId, ink);
    lookAndFeel.setColour(juce::ToggleButton::tickDisabledColourId, border);
    setLookAndFeel(&lookAndFeel);
    setWantsKeyboardFocus(true);
    preview = previewOnly;
    diagnosticMode = diagnostic;
    if (!preview) loadSettings();
    for (size_t i = 0; i < strips.size(); ++i)
    {
        strips[i] = std::make_unique<MixerStrip>(static_cast<int>(i), engine.router);
        strips[i]->onEdited = [this] { edited(); };
        addAndMakeVisible(*strips[i]);
    }
    devices.onEdited = [this] { edited(); };
    sounds.onEdited = [this] { dirty = true; saveAfter = juce::Time::getMillisecondCounterHiRes() + 500.0; publishRemote(); };
    engine.onChange = [this] { devices.refresh(); };
    panel.onClick = [this] { showPanel(); };
    permission.onClick = [this] { engine.requestMicrophone(); };
    menu.onClick = [this]
    {
        juce::PopupMenu items;
        const std::array<const char*, 9> labels { "チュートリアルをもう一度", "使い方（内蔵ヘルプ）", "声の遅れの調整（PANEL）",
            "BGM と効果音の使い方", "配信の音を大きくする（LOUD）", "OBS の設定のしかた", "Stream Deck の使い方", "設定フォルダを開く", "バージョン情報" };
        for (size_t i = 0; i < labels.size(); ++i) items.addItem(static_cast<int>(i) + 1, juce::String::fromUTF8(labels[i]));
        const juce::Component::SafePointer<MainComponent> safe(this);
        items.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(menu), [safe](int choice)
        {
            if (safe == nullptr) return;
            if (choice == 1) safe->startTutorial();
            if (choice == 2) safe->showHelp(1);
            if (choice == 3) safe->showPanel();
            if (choice == 4) safe->showHelp(7);
            if (choice == 5) safe->showHelp(8);
            if (choice == 6) safe->showHelp(4);
            if (choice == 7) safe->showHelp(10);
            if (choice == 8) safe->settings.directory().revealToUser();
            if (choice == 9) juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                juce::String::fromUTF8("バージョン情報"), "Klinglab Stream Mixer " JUCE_APPLICATION_VERSION_STRING "\nCopyright © 2026 Klinglab\nAGPL-3.0-only\nJUCE 8 / macOS 14.4+");
        });
    };
    addAndMakeVisible(devices); addAndMakeVisible(sounds);
    addAndMakeVisible(vocalPlugins); addAndMakeVisible(streamPlugins); addAndMakeVisible(loud);
    loud.onEdited = [this] { edited(); };
    vocalPlugins.onEdited = streamPlugins.onEdited = [this] { edited(); };
    engine.router.plugins.onChange = [this] { edited(); };
    engine.router.plugins.onError = [](const juce::String& error)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, juce::String::fromUTF8("プラグインの処理"), error);
    };
    for (auto* c : std::array<juce::Component*, 6> { &panel, &permission, &menu, &latency, &load, &status }) addAndMakeVisible(c);
    load.setFont(juce::FontOptions(10.0f));
    load.setJustificationType(juce::Justification::centred);
    load.setTooltip(juce::String::fromUTF8("音声コールバックの処理時間 ÷ バッファ時間。平均と起動後の最大。100%を超えると音切れの目安です。"));
    latency.setJustificationType(juce::Justification::centred);
    latency.setFont(juce::FontOptions(12.0f));
    status.setFont(juce::FontOptions(12.0f));
    status.setColour(juce::Label::textColourId, juce::Colour(0xff686c63));
    for (auto* c : std::array<juce::Component*, 6> { &projectBox, &projectSlots, &projectNew, &projectSave, &projectSaveAs, &projectDelete })
        addAndMakeVisible(c);
    projectSave.setColour(juce::TextButton::buttonColourId, ink);
    projectSave.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    projectDelete.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffad3434));
    projectBox.onChange = [this]
    {
        const int index = projectBox.getSelectedId() - 1;
        if (index >= 0 && index < projectNames.size())
        {
            const auto result = beginProjectLoad(projectNames[index]);
            if (result.failed()) juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, juce::String::fromUTF8("プロジェクト"), result.getErrorMessage());
        }
    };
    projectNew.onClick = [this] { newProject(); };
    projectSave.onClick = [this] { showSaveProject(false); };
    projectSaveAs.onClick = [this] { showSaveProject(true); };
    projectSlots.onClick = [this] { showProjectSlots(); };
    projectDelete.onClick = [this] { confirmDeleteProject(); };
    updateProjectBox();
    setSize(1280, 800);
    if (!preview)
    {
        applyingProject = true;
        if (diagnosticMode) { engine.startDiagnostic(); diagnosticTime = juce::Time::getMillisecondCounterHiRes(); }
        else engine.start();
        engine.router.plugins.restoreState(settings.load()["plugins"], [this](juce::Result result)
        {
            applyingProject = false;
            lastNRRevision = engine.router.nr.profileRevision(); lastLoudRevision = engine.router.loudness.revision();
            if (result.failed()) juce::Logger::writeToLog("FX restore errors " + result.getErrorMessage());
        });
    }
    devices.refresh();
    if (!preview)
    {
        hotkeys = std::make_unique<Hotkeys>();
        projectSlots.setButtonText(juce::String::fromUTF8(hotkeys->isRegistered() ? "1〜9登録" : "1〜9登録 !"));
        projectSlots.setTooltip(hotkeys->errorText());
        hotkeys->onProject = [this](int n)
        {
            if (slots[static_cast<size_t>(n - 1)].isNotEmpty()) beginProjectLoad(slots[static_cast<size_t>(n - 1)], n);
        };
        hotkeys->onFx = [this](int n)
        {
            const auto list = engine.router.plugins.items(PluginHost::Chain::vocal);
            if (n <= static_cast<int>(list.size())) engine.router.plugins.toggle(PluginHost::Chain::vocal, list[static_cast<size_t>(n - 1)].id);
        };
        remote.onRequest = [this](const juce::var& request) { return remoteRequest(request); };
        remote.update(remoteState());
        const auto result = remote.start();
        if (result.failed()) { projectNotice = result.getErrorMessage(); noticeUntil = juce::Time::getMillisecondCounterHiRes() + 10000.0; juce::Logger::writeToLog(result.getErrorMessage()); }
    }
    addChildComponent(tutorial); addAndMakeVisible(guidance);
    tutorial.onSkip = [this] { finishTutorial(); };
    tutorial.onSkipFx = [this] { if (tutorialStep == 3) { tutorialStep = 4; tutorialSince = juce::Time::getMillisecondCounterHiRes(); layoutGuidance(); } };
    tutorial.onInstall = [this]
    {
        const auto result = installOBSPlugin();
        tutorial.showStep(tutorialStep, obsPluginInstalled(), !scanner.plugins().isEmpty());
        tutorial.setInstallStatus(result.wasOk() ? juce::String::fromUTF8("導入しました。OBS を起動中なら再起動してください。") : result.getErrorMessage());
    };
    tutorial.onHelp = [this] { const std::array<int, 5> chapters {2, 3, 4, 6, 9}; showHelp(chapters[static_cast<size_t>(juce::jlimit(0, 4, tutorialStep))]); };
    guidance.setAlwaysOnTop(true);
    const std::array<juce::Component*, 12> numbered {&panel, &permission, &menu, &latency, &load, &status, &projectBox, &projectSlots, &projectNew, &projectSave, &projectSaveAs, &projectDelete};
    for (size_t i = 0; i < numbered.size(); ++i) Numbers::id(*numbered[i], 10 + static_cast<int>(i));
    dirty = false;
    startTimerHz(30);
}
MainComponent::~MainComponent()
{
    stopTimer();
    remote.stop(); hotkeys.reset();
    if (audioPanel != nullptr) delete audioPanel.getComponent();
    if (streamDeckHelp != nullptr) delete streamDeckHelp.getComponent();
    devices.closeAppSelection();
    scanner.cancel();
    engine.router.plugins.closeEditors();
    saveSettings();
    engine.onChange = nullptr;
    engine.router.plugins.onChange = nullptr;
    engine.router.plugins.onError = nullptr;
    engine.stop();
    setLookAndFeel(nullptr);
}
void MainComponent::showStreamDeckHelp() { showHelp(10); }
void MainComponent::showHelp(int chapter, const juce::String& heading)
{
    if (streamDeckHelp != nullptr)
    {
        if (auto* help = dynamic_cast<HelpPanel*>(streamDeckHelp->getContentComponent())) help->openChapter(chapter, heading);
        streamDeckHelp->toFront(true); return;
    }
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = juce::String::fromUTF8("Klinglab Stream Mixer · 使い方");
    options.dialogBackgroundColour = juce::Colour(0xfff7f5ef);
    auto* help = new HelpPanel(); help->openChapter(chapter, heading);
    options.content.setOwned(help); options.componentToCentreAround = this;
    options.useNativeTitleBar = true; options.resizable = true;
    streamDeckHelp = options.launchAsync();
    if (streamDeckHelp != nullptr) { streamDeckHelp->setLookAndFeel(&lookAndFeel); streamDeckHelp->setResizeLimits(760, 500, 1600, 1100); }
}
void MainComponent::toggleNumbers()
{
    guidance.numbers = !guidance.numbers; guidance.repaint();
    juce::Logger::writeToLog("NUMBERS visible=" + juce::String(guidance.numbers ? 1 : 0));
}
bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    if (key.getModifiers().isAltDown() && (key.getKeyCode() == 'N' || key.getKeyCode() == 'n')) { toggleNumbers(); return true; }
    return false;
}
void MainComponent::startTutorial()
{
    tutorialStarted = true; tutorialStep = 0; tutorialSaved = false;
    tutorialInputSeen = tutorialStreamSeen = false;
    tutorialSince = juce::Time::getMillisecondCounterHiRes();
    tutorial.setVisible(true); layoutGuidance();
    juce::Logger::writeToLog("TUTORIAL started");
}
void MainComponent::finishTutorial()
{
    tutorialDismissed = true; tutorialStep = -1; tutorial.setVisible(false); guidance.highlights.clear(); guidance.repaint();
    if (!preview) { dirty = true; saveAfter = juce::Time::getMillisecondCounterHiRes(); }
    juce::Logger::writeToLog("TUTORIAL dismissed");
}
void MainComponent::updateTutorial(double now)
{
    if (!preview && !diagnosticMode && !tutorialDismissed && !tutorialStarted) startTutorial();
    if (tutorialStep < 0) return;
    if (tutorialStep == 1 && engine.selection().instMode == "APP" && !engine.appCapture.selection().bundleIDs.isEmpty())
        for (float value : engine.router.inputMeter.current()) tutorialInputSeen = tutorialInputSeen || value >= 0.001f;
    if (tutorialStep == 2) for (float value : engine.router.streamMeter.current()) tutorialStreamSeen = tutorialStreamSeen || value >= 0.001f;
    const bool complete = tutorialStep == 0 ? engine.routeAvailable(1) && engine.routeAvailable(2)
        : tutorialStep == 1 ? tutorialInputSeen
        : tutorialStep == 2 ? obsPluginInstalled() && tutorialStreamSeen
        : tutorialStep == 3 ? engine.router.plugins.vocalCount() > 0 : tutorialSaved;
    if (complete && now - tutorialSince >= 1500.0)
    {
        juce::Logger::writeToLog("TUTORIAL completed step=" + juce::String(tutorialStep + 1));
        if (++tutorialStep >= 5) { finishTutorial(); return; }
        tutorialSince = now; layoutGuidance();
    }
}
void MainComponent::layoutGuidance()
{
    guidance.setBounds(getLocalBounds()); guidance.highlights.clear();
    Numbers::area(*this, 1, {20, 10, 310, 36});
    if (tutorialStep >= 0)
    {
        if (tutorialStep == 0) { guidance.highlights.add(devices.getBounds().withTrimmedTop(78).withHeight(62)); }
        if (tutorialStep == 1) { guidance.highlights.add(devices.getBounds().withTrimmedTop(44).withHeight(28)); guidance.highlights.add(strips[0]->getBounds()); }
        if (tutorialStep == 2) guidance.highlights.add(strips[2]->getBounds());
        if (tutorialStep == 3) guidance.highlights.add(vocalPlugins.getBounds());
        if (tutorialStep == 4) guidance.highlights.add(projectSave.getBounds());
        tutorial.showStep(tutorialStep, obsPluginInstalled(), !scanner.plugins().isEmpty());
        tutorial.setBounds(getWidth() - 440, tutorialStep == 2 ? 70 : getHeight() - 265, 420, 235);
        tutorial.toFront(false);
    }
    guidance.toFront(false); guidance.repaint();
}
void MainComponent::loadSettings()
{
    const auto data = settings.load();
    tutorialDismissed = static_cast<bool>(data["tutorialDismissed"]);
    currentProject = data["currentProject"].toString();
    projectDirty = static_cast<bool>(data["projectDirty"]);
    if (auto* registered = data["projectSlots"].getArray())
        for (int i = 0; i < std::min(9, registered->size()); ++i) slots[static_cast<size_t>(i)] = (*registered)[i].toString();
    const auto restoredCapture = CaptureSelection::fromJson(data["appCapture"]);
    if (auto result = engine.appCapture.select(restoredCapture); result.failed())
        juce::Logger::writeToLog("SETTINGS APP selection rejected: " + result.getErrorMessage());
    const auto d = data["devices"];
    DeviceSelection s;
    s.instMode = d["instMode"].toString() == "CABLE" ? "CABLE" : "APP";
    s.cableUID = d["cableUID"].toString(); s.micUID = d["micUID"].toString(); s.monitorUID = d["monitorUID"].toString();
    s.streamUID = d["streamUID"].toString(); s.micMonUID = d["micMonUID"].toString();
    s.micChannels = d.hasProperty("micChannels") ? juce::jlimit(1, 3, static_cast<int>(d["micChannels"])) : 1;
    const int size = static_cast<int>(d["bufferFrames"]);
    if (size == 32 || size == 64 || size == 128 || size == 256 || size == 512 || size == 1024) s.bufferFrames = size;
    const double rate = static_cast<double>(d["requestedRate"]);
    if (rate == 44100.0 || rate == 48000.0 || rate == 96000.0) s.requestedRate = rate;
    if (engine.selectDevices(s).failed())
    {
        // Refuse a persisted feedback loop without disabling the independent routes.
        s.streamUID.clear(); engine.selectDevices(s);
        juce::Logger::writeToLog("SETTINGS refused CABLE / STREAM OUT feedback loop");
    }
    const auto faders = engine.router.faders();
    for (size_t i = 0; i < faders.size(); ++i)
        if (data["faders"].hasProperty(faderNames[i])) faders[i]->setDecibels(static_cast<float>(static_cast<double>(data["faders"][faderNames[i]])));
    if (data.hasProperty("micMonGain")) engine.router.micMonGain.store(juce::jlimit(0.0f, 2.0f, static_cast<float>(static_cast<double>(data["micMonGain"]))));
    if (data.hasProperty("monWet")) engine.router.monWet.store(juce::jlimit(0.0f, 1.0f, static_cast<float>(static_cast<double>(data["monWet"]))));
    if (data.hasProperty("vocalMon")) engine.router.vocalMonEnabled.store(static_cast<bool>(data["vocalMon"]));
    if (data.hasProperty("bgmMon")) engine.router.bgmMonEnabled.store(static_cast<bool>(data["bgmMon"]));
    if (data.hasProperty("seMon")) engine.router.seMonEnabled.store(static_cast<bool>(data["seMon"]));
    engine.router.manualDelayMs.store(juce::jlimit(-400, 400, static_cast<int>(data["manualDelayMs"]) / 5) * 5);
    if (auto result = engine.router.nr.restoreState(data["nr"]); result.failed())
        juce::Logger::writeToLog("SETTINGS NR " + result.getErrorMessage());
    else restoringNRProfile = data["nr"].isObject();
    if (auto result = engine.router.loudness.restoreState(data["loudness"]); result.failed())
        juce::Logger::writeToLog("SETTINGS LOUD " + result.getErrorMessage());
    engine.sounds.restoreState(data["sounds"]);
    juce::Logger::writeToLog("SETTINGS restored devices and faders");
}
void MainComponent::saveSettings()
{
    if (preview) { dirty = false; return; }
    auto data = settings.load();
    auto* root = data.getDynamicObject();
    if (root == nullptr) return;
    auto* d = new juce::DynamicObject();
    const auto& s = engine.selection();
    d->setProperty("instMode", s.instMode); d->setProperty("cableUID", s.cableUID); d->setProperty("micUID", s.micUID);
    d->setProperty("monitorUID", s.monitorUID); d->setProperty("streamUID", s.streamUID); d->setProperty("micMonUID", s.micMonUID);
    d->setProperty("micChannels", s.micChannels); d->setProperty("bufferFrames", s.bufferFrames); d->setProperty("requestedRate", s.requestedRate);
    root->setProperty("devices", juce::var(d));
    root->setProperty("appCapture", (savedCapture.has_value() ? *savedCapture : engine.appCapture.selection()).toJson());
    auto* f = new juce::DynamicObject(); const auto faders = engine.router.faders();
    for (size_t i = 0; i < faders.size(); ++i) f->setProperty(faderNames[i], faders[i]->getDecibels());
    root->setProperty("faders", juce::var(f));
    root->setProperty("micMonGain", engine.router.micMonGain.load());
    root->setProperty("monWet", engine.router.monWet.load());
    root->setProperty("vocalMon", engine.router.vocalMonEnabled.load());
    root->setProperty("bgmMon", engine.router.bgmMonEnabled.load()); root->setProperty("seMon", engine.router.seMonEnabled.load());
    root->setProperty("manualDelayMs", engine.router.manualDelayMs.load());
    root->setProperty("nr", engine.router.nr.saveState());
    root->setProperty("loudness", engine.router.loudness.saveState());
    root->setProperty("sounds", engine.sounds.saveState());
    root->setProperty("tutorialDismissed", tutorialDismissed);
    root->setProperty("currentProject", currentProject); root->setProperty("projectDirty", projectDirty);
    juce::Array<juce::var> registered;
    for (const auto& name : slots) registered.add(name);
    root->setProperty("projectSlots", registered);
    if (savedPlugins.has_value()) root->setProperty("plugins", *savedPlugins);
    else if (!engine.router.plugins.isRestoring()) root->setProperty("plugins", engine.router.plugins.saveState());
    const auto result = settings.save(data);
    juce::Logger::writeToLog(result.wasOk() ? "SETTINGS saved devices and faders" : "SETTINGS ERROR " + result.getErrorMessage());
    dirty = false;
}
void MainComponent::edited()
{
    dirty = true; saveAfter = juce::Time::getMillisecondCounterHiRes() + 500.0;
    if (!applyingProject) { projectDirty = true; updateProjectBox(); publishRemote(); }
}
juce::var MainComponent::projectState()
{
    auto* root = new juce::DynamicObject();
    root->setProperty("version", 1);
    auto* d = new juce::DynamicObject();
    const auto& s = engine.selection();
    d->setProperty("instMode", s.instMode); d->setProperty("cableUID", s.cableUID); d->setProperty("micUID", s.micUID);
    d->setProperty("monitorUID", s.monitorUID); d->setProperty("streamUID", s.streamUID); d->setProperty("micMonUID", s.micMonUID);
    d->setProperty("micChannels", s.micChannels); d->setProperty("bufferFrames", s.bufferFrames); d->setProperty("requestedRate", s.requestedRate);
    root->setProperty("devices", juce::var(d));
    root->setProperty("appCapture", engine.appCapture.selection().toJson());
    auto* f = new juce::DynamicObject(); const auto faders = engine.router.faders();
    for (size_t i = 0; i < faders.size(); ++i) f->setProperty(faderNames[i], faders[i]->getDecibels());
    root->setProperty("faders", juce::var(f));
    root->setProperty("micMonGain", engine.router.micMonGain.load()); root->setProperty("monWet", engine.router.monWet.load());
    root->setProperty("vocalMon", engine.router.vocalMonEnabled.load()); root->setProperty("bgmMon", engine.router.bgmMonEnabled.load());
    root->setProperty("seMon", engine.router.seMonEnabled.load()); root->setProperty("manualDelayMs", engine.router.manualDelayMs.load());
    root->setProperty("nr", engine.router.nr.saveState()); root->setProperty("loudness", engine.router.loudness.saveState());
    root->setProperty("plugins", engine.router.plugins.saveState());
    return juce::var(root);
}
namespace
{
juce::var fields(std::initializer_list<std::pair<juce::Identifier, juce::var>> values)
{
    auto* o = new juce::DynamicObject();
    for (const auto& [key, value] : values) o->setProperty(key, value);
    return juce::var(o);
}
bool numberIn(const juce::var& value, double low, double high)
{
    if (!value.isInt() && !value.isInt64() && !value.isDouble()) return false;
    const double n = static_cast<double>(value);
    return std::isfinite(n) && n >= low && n <= high;
}
bool whole(const juce::var& value) { return value.isInt() || value.isInt64(); }
DeviceSelection projectDevices(const juce::var& d)
{
    DeviceSelection s;
    s.instMode = d["instMode"].toString(); s.cableUID = d["cableUID"].toString(); s.micUID = d["micUID"].toString();
    s.monitorUID = d["monitorUID"].toString(); s.streamUID = d["streamUID"].toString(); s.micMonUID = d["micMonUID"].toString();
    s.micChannels = static_cast<int>(d["micChannels"]); s.bufferFrames = static_cast<int>(d["bufferFrames"]);
    s.requestedRate = static_cast<double>(d["requestedRate"]); return s;
}
}
juce::Result MainComponent::validateProject(const juce::var& data) const
{
    const auto invalid = juce::Result::fail(juce::String::fromUTF8("プロジェクトの値が不正です。現在の設定を続けます。"));
    if (!data.isObject() || !whole(data["version"]) || static_cast<int64_t>(data["version"]) != 1) return invalid;
    const auto d = data["devices"];
    if (!d.isObject() || (d["instMode"].toString() != "APP" && d["instMode"].toString() != "CABLE")) return invalid;
    for (const auto* key : { "cableUID", "micUID", "monitorUID", "streamUID", "micMonUID" }) if (!d[key].isString()) return invalid;
    if (!whole(d["micChannels"]) || !numberIn(d["micChannels"], 1, 3) || !whole(d["bufferFrames"])) return invalid;
    const auto s = projectDevices(d);
    if (s.bufferFrames != 32 && s.bufferFrames != 64 && s.bufferFrames != 128 && s.bufferFrames != 256 && s.bufferFrames != 512 && s.bufferFrames != 1024) return invalid;
    if (!numberIn(d["requestedRate"], 0, 96000) || (s.requestedRate != 0 && s.requestedRate != 44100 && s.requestedRate != 48000 && s.requestedRate != 96000)) return invalid;
    if (s.instMode == "CABLE" && s.cableUID.isNotEmpty() && s.cableUID == s.streamUID) return invalid;
    for (size_t i = 0; i < faderNames.size(); ++i)
        if (!numberIn(data["faders"][faderNames[i]], -60, (i == 0 || i == 3) ? 0 : 6)) return invalid;
    if (!numberIn(data["micMonGain"], 0, 2) || !numberIn(data["monWet"], 0, 1)) return invalid;
    for (const auto* key : { "vocalMon", "bgmMon", "seMon" }) if (!data[key].isBool()) return invalid;
    if (!whole(data["manualDelayMs"]) || !numberIn(data["manualDelayMs"], -2000, 2000) || static_cast<int>(data["manualDelayMs"]) % 5 != 0) return invalid;
    const auto capture = data["appCapture"];
    if (!capture.isObject() || !capture["bundleIDs"].isArray() || !capture["muteDirect"].isBool()) return invalid;
    for (const auto& id : *capture["bundleIDs"].getArray()) if (!id.isString() || id.toString().isEmpty()) return invalid;
    if (const auto result = engine.appCapture.validate(CaptureSelection::fromJson(capture)); result.failed()) return result;
    const auto nr = data["nr"], loudState = data["loudness"];
    if (!nr["enabled"].isBool() || !nr["learned"].isBool() || !numberIn(nr["reductionDb"], 0, 18) || !nr["noiseRms"].isArray()) return invalid;
    for (const auto& n : *nr["noiseRms"].getArray()) if (!numberIn(n, 0, 1.0e6)) return invalid;
    NR nrCheck; if (auto result = nrCheck.restoreState(nr); result.failed()) return result;
    if (!loudState["enabled"].isBool() || !numberIn(loudState["learnedDecibels"], -120, 120)) return invalid;
    Loudness loudCheck; if (auto result = loudCheck.restoreState(loudState); result.failed()) return result;
    const auto plugins = data["plugins"];
    for (const auto* key : { "vocal", "stream" })
    {
        const auto* array = plugins[key].getArray(); if (array == nullptr) return invalid;
        for (const auto& entry : *array)
        {
            if (!entry.isObject() || !entry["enabled"].isBool() || !entry["description"].isString() || !entry["state"].isString()) return invalid;
            juce::PluginDescription description;
            const auto xml = juce::parseXML(entry["description"].toString());
            if (xml == nullptr || !description.loadFromXml(*xml)) return invalid;
            juce::MemoryOutputStream decoded;
            juce::MemoryBlock legacy;
            if (entry["encoding"].toString() == "base64") { if (!juce::Base64::convertFromBase64(decoded, entry["state"].toString())) return invalid; }
            else if (!legacy.fromBase64Encoding(entry["state"].toString())) return invalid;
        }
    }
    return juce::Result::ok();
}
juce::Result MainComponent::applyProject(const juce::var& data)
{
    // Devices, capture and plugins have committed before these validated values are applied.
    const auto faders = engine.router.faders();
    for (size_t i = 0; i < faders.size(); ++i) faders[i]->setDecibels(static_cast<float>(static_cast<double>(data["faders"][faderNames[i]])));
    engine.router.micMonGain.store(static_cast<float>(static_cast<double>(data["micMonGain"])));
    engine.router.monWet.store(static_cast<float>(static_cast<double>(data["monWet"])));
    engine.router.vocalMonEnabled.store(static_cast<bool>(data["vocalMon"]));
    engine.router.bgmMonEnabled.store(static_cast<bool>(data["bgmMon"])); engine.router.seMonEnabled.store(static_cast<bool>(data["seMon"]));
    engine.router.manualDelayMs.store(static_cast<int>(data["manualDelayMs"]));
    engine.router.nr.restoreState(data["nr"]); restoringNRProfile = true;
    engine.router.loudness.cancelLearn(); engine.router.loudness.restoreState(data["loudness"]);
    lastLearning = false;
    return juce::Result::ok();
}
juce::Result MainComponent::beginProjectLoad(const juce::String& name, int slot)
{
    if (!projectReady()) return juce::Result::fail(juce::String::fromUTF8("プロジェクトを読み込み中です。"));
    // Defer disk parsing and plugin creation so even an empty chain completes after the acceptance result.
    projectLoading = true; loadingSlot = slot; updateProjectBox(); publishRemote();
    const juce::Component::SafePointer<MainComponent> safe(this);
    juce::MessageManager::callAsync([safe, name]
    {
        if (safe == nullptr) return;
        juce::var data;
        auto result = safe->projects.load(name, data);
        if (result.wasOk()) result = safe->validateProject(data);
        auto finished = [safe, name, data](juce::Result outcome)
        {
            if (safe == nullptr) return;
            safe->lastProjectLoad = outcome;
            safe->applyingProject = false; safe->projectLoading = false; safe->loadingSlot = 0;
            if (outcome.wasOk())
            {
                safe->currentProject = name; safe->projectDirty = false;
                safe->lastNRRevision = safe->engine.router.nr.profileRevision(); safe->lastLoudRevision = safe->engine.router.loudness.revision();
                safe->dirty = true; safe->saveAfter = juce::Time::getMillisecondCounterHiRes();
                juce::Logger::writeToLog("PROJECT loaded " + name + " streamInst=" + juce::String(safe->engine.router.streamInst.getDecibels(), 1));
            }
            else
            {
                safe->projectNotice = outcome.getErrorMessage(); safe->noticeUntil = juce::Time::getMillisecondCounterHiRes() + 10000.0;
                juce::Logger::writeToLog("PROJECT load failed " + name + ": " + outcome.getErrorMessage());
            }
            safe->devices.refresh(); safe->updateProjectBox(); safe->publishRemote();
            if (outcome.failed()) safe->remote.error("LOAD_FAILED", outcome.getErrorMessage());
        };
        if (result.failed()) { finished(result); return; }
        safe->engine.router.plugins.restoreState(data["plugins"], finished, true, {}, [safe, data](std::function<juce::Result()> commit)
        {
            if (safe == nullptr) return juce::Result::fail("Window closed");
            safe->applyingProject = true;
            const auto capture = CaptureSelection::fromJson(data["appCapture"]);
            return safe->engine.selectDevices(projectDevices(data["devices"]), &capture, [safe, data, commit]
            {
                if (auto committed = commit(); committed.failed()) return committed;
                return safe->applyProject(data);
            });
        });
    });
    return juce::Result::ok();
}
juce::Result MainComponent::loadProjectForRun(const juce::String& name) { return beginProjectLoad(name); }
juce::Result MainComponent::saveProjectForRun(const juce::String& name, int slot)
{
    if (!projectReady()) return juce::Result::fail(juce::String::fromUTF8("読み込みが終わってから保存してください。"));
    auto result = projects.save(name, projectState());
    if (result.failed()) return result;
    currentProject = name; projectDirty = false;
    if (tutorialStep == 4) tutorialSaved = true;
    if (diagnosticMode) savedPlugins.reset();
    if (slot >= 1 && slot <= 9) slots[static_cast<size_t>(slot - 1)] = name;
    projectNotice = juce::String::fromUTF8("保存しました"); noticeUntil = juce::Time::getMillisecondCounterHiRes() + 2000.0;
    dirty = true; saveAfter = juce::Time::getMillisecondCounterHiRes(); updateProjectBox(); publishRemote();
    return result;
}
void MainComponent::updateProjectBox()
{
    projectNames = projects.names(); projectBox.clear(juce::dontSendNotification);
    for (int i = 0; i < projectNames.size(); ++i) projectBox.addItem(projectNames[i], i + 1);
    projectBox.setSelectedId(projectNames.indexOf(currentProject) + 1, juce::dontSendNotification);
    projectBox.setText(projectLoading ? juce::String::fromUTF8("読み込み中…") : projectDirty || currentProject.isEmpty() ? juce::String::fromUTF8("（未保存）") : currentProject, juce::dontSendNotification);
    projectBox.setTooltip(currentProject);
    for (auto* button : { &projectNew, &projectSave, &projectSaveAs, &projectDelete, &projectSlots }) button->setEnabled(!projectLoading);
    projectDelete.setEnabled(!projectLoading && currentProject.isNotEmpty());
}
void MainComponent::newProject()
{
    if (!projectReady()) return;
    currentProject.clear(); projectDirty = true; edited();
}
void MainComponent::showSaveProject(bool saveAs)
{
    if (!projectReady()) return;
    const juce::Component::SafePointer<MainComponent> safe(this);
    auto save = [safe](const juce::String& name)
    {
        if (safe == nullptr) return;
        const auto result = safe->saveProjectForRun(name);
        if (result.failed()) juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, juce::String::fromUTF8("保存できません"), result.getErrorMessage());
    };
    if (!saveAs && currentProject.isNotEmpty()) { save(currentProject); return; }
    auto* dialog = new juce::AlertWindow(juce::String::fromUTF8("プロジェクトを保存"), juce::String::fromUTF8("名前を入力してください。"), juce::MessageBoxIconType::NoIcon, this);
    dialog->setLookAndFeel(&lookAndFeel);
    dialog->addTextEditor("name", currentProject, juce::String::fromUTF8("名前"));
    dialog->addButton(juce::String::fromUTF8("保存"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton(juce::String::fromUTF8("キャンセル"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
    dialog->enterModalState(true, juce::ModalCallbackFunction::create([safe, dialog, save](int choice)
    {
        const auto name = dialog->getTextEditorContents("name").trim();
        if (choice != 1 || safe == nullptr) return;
        if (safe->projects.exists(name))
            juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon, juce::String::fromUTF8("上書きしますか？"), name,
                juce::String::fromUTF8("上書き"), juce::String::fromUTF8("キャンセル"), safe,
                juce::ModalCallbackFunction::create([save, name](int ok) { if (ok == 1) save(name); }));
        else save(name);
    }), true);
}
void MainComponent::showProjectSlots()
{
    juce::PopupMenu menuItems; const auto names = projects.names();
    const int stride = names.size() + 1;
    for (int n = 1; n <= 9; ++n)
    {
        juce::PopupMenu sub; sub.addItem((n - 1) * stride + 1, juce::String::fromUTF8("登録を解除"));
        for (int i = 0; i < names.size(); ++i) sub.addItem((n - 1) * stride + i + 2, names[i], true, slots[static_cast<size_t>(n - 1)] == names[i]);
        menuItems.addSubMenu(juce::String(n) + "  " + slots[static_cast<size_t>(n - 1)], sub);
    }
    const juce::Component::SafePointer<MainComponent> safe(this);
    menuItems.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(projectSlots), [safe, names, stride](int choice)
    {
        if (safe == nullptr || choice == 0) return;
        const int n = (choice - 1) / stride + 1, index = (choice - 1) % stride - 1;
        if (n < 1 || n > 9) return;
        safe->slots[static_cast<size_t>(n - 1)] = index >= 0 && index < names.size() ? names[index] : juce::String();
        safe->dirty = true; safe->saveAfter = juce::Time::getMillisecondCounterHiRes(); safe->publishRemote();
    });
}
void MainComponent::confirmDeleteProject()
{
    if (currentProject.isEmpty() || !projectReady()) return;
    const auto name = currentProject; const juce::Component::SafePointer<MainComponent> safe(this);
    juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon, juce::String::fromUTF8("プロジェクトを削除しますか？"),
        name + juce::String::fromUTF8(" を一覧から外します。ファイルは projects/archive に保管します。現在の音は変わりません。"),
        juce::String::fromUTF8("削除"), juce::String::fromUTF8("キャンセル"), this,
        juce::ModalCallbackFunction::create([safe, name](int choice)
        {
            if (choice != 1 || safe == nullptr || !safe->projectReady()) return;
            const auto result = safe->projects.archive(name);
            if (result.failed()) { juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, juce::String::fromUTF8("削除できません"), result.getErrorMessage()); return; }
            for (auto& slot : safe->slots) if (slot == name) slot.clear();
            if (safe->currentProject == name) { safe->currentProject.clear(); safe->projectDirty = true; }
            safe->dirty = true; safe->saveAfter = juce::Time::getMillisecondCounterHiRes(); safe->updateProjectBox(); safe->publishRemote();
        }));
}
juce::var MainComponent::remoteState() const
{
    auto chain = [this](PluginHost::Chain kind)
    {
        juce::Array<juce::var> values; int n = 0;
        for (const auto& item : engine.router.plugins.items(kind))
            values.add(fields({ {"n", ++n}, {"name", item.name.substring(0, 256)}, {"bypass", !item.enabled} }));
        return juce::var(values);
    };
    juce::Array<juce::var> registered;
    int currentSlot = 0;
    for (size_t i = 0; i < slots.size(); ++i)
    {
        const auto& name = slots[i]; const int n = static_cast<int>(i) + 1;
        registered.add(fields({ {"n", n}, {"name", name.isEmpty() ? juce::var() : juce::var(name.substring(0, 256))} }));
        if (name == currentProject && currentSlot == 0) currentSlot = n;
    }
    auto soundState = [this](SoundLibrary::Kind kind)
    {
        juce::Array<juce::var> values; int n = 0;
        for (const auto& item : engine.sounds.items(kind))
            values.add(fields({ {"n", ++n}, {"name", item.name.substring(0, 256)}, {"playing", kind == SoundLibrary::Kind::bgm ? engine.sounds.bgmPlayingId() == item.id : engine.sounds.isSePlaying(item.id)} }));
        return juce::var(values);
    };
    const auto& l = engine.router.loudness;
    const bool learning = l.isLearning();
    return fields({ {"mixerVersion", JUCE_APPLICATION_VERSION_STRING}, {"fxChain", chain(PluginHost::Chain::vocal)}, {"streamFx", chain(PluginHost::Chain::stream)},
        {"projects", fields({ {"slots", registered}, {"current", currentProject.isEmpty() || projectDirty ? juce::var() : fields({ {"n", currentSlot > 0 ? juce::var(currentSlot) : juce::var()}, {"name", currentProject.substring(0, 256)} })},
            {"loading", projectLoading && loadingSlot > 0 ? juce::var(loadingSlot) : juce::var()} })},
        {"bgm", soundState(SoundLibrary::Kind::bgm)}, {"se", soundState(SoundLibrary::Kind::se)},
        {"loud", fields({ {"on", l.enabled.load()}, {"mode", l.mode.load() == Loudness::Mode::cd ? "CD" : "STREAM"}, {"learning", learning},
            {"remainingSeconds", learning ? juce::jlimit(0, 20, static_cast<int>(std::ceil(20.0f - l.learnSeconds()))) : 0}, {"learnedDb", l.learnedDecibels.load()} })} });
}
void MainComponent::publishRemote() { if (!preview && !applyingProject) remote.update(remoteState()); }
juce::var MainComponent::remoteRequest(const juce::var& request)
{
    const auto command = request["command"].toString();
    auto fail = [](const juce::String& code) { return WebSocketServer::failure(code); };
    int n = 0;
    if (command == "toggleFx" || command == "toggleStreamFx" || command == "loadProject" || command == "playBgm" || command == "playSe")
    {
        const auto value = request["n"];
        if (!whole(value) || !numberIn(value, 1, 9007199254740991.0)) return fail("INVALID_ARGUMENT");
        const auto number = static_cast<int64_t>(value);
        if ((command == "toggleFx" || command == "loadProject") && number > 9) return fail("INVALID_ARGUMENT");
        if (number > std::numeric_limits<int>::max()) return fail("EMPTY_SLOT");
        n = static_cast<int>(number);
    }
    if (command == "toggleFx" || command == "toggleStreamFx")
    {
        const auto kind = command == "toggleFx" ? PluginHost::Chain::vocal : PluginHost::Chain::stream;
        const auto list = engine.router.plugins.items(kind);
        if (n > static_cast<int>(list.size())) return fail("EMPTY_SLOT");
        engine.router.plugins.toggle(kind, list[static_cast<size_t>(n - 1)].id);
    }
    else if (command == "loadProject")
    {
        if (!projectReady()) return fail("BUSY");
        const auto& name = slots[static_cast<size_t>(n - 1)];
        if (name.isEmpty()) return fail("EMPTY_SLOT");
        if (beginProjectLoad(name, n).failed()) return fail("LOAD_FAILED");
    }
    else if (command == "playBgm" || command == "playSe")
    {
        const bool bgm = command == "playBgm";
        const auto list = engine.sounds.items(bgm ? SoundLibrary::Kind::bgm : SoundLibrary::Kind::se);
        if (n > static_cast<int>(list.size())) return fail("EMPTY_SLOT");
        const auto id = list[static_cast<size_t>(n - 1)].id;
        const auto error = bgm ? engine.sounds.playBgm(id) : engine.sounds.playSe(id);
        if (error.isNotEmpty()) return fail(error);
    }
    else if (command == "stopSounds")
    {
        const auto kind = request["kind"];
        if (!kind.isString() || (kind.toString() != "bgm" && kind.toString() != "se" && kind.toString() != "all")) return fail("INVALID_ARGUMENT");
        const auto error = engine.sounds.stopSounds(kind.toString());
        if (error.isNotEmpty()) return fail(error);
    }
    else if (command == "loudToggle") { engine.router.loudness.enabled.store(!engine.router.loudness.enabled.load()); edited(); }
    else if (command == "loudMode")
    {
        auto& mode = engine.router.loudness.mode;
        mode.store(mode.load() == Loudness::Mode::stream ? Loudness::Mode::cd : Loudness::Mode::stream); edited();
    }
    else if (command == "loudLearn")
    {
        auto& l = engine.router.loudness;
        if (learnStop.waiting) return fail("BUSY");
        if (l.isLearning())
        {
            l.requestStop(); learnStop.waiting = true; learnStop.started = juce::Time::getMillisecondCounter();
            return fields({ {"pending", true} });
        }
        l.requestLearn(); lastLearning = true;
    }
    else return fail("UNKNOWN_COMMAND");
    publishRemote(); return {};
}
void MainComponent::importSoundForRun(const juce::StringArray& paths, SoundLibrary::Kind kind)
{
    ++pendingRunImports;
    const juce::Component::SafePointer<MainComponent> safe(this);
    sounds.importForRun(paths, kind, [safe]
    {
        if (safe == nullptr) return;
        if (--safe->pendingRunImports <= 0 && !safe->runSoundPlays.isEmpty()) safe->flushRunSoundPlays();
    });
}
void MainComponent::playSoundForRun(SoundLibrary::Kind kind, int number)
{
    runSoundPlays.add({ kind, number });
    if (pendingRunImports == 0) flushRunSoundPlays();
}
void MainComponent::flushRunSoundPlays()
{
    for (const auto& request : runSoundPlays) sounds.playForRun(request.kind, request.number);
    runSoundPlays.clear();
    soundLogActive = true;
    soundLogNext = juce::Time::getMillisecondCounterHiRes() + 1000.0;
}
void MainComponent::showPanel()
{
    if (audioPanel != nullptr) { audioPanel->toFront(true); return; }
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = juce::String::fromUTF8("オーディオ設定");
    options.dialogBackgroundColour = juce::Colour(0xfff7f5ef);
    const juce::Component::SafePointer<MainComponent> safe(this);
    options.content.setOwned(new AudioPanel(engine, [safe] { if (safe != nullptr) safe->edited(); }, [safe] { if (safe != nullptr) safe->showHelp(2, juce::String::fromUTF8("遅れの表示")); }));
    options.componentToCentreAround = this; options.useNativeTitleBar = true; options.resizable = false;
    audioPanel = options.launchAsync();
    if (audioPanel != nullptr) audioPanel->setLookAndFeel(&lookAndFeel);
}
void MainComponent::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (diagnosticMode)
    {
        const double framesPerMs = engine.sampleRate() / 1000.0;
        int frames = std::min(static_cast<int>(engine.sampleRate()), static_cast<int>((now - diagnosticTime) * framesPerMs));
        diagnosticTime += frames / framesPerMs;
        while (frames > 0)
        {
            const int count = std::min(frames, Router::maximumBlock);
            engine.sounds.render(diagnosticBgm.data(), diagnosticSe.data(), count);
            engine.router.processBlock(diagnosticInput.data(), diagnosticInput.data(), diagnosticOutput.data(), count, diagnosticBgm.data(), diagnosticSe.data());
            frames -= count;
        }
    }
    if (soundLogActive)
    {
        // Report the meters fed by BGM/SE playback once per second while a run-triggered sound plays.
        // Sample before the strip meters below drain the peaks for their own display.
        auto accumulate = [](const Meter& meter, float& peak)
        {
            for (float channel : meter.current()) peak = std::max(peak, channel);
        };
        accumulate(engine.router.bgmMeter, soundLogPeakBgm);
        accumulate(engine.router.seMeter, soundLogPeakSe);
        accumulate(engine.router.streamMeter, soundLogPeakStream);
        soundLogPeakSounds = engine.sounds.peakSince(soundLogPeakSounds);
        if (now >= soundLogNext)
        {
            soundLogNext = now + 1000.0;
            const auto level = [](float peak) { return juce::String(peak > 0.0f ? 20.0f * std::log10(peak) : -120.0, 1); };
            juce::Logger::writeToLog("SOUNDLOG bgm=" + level(soundLogPeakBgm) + " dB se=" + level(soundLogPeakSe)
                + " dB stream=" + level(soundLogPeakStream) + " dB sounds=" + level(soundLogPeakSounds)
                + " dB bgmPlaying=" + (engine.sounds.bgmPlayingId() != -1 ? "yes" : "no")
                + " position=" + juce::String(engine.sounds.bgmPositionSeconds(), 4) + " rate=" + juce::String(engine.sampleRate(), 0));
            if (diagnosticMode)
                for (const auto& voice : engine.sounds.seVoiceStates())
                    if (voice.id != 0) juce::Logger::writeToLog("SOUNDLOG se=" + juce::String(voice.id) + " position=" + juce::String(voice.position, 4));
            soundLogPeakBgm = soundLogPeakSe = soundLogPeakStream = soundLogPeakSounds = 0.0f;
        }
    }
    updateTutorial(now);
    if (guidance.numbers) guidance.repaint();
    strips[0]->setMicAvailable(engine.routeAvailable(1));
    for (auto& strip : strips) strip->tick(now / 1000.0);
    const auto nrRevision = engine.router.nr.profileRevision();
    if (nrRevision != lastNRRevision)
    {
        lastNRRevision = nrRevision;
        if (restoringNRProfile) restoringNRProfile = false; else edited();
        juce::Logger::writeToLog("NR profile updated learned=" + juce::String(engine.router.nr.hasProfile() ? 1 : 0));
    }
    loud.refresh();
    const auto loudRevision = engine.router.loudness.revision();
    if (loudRevision != lastLoudRevision)
    {
        lastLoudRevision = loudRevision; edited();
        juce::Logger::writeToLog("LOUD learnedDb=" + juce::String(engine.router.loudness.learnedDecibels.load(), 2));
    }
    latency.setText(engine.latencyText(), juce::dontSendNotification);
    load.setText("CPU " + juce::String(engine.averageLoad(), 1) + "% / max " + juce::String(engine.maximumLoad(), 1) + "%", juce::dontSendNotification);
    const auto ms = engine.roundTripMs();
    latency.setColour(juce::Label::backgroundColourId, !engine.routeAvailable(1) || !engine.routeAvailable(2) ? juce::Colour(0xffdedbd2) : ms <= 12.0 ? juce::Colour(0xffdcebd9) : ms <= 20.0 ? juce::Colour(0xfff4dfbb) : juce::Colour(0xfff0cccc));
    juce::StringArray messages;
    messages.add(engine.statusText().trim());
    if (engine.selection().instMode == "APP") messages.add(engine.appCapture.statusText().trim());
    if (engine.sender.isConnected()) messages.add(juce::String::fromUTF8("OBS 共有メモリ送信中"));
    messages.removeEmptyStrings();
    status.setText(messages.joinIntoString(" / "), juce::dontSendNotification);
    permission.setVisible(!diagnosticMode && !engine.microphoneAllowed());
    auto& learning = engine.router.loudness;
    const bool measuring = learning.isLearning();
    publishRemote();
    if (learnStop.waiting && juce::Time::getMillisecondCounter() - learnStop.started >= 4000)
    {
        learning.cancelLearn(); learnStop.waiting = false;
        publishRemote(); remote.completePending(WebSocketServer::failure("INTERNAL_ERROR"));
    }
    else if (learnStop.waiting && !measuring && !learning.stopPending())
    {
        learnStop.waiting = false;
        remote.completePending(learning.tooQuiet() ? WebSocketServer::failure("TOO_QUIET") : juce::var());
    }
    else if (lastLearning && !measuring && learning.tooQuiet())
    {
        const auto error = WebSocketServer::failure("TOO_QUIET"); remote.error("TOO_QUIET", error["message"].toString());
    }
    lastLearning = learning.isLearning();
    if (now < noticeUntil) status.setText(projectNotice, juce::dontSendNotification);
    if (dirty && now >= saveAfter) saveSettings();
}
void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xffe8e5dd));
    const bool compact = getWidth() < 1200;
    g.setColour(juce::Colour(0xff30342f)); g.setFont(juce::FontOptions(compact ? 16.0f : 20.0f, juce::Font::bold));
    g.drawText("KLINGLAB STREAM MIXER", 20, 10, compact ? 300 : 380, 36, juce::Justification::centredLeft);
}
void MainComponent::resized()
{
    const bool compact = getWidth() < 1200;
    auto header = getLocalBounds().reduced(20, 10).removeFromTop(36);
    menu.setBounds(header.removeFromRight(36)); header.removeFromRight(8);
    panel.setBounds(header.removeFromRight(66)); header.removeFromRight(8);
    load.setBounds(header.removeFromRight(compact ? 130 : 154)); header.removeFromRight(6);
    latency.setBounds(header.removeFromRight(compact ? 250 : 300)); header.removeFromRight(10);
    permission.setBounds(header.removeFromRight(compact ? 112 : 126));
    auto area = getLocalBounds().reduced(20); area.removeFromTop(42);
    status.setBounds(area.removeFromBottom(24)); area.removeFromBottom(5);
    auto upper = area.removeFromTop(306);
    const int leftWidth = juce::roundToInt(static_cast<float>(upper.getWidth()) * 0.68f);
    const auto left = upper.removeFromLeft(leftWidth);
    devices.setBounds(left);
    auto projectRow = left.withTrimmedLeft(86).withTrimmedRight(12).withHeight(28).translated(0, 8);
    projectDelete.setBounds(projectRow.removeFromRight(44)); projectRow.removeFromRight(4);
    projectSaveAs.setBounds(projectRow.removeFromRight(72)); projectRow.removeFromRight(4);
    projectSave.setBounds(projectRow.removeFromRight(44)); projectRow.removeFromRight(4);
    projectNew.setBounds(projectRow.removeFromRight(44)); projectRow.removeFromRight(4);
    projectSlots.setBounds(projectRow.removeFromRight(82)); projectRow.removeFromRight(4);
    projectBox.setBounds(projectRow);
    vocalPlugins.setBounds(left.getX() + 16, left.getY() + 220, left.getWidth() - 32, 35);
    const int loudWidth = 390;
    streamPlugins.setBounds(left.getX() + 16, left.getY() + 260, left.getWidth() - 40 - loudWidth, 35);
    loud.setBounds(left.getRight() - 16 - loudWidth, left.getY() + 260, loudWidth, 35);
    upper.removeFromLeft(12); sounds.setBounds(upper);
    area.removeFromTop(12);
    const int unit = (area.getWidth() - 5 * 8) / 7;
    for (size_t i = 0; i < strips.size(); ++i)
    {
        const int width = i == 2 ? unit * 2 : unit;
        strips[i]->setBounds(area.removeFromLeft(i + 1 == strips.size() ? area.getWidth() : width)); area.removeFromLeft(8);
    }
    layoutGuidance();
}
}
