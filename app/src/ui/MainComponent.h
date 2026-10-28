#pragma once

#include "Panels.h"
#include "PluginPanel.h"
#include "SoundPanel.h"
#include "LoudPanel.h"
#include "HelpPanel.h"
#include "Guidance.h"
#include "../project/Settings.h"
#include "../project/ProjectStore.h"
#include "../project/Hotkeys.h"
#include "../remote/WebSocketServer.h"
#include <array>
#include <memory>
#include <optional>

namespace ksm
{
class MainComponent final : public juce::Component, private juce::Timer
{
public:
    explicit MainComponent(bool previewOnly = false, bool diagnostic = false);
    ~MainComponent() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
    void showStreamDeckHelp();
    void showHelp(int chapter = 1, const juce::String& heading = {});
    void startTutorial();
    void toggleNumbers();
    void showStartupNotice(const juce::String& message)
    {
        projectNotice = message;
        noticeUntil = juce::Time::getMillisecondCounterHiRes() + 30000.0;
        status.setText(message, juce::dontSendNotification);
        status.setTooltip(message);
    }
    juce::Component* streamDeckHelpContent() const { return streamDeckHelp != nullptr ? streamDeckHelp->getContentComponent() : nullptr; }
    void showAppSelection() { devices.showAppSelection(); }
    void showPluginAdd() { vocalPlugins.showAdd(); }
    juce::Component* pluginAddContent() const { return vocalPlugins.addContent(); }
    PluginHost& pluginHost() { return engine.router.plugins; }
    void preservePluginsForRun() { if (!savedPlugins.has_value()) savedPlugins = engine.router.plugins.isRestoring() ? settings.load()["plugins"] : engine.router.plugins.saveState(); }
    Scanner& pluginScanner() { return scanner; }
    juce::Component* appSelectionContent() const { return devices.appSelectionContent(); }
    void importSoundForRun(const juce::StringArray& paths, SoundLibrary::Kind kind);
    void playSoundForRun(SoundLibrary::Kind kind, int number);
    juce::Result selectCaptureForRun(const CaptureSelection& selection)
    {
        if (!savedCapture.has_value()) savedCapture = engine.appCapture.selection();
        return engine.appCapture.select(selection);
    }
    juce::Result saveProjectForRun(const juce::String& name, int slot = 0);
    juce::Result loadProjectForRun(const juce::String& name);
    bool projectReady() const { return !projectLoading && !engine.router.plugins.isRestoring(); }
    const juce::Result& projectLoadResult() const { return lastProjectLoad; }
    void setRunFader(float db) { engine.router.streamInst.setDecibels(db); edited(); }
private:
    juce::LookAndFeel_V4 lookAndFeel;
    AudioEngine engine;
    Settings settings;
    ProjectStore projects { settings.directory() };
    std::unique_ptr<Hotkeys> hotkeys;
    WebSocketServer remote;
    Scanner scanner;
    PluginChainPanel vocalPlugins, streamPlugins;
    DevicePanel devices;
    SoundPanel sounds { engine.sounds };
    LoudPanel loud { engine.router.loudness };
    std::array<std::unique_ptr<MixerStrip>, 6> strips;
    juce::TextButton panel { "PANEL" }, permission { juce::String::fromUTF8("マイクを許可") }, menu { juce::String::fromUTF8("☰") };
    juce::ComboBox projectBox;
    juce::TextButton projectSlots { juce::String::fromUTF8("1〜9登録") }, projectNew { juce::String::fromUTF8("新規") },
        projectSave { juce::String::fromUTF8("保存") }, projectSaveAs { juce::String::fromUTF8("別名保存") }, projectDelete { juce::String::fromUTF8("削除") };
    juce::Label latency, load, status;
    juce::String currentProject, projectNotice;
    juce::StringArray projectNames;
    std::array<juce::String, 9> slots;
    bool projectDirty = false, projectLoading = false, applyingProject = false, diagnosticMode = false;
    int loadingSlot = 0;
    juce::Result lastProjectLoad = juce::Result::ok();
    double noticeUntil = 0.0;
    double diagnosticTime = 0.0;
    std::array<Stereo, Router::maximumBlock> diagnosticInput {}, diagnosticBgm {}, diagnosticSe {};
    std::array<Router::Outputs, Router::maximumBlock> diagnosticOutput {};
    juce::var projectState();
    juce::Result validateProject(const juce::var&) const;
    juce::Result applyProject(const juce::var&);
    juce::Result beginProjectLoad(const juce::String&, int slot = 0);
    void updateProjectBox();
    void showSaveProject(bool saveAs);
    void showProjectSlots();
    void confirmDeleteProject();
    void newProject();
    juce::var remoteState() const;
    juce::var remoteRequest(const juce::var&);
    void publishRemote();
    struct PendingLearn { bool waiting = false; uint32_t started = 0; };
    PendingLearn learnStop;
    bool lastLearning = false;
    TutorialCard tutorial;
    GuidanceOverlay guidance;
    bool tutorialDismissed = false, tutorialStarted = false, tutorialSaved = false;
    bool tutorialInputSeen = false, tutorialStreamSeen = false;
    int tutorialStep = -1;
    double tutorialSince = 0.0;
    void finishTutorial();
    void updateTutorial(double now);
    void layoutGuidance();
    juce::TooltipWindow tooltips { this, 500 };
    juce::Component::SafePointer<juce::DialogWindow> audioPanel, streamDeckHelp;
    std::optional<CaptureSelection> savedCapture;
    std::optional<juce::var> savedPlugins;
    bool dirty = false, preview = false;
    uint32_t lastNRRevision = 0, lastLoudRevision = 0;
    bool restoringNRProfile = false;
    double saveAfter = 0.0;
    bool soundLogActive = false;
    double soundLogNext = 0.0;
    float soundLogPeakBgm = 0.0f, soundLogPeakSe = 0.0f, soundLogPeakStream = 0.0f, soundLogPeakSounds = 0.0f;
    struct RunSoundPlay { SoundLibrary::Kind kind; int number; };
    juce::Array<RunSoundPlay> runSoundPlays;
    int pendingRunImports = 0;
    void flushRunSoundPlays();
    void timerCallback() override;
    void loadSettings();
    void saveSettings();
    void edited();
    void showPanel();
};
}
