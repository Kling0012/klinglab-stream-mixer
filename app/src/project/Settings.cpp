#include "Settings.h"

namespace ksm
{
Settings::Settings()
    : root(juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile("Application Support/Klinglab Stream Mixer"))
{
    const auto overridePath = juce::SystemStats::getEnvironmentVariable("KSM_SETTINGS_DIRECTORY", {});
    if (overridePath.isNotEmpty() && juce::File::isAbsolutePath(overridePath)) root = juce::File(overridePath);
}

juce::Result Settings::createDirectories() const
{
    for (const auto* name : { "projects", "sounds", "logs" })
    {
        const auto result = root.getChildFile(name).createDirectory();
        if (result.failed())
            return result;
    }

    return juce::Result::ok();
}

juce::Result Settings::rotateLogs() const
{
    const auto logs = root.getChildFile("logs");
    const auto oldest = logs.getChildFile("startup.4.log");
    if (oldest.existsAsFile())
    {
        const auto archive = root.getChildFile("archive/logs");
        if (auto result = archive.createDirectory(); result.failed()) return result;
        const auto destination = archive.getNonexistentChildFile(juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S"), ".log", false);
        if (!oldest.moveFileTo(destination)) return juce::Result::fail("Could not archive oldest startup log");
    }
    for (int i = 3; i >= 0; --i)
    {
        const auto source = i == 0 ? logFile() : logs.getChildFile("startup." + juce::String(i) + ".log");
        const auto destination = logs.getChildFile("startup." + juce::String(i + 1) + ".log");
        if (source.existsAsFile() && !source.moveFileTo(destination))
            return juce::Result::fail("Could not rotate startup log");
    }
    return juce::Result::ok();
}

bool Settings::previousRunInterrupted() const
{
    return root.getChildFile("run-state.txt").loadFileAsString().trim() == "running";
}

juce::Result Settings::writeRunState(bool running) const
{
    juce::TemporaryFile temporary(root.getChildFile("run-state.txt"));
    if (!temporary.getFile().replaceWithText(running ? "running\n" : "clean\n")
        || !temporary.overwriteTargetFileWithTemporary())
        return juce::Result::fail("Could not write run state");
    return juce::Result::ok();
}

juce::var Settings::load() const
{
    const auto file = root.getChildFile("settings.json");
    if (!file.existsAsFile()) return juce::var(new juce::DynamicObject());
    const auto value = juce::JSON::parse(file);
    if (value.isObject()) return value;
    juce::Logger::writeToLog("SETTINGS invalid JSON; using defaults");
    return juce::var(new juce::DynamicObject());
}

juce::Result Settings::save(const juce::var& value) const
{
    if (auto result = root.createDirectory(); result.failed()) return result;
    const auto file = root.getChildFile("settings.json");
    juce::TemporaryFile temporary(file);
    if (!temporary.getFile().replaceWithText(juce::JSON::toString(value, false)))
        return juce::Result::fail(juce::String::fromUTF8("設定を書き込めませんでした"));
    if (!temporary.overwriteTargetFileWithTemporary())
        return juce::Result::fail(juce::String::fromUTF8("設定を保存できませんでした"));
    return juce::Result::ok();
}
}
