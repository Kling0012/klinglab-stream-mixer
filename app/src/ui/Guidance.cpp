#include "Guidance.h"

namespace ksm
{
namespace
{
juce::File obsDirectory()
{
    return juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("Library/Application Support/obs-studio/plugins/klinglab-stream-mixer-source.plugin");
}
}
bool obsPluginInstalled() { return obsDirectory().getChildFile("Contents/MacOS/klinglab-stream-mixer-source").existsAsFile(); }
juce::Result installOBSPlugin()
{
    if (obsPluginInstalled()) return juce::Result::ok();
    const auto destination = obsDirectory();
    if (destination.exists()) return juce::Result::fail(juce::String::fromUTF8("導入先に既存のファイルがあります。設定フォルダと OBS の導入状態を確認してください。"));
    const auto archive = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory().getSiblingFile("Resources").getChildFile("klinglab-stream-mixer-source.zip");
    if (!archive.existsAsFile()) return juce::Result::fail(juce::String::fromUTF8("同梱の OBS プラグインがありません。アプリの配布元を確認してください。"));
    juce::ZipFile zip(archive);
    for (int i = 0; i < zip.getNumEntries(); ++i)
    {
        const auto name = zip.getEntry(i)->filename;
        if (!name.startsWith("klinglab-stream-mixer-source.plugin/") || name.contains("..") || juce::File::isAbsolutePath(name))
            return juce::Result::fail(juce::String::fromUTF8("OBS プラグインの同梱ファイルを確認できませんでした。"));
    }
    auto result = destination.getParentDirectory().createDirectory();
    if (result.failed()) return result;
    result = zip.uncompressTo(destination.getParentDirectory(), false);
    if (result.wasOk() && (!obsPluginInstalled() || !destination.getChildFile("Contents/MacOS/klinglab-stream-mixer-source").setExecutePermission(true)))
        result = juce::Result::fail(juce::String::fromUTF8("OBS プラグインを導入できませんでした。"));
    return result;
}
TutorialCard::TutorialCard()
{
    title.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    instructions.setFont(juce::FontOptions(13.0f)); instructions.setJustificationType(juce::Justification::topLeft);
    state.setFont(juce::FontOptions(12.0f)); state.setJustificationType(juce::Justification::topLeft);
    skip.setButtonText(juce::String::fromUTF8("スキップ")); skip.onClick = [this] { if (onSkip) onSkip(); };
    skipFx.setButtonText(juce::String::fromUTF8("エフェクトを飛ばす")); skipFx.onClick = [this] { if (onSkipFx) onSkipFx(); };
    install.setButtonText(juce::String::fromUTF8("OBS プラグインを導入")); install.onClick = [this] { if (onInstall) onInstall(); };
    help.setButtonText(juce::String::fromUTF8("使い方を開く")); help.onClick = [this] { if (onHelp) onHelp(); };
    int number = 900;
    for (auto* c : std::array<juce::Component*, 7>{&title, &instructions, &state, &skip, &skipFx, &install, &help})
    { addAndMakeVisible(c); Numbers::id(*c, number++); }
}
void TutorialCard::showStep(int step, bool installed, bool pluginAvailable)
{
    const std::array<const char*, 5> titles { "入出力を選ぶ", "アプリの音を選ぶ", "OBS に音を送る", "声のエフェクトを入れる", "プロジェクトを保存する" };
    const std::array<const char*, 5> descriptions {
        "MIC INPUT と MONITOR OUT に、使うマイクとヘッドホンを選んでください。接続を確認すると次へ進みます。",
        "INST INPUT を APP にしてアプリを選び、そのアプリで音を再生してください。入力メーターの動きを確認すると次へ進みます。",
        "OBS に音声ソース「Klinglab Stream Mixer」を1つ追加してください。元のマイクはミュート、デスクトップ音声は無効にします。配信メーターの動きを確認すると次へ進みます。",
        "+ VST から声用のエフェクトを1つ追加してください。実在のプラグインがない場合は、この手順を飛ばせます。",
        "PROJECT 行の「保存」で名前を付けて保存してください。保存が成功すると案内を閉じます。" };
    const auto index = static_cast<size_t>(juce::jlimit(0, 4, step));
    title.setText(juce::String(step + 1) + " / 5  " + juce::String::fromUTF8(titles[index]), juce::dontSendNotification);
    instructions.setText(juce::String::fromUTF8(descriptions[index]), juce::dontSendNotification);
    state.setText(step == 2 ? juce::String::fromUTF8(installed ? "OBS プラグイン：導入済み。OBS 側の受信メーターも確認してください。" : "OBS プラグイン：未導入。下のボタンで導入できます。")
        : step == 3 && !pluginAvailable ? juce::String::fromUTF8("プラグインは未検出です。Scan で検索するか、この手順を飛ばしてください。")
        : juce::String::fromUTF8("操作ができると自動で次の手順へ進みます。"), juce::dontSendNotification);
    install.setVisible(step == 2 && !installed); skipFx.setVisible(step == 3); resized();
}
void TutorialCard::setInstallStatus(const juce::String& message) { state.setText(message, juce::dontSendNotification); }
void TutorialCard::paint(juce::Graphics& g)
{
    g.setColour(juce::Colour(0xfff7f5ef)); g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 9.0f);
    g.setColour(juce::Colour(0xff477aaf)); g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 9.0f, 2.0f);
}
void TutorialCard::resized()
{
    title.setBounds(14, 8, getWidth() - 115, 30); skip.setBounds(getWidth() - 96, 10, 82, 27);
    instructions.setBounds(14, 47, getWidth() - 28, 79); state.setBounds(14, 132, getWidth() - 28, 46);
    help.setBounds(14, getHeight() - 43, 120, 29); install.setBounds(getWidth() - 204, getHeight() - 43, 190, 29);
    skipFx.setBounds(getWidth() - 174, getHeight() - 43, 160, 29);
}
}
