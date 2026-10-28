#include "ProjectStore.h"

namespace ksm
{
ProjectStore::ProjectStore(juce::File root) : directory(root.getChildFile("projects")) {}
juce::File ProjectStore::fileFor(const juce::String& name) const
{
    if (name.isEmpty() || name.length() > 120 || name != juce::File::createLegalFileName(name) || name == "." || name == "..") return {};
    return directory.getChildFile(name + ".ksmproj");
}
juce::StringArray ProjectStore::names() const
{
    juce::StringArray result;
    for (const auto& file : directory.findChildFiles(juce::File::findFiles, false, "*.ksmproj")) result.add(file.getFileNameWithoutExtension());
    result.sort(true); return result;
}
juce::Result ProjectStore::save(const juce::String& name, const juce::var& data) const
{
    const auto file = fileFor(name);
    if (file == juce::File{}) return juce::Result::fail(juce::String::fromUTF8("名前は120文字以内で、区切り文字を含めずに入力してください。"));
    if (file.existsAsFile() && !juce::JSON::parse(file).isObject())
        return juce::Result::fail(juce::String::fromUTF8("プロジェクトの JSON を読めないため、上書きしません。"));
    if (auto result = directory.createDirectory(); result.failed()) return result;
    juce::TemporaryFile temporary(file);
    if (!temporary.getFile().replaceWithText(juce::JSON::toString(data, false)) || !temporary.overwriteTargetFileWithTemporary())
        return juce::Result::fail(juce::String::fromUTF8("プロジェクトを保存できませんでした。"));
    juce::Logger::writeToLog("PROJECT saved " + name);
    return juce::Result::ok();
}
juce::Result ProjectStore::load(const juce::String& name, juce::var& data) const
{
    const auto file = fileFor(name);
    if (file == juce::File{} || !file.existsAsFile()) return juce::Result::fail(juce::String::fromUTF8("プロジェクトが見つかりません。"));
    data = juce::JSON::parse(file);
    if (!data.isObject() || static_cast<int>(data["version"]) != 1 || !data["devices"].isObject()
        || !data["faders"].isObject() || !data["plugins"].isObject() || !data["appCapture"].isObject()
        || !data["nr"].isObject() || !data["loudness"].isObject())
        return juce::Result::fail(juce::String::fromUTF8("プロジェクトの保存データが不完全です。現在の設定を続けます。"));
    return juce::Result::ok();
}
juce::Result ProjectStore::archive(const juce::String& name) const
{
    const auto file = fileFor(name);
    if (file == juce::File{} || !file.existsAsFile()) return juce::Result::fail(juce::String::fromUTF8("プロジェクトが見つかりません。"));
    const auto archiveDirectory = directory.getChildFile("archive");
    if (auto result = archiveDirectory.createDirectory(); result.failed()) return result;
    const auto target = archiveDirectory.getNonexistentChildFile(name, ".ksmproj");
    if (!file.moveFileTo(target)) return juce::Result::fail(juce::String::fromUTF8("プロジェクトを保管フォルダへ移せませんでした。"));
    return juce::Result::ok();
}
juce::Result ProjectStore::saveCapture(const juce::String& name, const CaptureSelection& selection) const
{
    const auto file = fileFor(name);
    if (file == juce::File{}) return juce::Result::fail(juce::String::fromUTF8("名前は120文字以内で、区切り文字を含めずに入力してください。"));
    juce::var data(new juce::DynamicObject());
    if (file.existsAsFile())
    {
        data = juce::JSON::parse(file);
        if (!data.isObject()) return juce::Result::fail(juce::String::fromUTF8("プロジェクトの JSON を読めないため、上書きしません。"));
    }
    data.getDynamicObject()->setProperty("appCapture", selection.toJson());
    if (auto result = directory.createDirectory(); result.failed()) return result;
    juce::TemporaryFile temporary(file);
    if (!temporary.getFile().replaceWithText(juce::JSON::toString(data, false)) || !temporary.overwriteTargetFileWithTemporary())
        return juce::Result::fail(juce::String::fromUTF8("APP 選択をプロジェクトに保存できませんでした。"));
    juce::Logger::writeToLog("PROJECT saved APP selection " + name);
    return juce::Result::ok();
}
juce::Result ProjectStore::savePlugins(const juce::String& name, const juce::var& plugins) const
{
    const auto file = fileFor(name);
    if (file == juce::File{}) return juce::Result::fail(juce::String::fromUTF8("名前は120文字以内で、区切り文字を含めずに入力してください。"));
    juce::var data(new juce::DynamicObject());
    if (file.existsAsFile())
    {
        data = juce::JSON::parse(file);
        if (!data.isObject()) return juce::Result::fail(juce::String::fromUTF8("プロジェクトの JSON を読めないため、上書きしません。"));
    }
    data.getDynamicObject()->setProperty("plugins", plugins);
    if (auto result = directory.createDirectory(); result.failed()) return result;
    juce::TemporaryFile temporary(file);
    if (!temporary.getFile().replaceWithText(juce::JSON::toString(data, false)) || !temporary.overwriteTargetFileWithTemporary())
        return juce::Result::fail(juce::String::fromUTF8("プラグインをプロジェクトに保存できませんでした。"));
    return juce::Result::ok();
}
juce::Result ProjectStore::loadPlugins(const juce::String& name, juce::var& plugins) const
{
    const auto file = fileFor(name);
    if (file == juce::File{} || !file.existsAsFile()) return juce::Result::fail(juce::String::fromUTF8("プロジェクトが見つかりません。"));
    const auto data = juce::JSON::parse(file);
    if (!data.isObject() || !data["plugins"].isObject()) return juce::Result::fail(juce::String::fromUTF8("このプロジェクトにはプラグインの保存がありません。"));
    plugins = data["plugins"]; return juce::Result::ok();
}
juce::Result ProjectStore::saveNR(const juce::String& name, const juce::var& nr) const
{
    const auto file = fileFor(name);
    if (file == juce::File{}) return juce::Result::fail(juce::String::fromUTF8("名前は120文字以内で、区切り文字を含めずに入力してください。"));
    juce::var data(new juce::DynamicObject());
    if (file.existsAsFile())
    {
        data = juce::JSON::parse(file);
        if (!data.isObject()) return juce::Result::fail(juce::String::fromUTF8("プロジェクトの JSON を読めないため、上書きしません。"));
    }
    data.getDynamicObject()->setProperty("nr", nr);
    if (auto result = directory.createDirectory(); result.failed()) return result;
    juce::TemporaryFile temporary(file);
    if (!temporary.getFile().replaceWithText(juce::JSON::toString(data, false)) || !temporary.overwriteTargetFileWithTemporary())
        return juce::Result::fail(juce::String::fromUTF8("NR をプロジェクトに保存できませんでした。"));
    return juce::Result::ok();
}
juce::Result ProjectStore::loadNR(const juce::String& name, juce::var& nr) const
{
    const auto file = fileFor(name);
    if (file == juce::File{} || !file.existsAsFile()) return juce::Result::fail(juce::String::fromUTF8("プロジェクトが見つかりません。"));
    const auto data = juce::JSON::parse(file);
    if (!data.isObject() || !data["nr"].isObject()) return juce::Result::fail(juce::String::fromUTF8("このプロジェクトには NR の保存がありません。"));
    nr = data["nr"]; return juce::Result::ok();
}
juce::Result ProjectStore::saveLoudness(const juce::String& name, const juce::var& loudness) const
{
    const auto file = fileFor(name);
    if (file == juce::File{}) return juce::Result::fail(juce::String::fromUTF8("名前は120文字以内で、区切り文字を含めずに入力してください。"));
    juce::var data(new juce::DynamicObject());
    if (file.existsAsFile())
    {
        data = juce::JSON::parse(file);
        if (!data.isObject()) return juce::Result::fail(juce::String::fromUTF8("プロジェクトの JSON を読めないため、上書きしません。"));
    }
    data.getDynamicObject()->setProperty("loudness", loudness);
    if (auto result = directory.createDirectory(); result.failed()) return result;
    juce::TemporaryFile temporary(file);
    if (!temporary.getFile().replaceWithText(juce::JSON::toString(data, false)) || !temporary.overwriteTargetFileWithTemporary())
        return juce::Result::fail(juce::String::fromUTF8("LOUD をプロジェクトに保存できませんでした。"));
    return juce::Result::ok();
}
juce::Result ProjectStore::loadLoudness(const juce::String& name, juce::var& loudness) const
{
    const auto file = fileFor(name);
    if (file == juce::File{} || !file.existsAsFile()) return juce::Result::fail(juce::String::fromUTF8("プロジェクトが見つかりません。"));
    const auto data = juce::JSON::parse(file);
    if (!data.isObject() || !data["loudness"].isObject()) return juce::Result::fail(juce::String::fromUTF8("このプロジェクトには LOUD の保存がありません。"));
    loudness = data["loudness"]; return juce::Result::ok();
}
juce::Result ProjectStore::loadCapture(const juce::String& name, CaptureSelection& selection) const
{
    const auto file = fileFor(name);
    if (file == juce::File{} || !file.existsAsFile()) return juce::Result::fail(juce::String::fromUTF8("プロジェクトが見つかりません。"));
    const auto data = juce::JSON::parse(file);
    if (!data.isObject() || !data["appCapture"].isObject()) return juce::Result::fail(juce::String::fromUTF8("このプロジェクトには APP 選択がありません。"));
    selection = CaptureSelection::fromJson(data["appCapture"]);
    juce::Logger::writeToLog("PROJECT loaded APP selection " + name);
    return juce::Result::ok();
}
}
