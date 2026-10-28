#include "ui/MainComponent.h"
#include "project/Settings.h"
#include "dsp/NRRender.h"
#include "dsp/LoudRender.h"
#include <iostream>
#include <thread>

namespace ksm
{
int nativeWindowNumber(juce::Component&);
void postNumberShortcut(juce::Component&);
class MainWindow final : public juce::DocumentWindow
{
public:
    explicit MainWindow(bool previewOnly, bool diagnostic)
        : DocumentWindow("Klinglab Stream Mixer", juce::Colour(0xffe8e5dd),
                         DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new MainComponent(previewOnly, diagnostic), true);
        setResizable(true, false);
        setResizeLimits(1100, 720, 3840, 2160);
        centreWithSize(1280, 800);
        setVisible(true);
        juce::Process::makeForegroundProcess(); toFront(true);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
};

class Application final : public juce::JUCEApplication, private juce::Timer
{
public:
    const juce::String getApplicationName() override { return "Klinglab Stream Mixer"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override
    {
        return juce::JUCEApplication::getCommandLineParameters().contains("--scan-worker")
            || juce::JUCEApplication::getCommandLineParameters().contains("--scan-library")
            || juce::JUCEApplication::getCommandLineParameters().contains("--nr-render")
            || juce::JUCEApplication::getCommandLineParameters().contains("--loud-render");
    }

    void initialise(const juce::String& commandLine) override
    {
        auto arguments = juce::StringArray::fromTokens(commandLine, true);
        for (auto& argument : arguments) argument = argument.unquoted();
        if (arguments.contains("--scan-worker"))
        {
            workerMode = true;
            scanThread = std::thread([this, arguments]
            {
                const int code = Scanner::worker(arguments);
                juce::MessageManager::callAsync([this, code] { setApplicationReturnValue(code); quit(); });
            });
            return;
        }
        if (arguments.contains("--loud-render"))
        {
            workerMode = true;
            setApplicationReturnValue(renderLoud(arguments)); quit(); return;
        }
        if (arguments.contains("--nr-render"))
        {
            workerMode = true;
            setApplicationReturnValue(renderNR(arguments)); quit(); return;
        }
        const auto result = settings.createDirectories();
        if (result.wasOk())
        {
            if (auto rotation = settings.rotateLogs(); rotation.failed())
                std::cerr << "Log rotation unavailable: " << rotation.getErrorMessage() << '\n';
            logger = std::make_unique<juce::FileLogger>(settings.logFile(), "", -1);
            juce::Logger::setCurrentLogger(logger.get());
        }
        else
        {
            std::cerr << "Settings directory unavailable: " << result.getErrorMessage() << '\n';
        }

        log("STARTUP Klinglab Stream Mixer " + getApplicationVersion() + " (M9)");
        if (arguments.contains("--scan-library"))
        {
            headlessScanner = std::make_unique<Scanner>(settings.directory());
            headlessScanner->onFinished = [this]
            {
                setApplicationReturnValue(headlessScanner->failures().isEmpty() ? 0 : 1); quit();
            };
            headlessScanner->start(!arguments.contains("--incremental"), juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("Library/Audio/Plug-Ins/VST3"));
            return;
        }
        log("SETTINGS " + settings.directory().getFullPathName());
        if (result.failed())
            log("SETTINGS ERROR " + result.getErrorMessage());

        const auto& args = arguments;
        for (const auto& arg : args)
        {
            if (arg.startsWith("--quit-after="))
                quitAfterSeconds = juce::jlimit(1, 60, arg.fromFirstOccurrenceOf("=", false, false).getIntValue());
            else if (arg == "--snapshot")
                snapshotRequested = true;
            else if (arg.startsWith("--screencapture="))
                capturePath = arg.fromFirstOccurrenceOf("=", false, false);
            else if (arg.startsWith("--help-chapter="))
                helpChapter = juce::jlimit(1, 12, arg.fromFirstOccurrenceOf("=", false, false).getIntValue());
            else if (arg == "--show-app-selection")
                appSelectionRequested = true;
            else if (arg == "--show-streamdeck-help")
                streamDeckHelpRequested = true;
            else if (arg == "--show-plugin-add")
                pluginAddRequested = true;
            else if (arg.startsWith("--load-fx="))
                runFx.add({PluginHost::Chain::vocal, arg.fromFirstOccurrenceOf("=", false, false)});
            else if (arg.startsWith("--load-stream-fx="))
                runFx.add({PluginHost::Chain::stream, arg.fromFirstOccurrenceOf("=", false, false)});
            else if (arg == "--show-fx-editor")
                showFxEditor = true;
            else if (arg.startsWith("--capture-app="))
                runCapture.bundleIDs.addIfNotAlreadyThere(arg.fromFirstOccurrenceOf("=", false, false));
            else if (arg.startsWith("--import-sound="))
            {
                const auto value = arg.fromFirstOccurrenceOf("=", false, false);
                const auto separator = value.lastIndexOf(":");
                if (separator > 0)
                {
                    const auto tail = value.substring(separator + 1);
                    if (tail == "bgm" || tail == "se")
                        runSoundImports.add({ value.substring(0, separator), tail == "se" });
                }
            }
            else if (arg.startsWith("--play-sound="))
            {
                const auto value = arg.fromFirstOccurrenceOf("=", false, false);
                const auto separator = value.lastIndexOf(":");
                if (separator > 0)
                {
                    const auto tail = value.substring(separator + 1);
                    const auto number = value.substring(0, separator).getIntValue();
                    if ((tail == "bgm" || tail == "se") && number > 0)
                        runSoundPlays.add({ number, tail == "se" });
                }
            }
        }

        previousRunInterrupted = settings.previousRunInterrupted();
        if (result.wasOk())
        {
            const auto state = settings.writeRunState(true);
            trackingRun = state.wasOk();
            if (state.failed()) log("RUN STATE ERROR " + state.getErrorMessage());
        }
        if (previousRunInterrupted) log("RECOVERY previous run did not shut down cleanly; possible crash");
        window = std::make_unique<MainWindow>(arguments.contains("--silent-preview"), arguments.contains("--diagnostic"));
        projectExercise = arguments.contains("--project-exercise");
        quitAfterProjectExercise = projectExercise && quitAfterSeconds == 0;
        if (arguments.contains("--minimum-window")) window->centreWithSize(1100, 720);
        log("WINDOW visible " + juce::String(window->getWidth()) + "x" + juce::String(window->getHeight()) + "; minimum 1100x720");
        if (auto* main = dynamic_cast<MainComponent*>(window->getContentComponent()))
        {
            if (previousRunInterrupted)
                main->showStartupNotice(juce::String::fromUTF8("前回は正常に終了できませんでした。クラッシュした可能性があります。設定フォルダの logs を確認してください。"));
            if (!runCapture.bundleIDs.isEmpty())
            {
                const auto selected = main->selectCaptureForRun(runCapture);
                log(selected.wasOk() ? "TAP run selection applied" : "TAP run selection rejected " + selected.getErrorMessage());
            }
            if (streamDeckHelpRequested) main->showStreamDeckHelp();
            if (helpChapter > 0) main->showHelp(helpChapter, arguments.contains("--help-latency") ? juce::String::fromUTF8("遅れの表示") : juce::String());
            if (arguments.contains("--show-numbers")) main->toggleNumbers();
            if (arguments.contains("--show-tutorial")) main->startTutorial();
            if (appSelectionRequested) main->showAppSelection();
            if (pluginAddRequested) main->showPluginAdd();
            if (!runFx.isEmpty()) main->preservePluginsForRun();
            for (const auto& request : runSoundImports)
            {
                main->importSoundForRun({ request.path }, request.se ? SoundLibrary::Kind::se : SoundLibrary::Kind::bgm);
                log("SOUND run import " + request.path + " as " + (request.se ? "SE" : "BGM"));
            }
            for (const auto& request : runSoundPlays)
                main->playSoundForRun(request.se ? SoundLibrary::Kind::se : SoundLibrary::Kind::bgm, request.number);
        }
        log("WINDOW_ID " + juce::String(nativeWindowNumber(*window)));
        startTime = juce::Time::getMillisecondCounterHiRes();
        if (quitAfterSeconds > 0 || snapshotRequested || !runFx.isEmpty() || projectExercise)
            startTimer(100);
    }

    void shutdown() override
    {
        stopTimer();
        if (scanThread.joinable()) scanThread.join();
        if (workerMode) return;
        headlessScanner.reset();
        window.reset();
        log("SHUTDOWN Klinglab Stream Mixer");
        if (trackingRun)
            if (auto state = settings.writeRunState(false); state.failed()) log("RUN STATE ERROR " + state.getErrorMessage());
        juce::Logger::setCurrentLogger(nullptr);
        logger.reset();
    }

    void systemRequestedQuit() override { quit(); }

    void anotherInstanceStarted(const juce::String&) override
    {
        if (workerMode || headlessScanner != nullptr) return;
        log("INSTANCE existing window brought to front");
        if (window != nullptr)
        {
            window->setVisible(true);
            window->setMinimised(false);
            window->toFront(true);
            juce::Process::makeForegroundProcess();
        }
    }

private:
    static void log(const juce::String& message)
    {
        const auto line = juce::Time::getCurrentTime().toISO8601(true) + " " + message;
        juce::Logger::writeToLog(line);
        std::cout << line << std::endl;
    }

    int helpChapter = 0;
    juce::String capturePath;
    bool numberShortcutPosted = false, numberShortcutRepeated = false;
    void timerCallback() override
    {
        const double elapsed = (juce::Time::getMillisecondCounterHiRes() - startTime) / 1000.0;
        if (!numberShortcutPosted && elapsed >= 0.4 && juce::JUCEApplication::getCommandLineParameters().contains("--number-shortcut"))
        { numberShortcutPosted = true; window->getContentComponent()->grabKeyboardFocus(); postNumberShortcut(*window); }
        if (!numberShortcutRepeated && elapsed >= 1.7 && juce::JUCEApplication::getCommandLineParameters().contains("--number-shortcut-twice"))
        { numberShortcutRepeated = true; window->getContentComponent()->grabKeyboardFocus(); postNumberShortcut(*window); }
        if (capturePath.isNotEmpty() && elapsed >= 1.2)
        {
            juce::Component* target = window.get();
            if (helpChapter > 0 || streamDeckHelpRequested)
                if (auto* main = dynamic_cast<MainComponent*>(window->getContentComponent()))
                    if (auto* help = main->streamDeckHelpContent()) target = help->getTopLevelComponent();
            const auto id = nativeWindowNumber(*target);
            juce::File(capturePath).getParentDirectory().createDirectory();
            juce::ChildProcess capture;
            const bool started = id > 0 && capture.start(juce::StringArray{"/usr/sbin/screencapture", "-x", "-o", "-l", juce::String(id), capturePath});
            const auto captureOutput = started ? capture.readAllProcessOutput() : juce::String("could not start screencapture");
            const bool done = started && capture.waitForProcessToFinish(5000);
            if (captureOutput.isNotEmpty()) log("SCREENCAPTURE output " + captureOutput);
            const bool ok = done && capture.getExitCode() == 0 && juce::File(capturePath).existsAsFile();
            log((ok ? "SCREENCAPTURE " : "SCREENCAPTURE ERROR ") + capturePath + " window=" + juce::String(id));
            if (!ok) setApplicationReturnValue(1);
            capturePath.clear();
        }
        if (!runFx.isEmpty())
            if (auto* main = dynamic_cast<MainComponent*>(window->getContentComponent()); main != nullptr && !main->pluginHost().isRestoring())
            {
                const auto requests = runFx; runFx.clear();
                for (const auto& request : requests)
                {
                    bool found = false;
                    for (const auto& description : main->pluginScanner().plugins())
                        if (description.name.containsIgnoreCase(request.name))
                        {
                            found = true;
                            const juce::Component::SafePointer<MainComponent> safe(main);
                            ++pendingFxAdds;
                            main->pluginHost().add(request.chain, description, [this, safe, chain = request.chain](juce::Result r)
                            {
                                --pendingFxAdds;
                                log(r.wasOk() ? "FX run selection loaded" : "FX run selection failed " + r.getErrorMessage());
                                if (r.wasOk() && showFxEditor && safe != nullptr)
                                {
                                    const auto items = safe->pluginHost().items(chain);
                                    if (!items.empty()) safe->pluginHost().showEditor(chain, items.back().id);
                                }
                            });
                            break;
                        }
                    if (!found) log("FX run selection not found " + request.name);
                }
            }
        if (projectExercise)
            if (auto* main = dynamic_cast<MainComponent*>(window->getContentComponent()); main != nullptr && main->projectReady() && runFx.isEmpty() && pendingFxAdds == 0)
            {
                if (projectStep >= 3 && main->projectLoadResult().failed())
                {
                    log("PROJECT exercise asynchronous load failed " + main->projectLoadResult().getErrorMessage());
                    setApplicationReturnValue(1); projectExercise = false;
                    if (quitAfterProjectExercise) quit();
                    return;
                }
                if (projectStep == 4)
                {
                    log("PROJECT exercise completed"); projectExercise = false;
                    if (quitAfterSeconds == 0 && !snapshotRequested) quit();
                    return;
                }
                juce::Result result = juce::Result::ok();
                if (projectStep == 0) { main->setRunFader(-2.0f); result = main->saveProjectForRun("M7 Talk", 1); }
                if (projectStep == 1) { main->setRunFader(-8.0f); result = main->saveProjectForRun("M7 Song", 2); }
                if (projectStep == 2) result = main->loadProjectForRun("M7 Talk");
                if (projectStep == 3) result = main->loadProjectForRun("M7 Song");
                log("PROJECT exercise step=" + juce::String(projectStep) + (result.wasOk() ? " accepted" : " failed " + result.getErrorMessage()));
                if (result.failed())
                {
                    setApplicationReturnValue(1); projectExercise = false;
                    if (quitAfterProjectExercise) { quit(); return; }
                }
                ++projectStep;
            }
        if (snapshotRequested && elapsed >= 0.7 && !projectExercise)
        {
            snapshotRequested = false;
            auto* content = window->getContentComponent();
            if (appSelectionRequested)
                if (auto* main = dynamic_cast<MainComponent*>(content))
                    if (auto* selection = main->appSelectionContent()) content = selection;
            if (pluginAddRequested)
                if (auto* main = dynamic_cast<MainComponent*>(window->getContentComponent()))
                    if (auto* add = main->pluginAddContent()) content = add;
            if (streamDeckHelpRequested || helpChapter > 0)
                if (auto* main = dynamic_cast<MainComponent*>(window->getContentComponent()))
                    if (auto* help = main->streamDeckHelpContent()) content = help;
            const auto image = content->createComponentSnapshot(content->getLocalBounds(), true, 1.0f);
            const auto path = settings.directory().getChildFile(pluginAddRequested ? "logs/plugin-add-preview.png" : appSelectionRequested ? "logs/app-selection-preview.png" : "logs/window-preview.png");
            auto stream = path.createOutputStream();
            if (stream != nullptr && stream->setPosition(0) && stream->truncate().wasOk()
                && juce::PNGImageFormat().writeImageToStream(image, *stream))
                log("SNAPSHOT " + path.getFullPathName());
            else
                log("SNAPSHOT ERROR could not write preview");
        }

        if (quitAfterSeconds > 0 && elapsed >= static_cast<double>(quitAfterSeconds))
        {
            log("AUTOQUIT after " + juce::String(quitAfterSeconds) + " seconds");
            stopTimer();
            quit();
        }
        else if (quitAfterSeconds == 0 && !snapshotRequested && runFx.isEmpty() && !projectExercise && pendingFxAdds == 0)
        {
            stopTimer();
            if (quitAfterProjectExercise) quit();
        }
    }

    Settings settings;
    std::unique_ptr<juce::FileLogger> logger;
    std::unique_ptr<MainWindow> window;
    int quitAfterSeconds = 0;
    bool snapshotRequested = false, appSelectionRequested = false, pluginAddRequested = false, workerMode = false;
    struct RunFx { PluginHost::Chain chain; juce::String name; };
    juce::Array<RunFx> runFx;
    bool showFxEditor = false, projectExercise = false, streamDeckHelpRequested = false;
    bool quitAfterProjectExercise = false;
    bool trackingRun = false, previousRunInterrupted = false;
    int projectStep = 0, pendingFxAdds = 0;
    struct RunSoundImport { juce::String path; bool se; };
    juce::Array<RunSoundImport> runSoundImports;
    struct RunSoundPlay { int number; bool se; };
    juce::Array<RunSoundPlay> runSoundPlays;
    std::thread scanThread;
    std::unique_ptr<Scanner> headlessScanner;
    CaptureSelection runCapture;
    double startTime = 0.0;
};
}

START_JUCE_APPLICATION(ksm::Application)
