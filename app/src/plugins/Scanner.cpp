#include "Scanner.h"
#include <iostream>

namespace ksm
{
namespace
{
void scanLog(const juce::String& text) { juce::Logger::writeToLog(text); std::cout << text << std::endl; }
juce::AudioPluginFormat* formatFor(juce::AudioPluginFormatManager& manager, const juce::String& name)
{
    for (int i = 0; i < manager.getNumFormats(); ++i) if (manager.getFormat(i)->getName() == name) return manager.getFormat(i);
    return nullptr;
}
bool writeXml(const juce::XmlElement& xml, const juce::File& target)
{
    juce::TemporaryFile temporary(target);
    return xml.writeTo(temporary.getFile()) && temporary.overwriteTargetFileWithTemporary();
}
}
Scanner::Scanner(juce::File directory) : root(std::move(directory))
{
    formats.addFormat(std::make_unique<juce::VST3PluginFormat>());
    formats.addFormat(std::make_unique<juce::AudioUnitPluginFormat>());
    if (auto xml = juce::parseXML(root.getChildFile("plugin-list.xml")))
    {
        known.recreateFromXml(*xml);
        for (auto* e : xml->getChildIterator()) if (e->hasTagName("RETRY")) retryIdentifiers.add(e->getStringAttribute("identifier"));
    }
}
Scanner::~Scanner() { cancel(); }
void Scanner::start(bool full, juce::File folder)
{
    if (running) return;
    rescanAll = full; failed.clear(); pending.clear(); index = 0; running = true; enumerating = true;
    resultFile = root.getChildFile("logs/scan-" + juce::Uuid().toString() + ".xml");
    if (!launch({"--scan-worker", "enumerate", resultFile.getFullPathName(), folder.getFullPathName(), full ? "full" : "incremental"}))
    {
        failed.add(juce::String::fromUTF8("検索プロセスを起動できませんでした。")); finish(); return;
    }
    startTimer(100);
    if (onChange) onChange();
}
bool Scanner::launch(const juce::StringArray& args)
{
    juce::StringArray command { juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName() };
    command.addArray(args); began = juce::Time::getMillisecondCounterHiRes();
    return child.start(command, 0);
}
void Scanner::cancel()
{
    stopTimer();
    if (running && child.isRunning()) child.kill();
    if (running) { scanLog("SCAN cancelled"); running = false; }
}
juce::String Scanner::progress() const
{
    if (!running) return juce::String::fromUTF8("検索待機中 · ") + juce::String(plugins().size()) + juce::String::fromUTF8("件");
    const int seconds = static_cast<int>((juce::Time::getMillisecondCounterHiRes() - began) / 1000.0);
    return (enumerating ? juce::String::fromUTF8("検索対象を列挙中") : juce::String(index) + "/" + juce::String(pending.size()) + "  " + current.identifier)
        + juce::String::fromUTF8("（") + juce::String(seconds) + juce::String::fromUTF8("秒）");
}
void Scanner::save()
{
    auto xml = known.createXml();
    for (const auto& identifier : retryIdentifiers)
        xml->createNewChildElement("RETRY")->setAttribute("identifier", identifier);
    if (!writeXml(*xml, root.getChildFile("plugin-list.xml")))
        failed.add(juce::String::fromUTF8("plugin-list.xml を保存できませんでした。"));
}
void Scanner::finish()
{
    stopTimer(); running = false; save();
    scanLog("SCAN complete plugins=" + juce::String(plugins().size()) + " failures=" + juce::String(failed.size()));
    for (const auto& error : failed) scanLog("SCAN failed " + error);
    if (onChange) onChange(); if (onFinished) onFinished();
}
void Scanner::next()
{
    while (index < pending.size())
    {
        current = pending[index++];
        bool needs = rescanAll || retryIdentifiers.contains(current.identifier);
        if (!needs)
        {
            bool found = false;
            auto* format = formatFor(formats, current.format);
            for (const auto& type : known.getTypes())
                if (type.pluginFormatName == current.format && type.fileOrIdentifier == current.identifier)
                {
                    found = true;
                    if (format == nullptr || format->pluginNeedsRescanning(type)) needs = true;
                }
            if (!found) needs = true;
        }
        if (!needs) continue;
        resultFile = root.getChildFile("logs/scan-" + juce::Uuid().toString() + ".xml");
        scanLog("SCAN file " + current.format + " " + current.identifier);
        retryIdentifiers.addIfNotAlreadyThere(current.identifier); save();
        if (launch({"--scan-worker", "file", resultFile.getFullPathName(), current.format, current.identifier})) return;
        failed.add(current.identifier + juce::String::fromUTF8("：起動失敗"));
    }
    finish();
}
void Scanner::timerCallback()
{
    if (child.isRunning())
    {
        if (juce::Time::getMillisecondCounterHiRes() - began < 300000.0) { if (onChange) onChange(); return; }
        child.kill();
        failed.add((enumerating ? juce::String::fromUTF8("検索対象の列挙") : current.identifier) + juce::String::fromUTF8("：5分で時間切れ"));
        if (enumerating) { finish(); return; }
        next(); return;
    }
    const auto xml = juce::parseXML(resultFile);
    if (child.getExitCode() != 0 || xml == nullptr)
    {
        failed.add((enumerating ? juce::String::fromUTF8("検索対象の列挙") : current.identifier) + juce::String::fromUTF8("：検索失敗・クラッシュ"));
        if (enumerating) { finish(); return; }
    }
    else if (enumerating)
    {
        for (auto* e : xml->getChildIterator()) pending.push_back({e->getStringAttribute("format"), e->getStringAttribute("identifier")});
        enumerating = false;
    }
    else
    {
        juce::KnownPluginList scanned; scanned.recreateFromXml(*xml);
        if (scanned.getNumTypes() == 0) failed.add(current.identifier + juce::String::fromUTF8("：エフェクトを検出できませんでした"));
        else
        {
            retryIdentifiers.removeString(current.identifier);
            for (const auto& old : known.getTypes())
                if (old.pluginFormatName == current.format && old.fileOrIdentifier == current.identifier) known.removeType(old);
            for (const auto& type : scanned.getTypes())
            {
                known.addType(type);
                if (!type.isInstrument && type.numInputChannels > 0 && type.numOutputChannels > 0)
                    scanLog("SCAN found " + type.pluginFormatName + " " + type.name);
            }
            save();
        }
    }
    next();
    if (onChange) onChange();
}
int Scanner::worker(const juce::StringArray& args)
{
    // No application logger, devices, settings, window or single-instance notification in a worker.
    if (args.size() < 5) return 2;
    juce::AudioPluginFormatManager manager;
    manager.addFormat(std::make_unique<juce::VST3PluginFormat>());
    manager.addFormat(std::make_unique<juce::AudioUnitPluginFormat>());
    const juce::File output(args[2]);
    if (args[1] == "enumerate")
    {
        juce::XmlElement result("CANDIDATES");
        const bool full = args[4] == "full";
        for (int i = 0; i < manager.getNumFormats(); ++i)
        {
            auto& format = *manager.getFormat(i);
            auto path = format.getDefaultLocationsToSearch();
            if (!full && format.getName() == "VST3") path = juce::FileSearchPath(args[3]);
            auto identifiers = format.searchPathsForPlugins(path, true, true);
            if (!full && format.getName() == "AudioUnit")
                for (const auto& file : juce::File(args[3]).findChildFiles(juce::File::findDirectories, true, "*.component"))
                    identifiers.addIfNotAlreadyThere(file.getFullPathName());
            for (const auto& identifier : identifiers)
            {
                auto* child = result.createNewChildElement("FILE");
                child->setAttribute("format", format.getName()); child->setAttribute("identifier", identifier);
            }
        }
        return writeXml(result, output) ? 0 : 3;
    }
    if (args[1] == "file")
    {
        auto* format = formatFor(manager, args[3]);
        if (format == nullptr) return 2;
        juce::OwnedArray<juce::PluginDescription> descriptions;
        format->findAllTypesForFile(descriptions, args[4]);
        juce::KnownPluginList result;
        for (const auto* type : descriptions) result.addType(*type);
        return writeXml(*result.createXml(), output) ? 0 : 3;
    }
    return 2;
}
}
