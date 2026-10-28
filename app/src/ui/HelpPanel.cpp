#include "HelpPanel.h"
#include <vector>

namespace ksm
{
namespace
{
const juce::Colour ink(0xff30342f), surface(0xfff7f5ef), muted(0xff686c63), border(0xffcfcac0);
const std::array<const char*, 12> files { "01-start.md", "02-devices.md", "03-apps.md", "04-obs.md", "05-mixer.md", "06-nr-fx.md",
    "07-bgm-se.md", "08-loud.md", "09-projects.md", "10-streamdeck.md", "11-voicechat.md", "12-trouble.md" };
const std::array<const char*, 12> names { "はじめに", "入出力", "アプリの音", "OBS", "画面の見かた", "NR とエフェクト",
    "BGM と効果音", "LOUD", "プロジェクト", "Stream Deck", "通話への出力", "困ったとき" };
juce::AttributedString inlineText(const juce::String& source, float size, bool heading = false, bool codeBlock = false)
{
    juce::AttributedString result;
    result.setWordWrap(juce::AttributedString::byChar);
    result.setLineSpacing(5.0f);
    bool bold = heading, code = codeBlock;
    juce::String run;
    auto flush = [&]
    {
        if (run.isEmpty()) return;
        juce::Font font(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain));
        if (code) font = juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), size - 1.0f, juce::Font::plain));
        result.append(run, font, code ? juce::Colour(0xff375d78) : ink); run.clear();
    };
    for (int i = 0; i < source.length(); ++i)
    {
        if (!codeBlock && source.substring(i, i + 2) == "**") { flush(); bold = !bold; ++i; }
        else if (!codeBlock && source[i] == '`') { flush(); code = !code; }
        else run += source[i];
    }
    flush(); return result;
}
}
class HelpPanel::Document final : public juce::Component
{
public:
    void load(const juce::String& markdown)
    {
        blocks.clear();
        auto lines = juce::StringArray::fromLines(markdown);
        bool code = false;
        for (int i = 0; i < lines.size(); ++i)
        {
            auto line = code ? lines[i] : lines[i].trim();
            if (line.trimStart().startsWith("```")) { code = !code; continue; }
            if (line.isEmpty() && !code) continue;
            Block block;
            block.code = code;
            if (!code && line.startsWith("|"))
            {
                block.table = true;
                while (i < lines.size() && lines[i].trim().startsWith("|"))
                {
                    auto row = lines[i].trim().trimCharactersAtStart("|").trimCharactersAtEnd("|");
                    if (row.removeCharacters("| :-").isNotEmpty())
                    {
                        auto cells = juce::StringArray::fromTokens(row, "|", "");
                        cells.trim(); block.rows.push_back(cells);
                    }
                    ++i;
                }
                --i;
            }
            else
            {
                block.heading = !code && line.startsWith("#") ? (line.startsWith("##") ? 2 : 1) : 0;
                if (block.heading > 0) line = line.trimCharactersAtStart("#").trimStart();
                if (!code && line.startsWith("- ")) line = juce::String::fromUTF8("•  ") + line.substring(2);
                block.text = line;
            }
            blocks.push_back(std::move(block));
        }
        layout();
    }
    int headingY(const juce::String& heading) const
    {
        for (const auto& block : blocks) if (block.heading > 0 && block.text.contains(heading)) return block.y;
        return 0;
    }
    void resized() override { if (!layingOut) layout(); }
    void paint(juce::Graphics& g) override
    {
        g.fillAll(surface);
        for (const auto& block : blocks)
        {
            if (block.table)
            {
                int y = block.y;
                for (size_t row = 0; row < block.cells.size(); ++row)
                {
                    const auto height = block.rowHeights[row];
                    g.setColour(row == 0 ? juce::Colour(0xffe5e1d7) : row % 2 == 0 ? juce::Colour(0xffefede5) : surface);
                    g.fillRect(18, y, getWidth() - 36, height);
                    const int count = static_cast<int>(block.cells[row].size());
                    const int width = (getWidth() - 36) / std::max(1, count);
                    for (int col = 0; col < count; ++col)
                    {
                        block.cells[row][static_cast<size_t>(col)].draw(g, {static_cast<float>(26 + col * width), static_cast<float>(y + 8), static_cast<float>(width - 16), static_cast<float>(height - 16)});
                        g.setColour(border); g.drawVerticalLine(18 + col * width, static_cast<float>(y), static_cast<float>(y + height));
                    }
                    g.setColour(border); g.drawHorizontalLine(y + height, 18.0f, static_cast<float>(getWidth() - 18));
                    y += height;
                }
            }
            else
            {
                if (block.code)
                {
                    g.setColour(juce::Colour(0xffe9e6dd)); g.fillRoundedRectangle(18.0f, static_cast<float>(block.y - 3), static_cast<float>(getWidth() - 36), static_cast<float>(block.height + 6), 4.0f);
                }
                block.layout.draw(g, {26.0f, static_cast<float>(block.y), static_cast<float>(getWidth() - 52), static_cast<float>(block.height)});
            }
        }
    }
private:
    struct Block
    {
        juce::String text;
        int heading = 0, y = 0, height = 0;
        bool table = false, code = false;
        juce::TextLayout layout;
        std::vector<juce::StringArray> rows;
        std::vector<std::vector<juce::TextLayout>> cells;
        std::vector<int> rowHeights;
    };
    std::vector<Block> blocks;
    bool layingOut = false;
    void layout()
    {
        layingOut = true;
        int y = 22;
        for (auto& block : blocks)
        {
            if (block.heading == 2) y += 12;
            block.y = y;
            if (block.table)
            {
                block.cells.clear(); block.rowHeights.clear(); block.height = 0;
                for (size_t row = 0; row < block.rows.size(); ++row)
                {
                    std::vector<juce::TextLayout> cells;
                    int height = 34;
                    const auto& values = block.rows[row];
                    const float width = static_cast<float>((getWidth() - 36) / std::max(1, values.size()) - 16);
                    for (const auto& value : values)
                    {
                        juce::TextLayout cell; cell.createLayout(inlineText(value, 14.0f, row == 0), std::max(30.0f, width));
                        height = std::max(height, juce::roundToInt(std::ceil(cell.getHeight())) + 16); cells.push_back(std::move(cell));
                    }
                    block.cells.push_back(std::move(cells)); block.rowHeights.push_back(height); block.height += height;
                }
            }
            else
            {
                block.layout.createLayout(inlineText(block.text, block.heading == 1 ? 24.0f : block.heading == 2 ? 18.0f : 15.0f, block.heading > 0, block.code), static_cast<float>(std::max(80, getWidth() - 52)));
                block.height = juce::roundToInt(std::ceil(block.layout.getHeight()));
            }
            y += block.height + (block.heading > 0 ? 16 : 12);
        }
        setSize(getWidth(), y + 20); layingOut = false; repaint();
    }
};
HelpPanel::HelpPanel() : document(std::make_unique<Document>())
{
    title.setText(juce::String::fromUTF8("使い方"), juce::dontSendNotification); title.setFont(juce::FontOptions(21.0f, juce::Font::bold));
    for (size_t i = 0; i < chapters.size(); ++i)
    {
        auto& button = chapters[i]; button.setButtonText(juce::String(static_cast<int>(i) + 1).paddedLeft('0', 2) + "  " + juce::String::fromUTF8(names[i]));
        button.onClick = [this, i] { openChapter(static_cast<int>(i) + 1); }; addAndMakeVisible(button);
    }
    viewport.setViewedComponent(document.get(), false); viewport.setScrollBarsShown(true, false); viewport.setScrollBarThickness(10);
    for (auto* c : std::array<juce::Component*, 4>{&viewport, &title, &install, &status}) addAndMakeVisible(c);
    status.setFont(juce::FontOptions(12.0f)); status.setColour(juce::Label::textColourId, muted);
    install.setButtonText(juce::String::fromUTF8("Stream Deck プラグインを追加"));
    install.onClick = [this]
    {
        const auto package = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory().getSiblingFile("Resources").getChildFile("site.klinglab.streammixer.streamDeckPlugin");
        const bool opened = package.existsAsFile() && package.startAsProcess();
        status.setText(juce::String::fromUTF8(opened ? "Stream Deck アプリで追加を確認してください。" : "開けませんでした。同梱ファイルと Stream Deck アプリを確認してください。"), juce::dontSendNotification);
    };
    setSize(900, 620); openChapter(1);
}
HelpPanel::~HelpPanel() { viewport.setViewedComponent(nullptr); }
void HelpPanel::openChapter(int chapter, const juce::String& heading)
{
    currentChapter = juce::jlimit(1, 12, chapter);
    const auto file = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory().getSiblingFile("Resources").getChildFile("manual").getChildFile(files[static_cast<size_t>(currentChapter - 1)]);
    document->load(file.existsAsFile() ? file.loadFileAsString() : juce::String::fromUTF8("# ヘルプを開けません\n同梱の manual フォルダを確認してください。"));
    for (size_t i = 0; i < chapters.size(); ++i) chapters[i].setToggleState(static_cast<int>(i) + 1 == currentChapter, juce::dontSendNotification);
    install.setVisible(currentChapter == 10); status.setText({}, juce::dontSendNotification);
    resized(); viewport.setViewPosition(0, heading.isEmpty() ? 0 : document->headingY(heading));
    if (heading.isNotEmpty())
    {
        const juce::Component::SafePointer<HelpPanel> safe(this);
        juce::MessageManager::callAsync([safe, heading]
        {
            if (safe != nullptr) safe->viewport.setViewPosition(0, safe->document->headingY(heading));
        });
    }
    juce::Logger::writeToLog("HELP chapter=" + juce::String(currentChapter) + " heading=" + heading + " bundled=" + juce::String(file.existsAsFile() ? 1 : 0));
}
void HelpPanel::paint(juce::Graphics& g) { g.fillAll(surface); g.setColour(border); g.drawVerticalLine(210, 16.0f, static_cast<float>(getHeight() - 16)); }
void HelpPanel::resized()
{
    title.setBounds(18, 12, 184, 36);
    const int chapterHeight = std::min(39, (getHeight() - 70) / static_cast<int>(chapters.size()));
    for (size_t i = 0; i < chapters.size(); ++i) chapters[i].setBounds(14, 60 + static_cast<int>(i) * chapterHeight, 184, chapterHeight - 6);
    viewport.setBounds(218, 10, std::max(100, getWidth() - 230), getHeight() - (currentChapter == 10 ? 100 : 20));
    document->setSize(viewport.getWidth() - 12, document->getHeight());
    install.setBounds(236, getHeight() - 82, 310, 34); status.setBounds(230, getHeight() - 43, getWidth() - 248, 35);
}
}
