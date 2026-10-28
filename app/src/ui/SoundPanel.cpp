#include "SoundPanel.h"
#include "Guidance.h"

namespace ksm
{
namespace
{
const juce::Colour ink(0xff30342f), muted(0xff686c63), surface(0xfff7f5ef), border(0xffcfcac0);
const juce::Colour bgmColour(0xff00856d), seColour(0xffe078a4);
juce::String lengthText(double seconds)
{
    const auto total = std::max(0, static_cast<int>(std::floor(seconds + 0.5)));
    return juce::String(total / 60) + ":" + juce::String(total % 60).paddedLeft('0', 2);
}
bool knownAudioExtension(const juce::String& file)
{
    const auto extension = juce::File(file).getFileExtension().toLowerCase();
    for (const auto* known : { ".wav", ".aif", ".aiff", ".mp3", ".flac", ".ogg", ".aac", ".m4a", ".caf" })
        if (extension == known) return true;
    return false;
}
void card(juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour(surface); g.fillRoundedRectangle(area.toFloat().reduced(0.5f), 9.0f);
    g.setColour(border); g.drawRoundedRectangle(area.toFloat().reduced(0.5f), 9.0f, 1.0f);
}
void smallButton(juce::TextButton& button, const juce::String& caption)
{
    button.setButtonText(caption);
    button.setColour(juce::TextButton::buttonColourId, surface);
    button.setColour(juce::TextButton::textColourOffId, muted);
    button.setSize(20, 18);
    button.setVisible(false);
}
}

// One BGM row or one SE tile. Click plays/stops, the leading number area drags
// for reordering, the pencil edits the name in place, the cross removes the copy.
class SoundSection::Entry final : public juce::Component, private juce::Label::Listener
{
public:
    Entry(SoundSection& owner, SoundLibrary& library, SoundLibrary::Item item)
        : section(owner), sounds(library), sound(std::move(item))
    {
        const auto prefix = "sound:" + juce::String(sound.id);
        Numbers::dynamicId(*this, prefix + ":play-drag"); Numbers::dynamicId(name, prefix + ":name");
        Numbers::dynamicId(pencil, prefix + ":rename"); Numbers::dynamicId(remove, prefix + ":remove"); Numbers::dynamicId(loop, prefix + ":loop");
        name.setText(sound.name, juce::dontSendNotification);
        name.setInterceptsMouseClicks(false, false);
        name.addListener(this);
        smallButton(pencil, juce::String::fromUTF8("✎"));
        smallButton(remove, "×");
        pencil.onClick = [this] { name.showEditor(); };
        remove.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffad3434));
        remove.onClick = [this] { confirmRemove(); };
        for (auto* b : { &pencil, &remove }) addAndMakeVisible(b);
        if (sound.kind == SoundLibrary::Kind::bgm)
        {
            loop.setButtonText(juce::String::fromUTF8("⟳"));
            loop.setClickingTogglesState(true);
            loop.setToggleState(sound.loop, juce::dontSendNotification);
            loop.setColour(juce::TextButton::buttonColourId, surface);
            loop.setColour(juce::TextButton::buttonOnColourId, bgmColour);
            loop.setColour(juce::TextButton::textColourOffId, muted);
            loop.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
            loop.setTooltip(sound.loop ? juce::String::fromUTF8("くり返し ON：押すと解除") : juce::String::fromUTF8("くり返し OFF：押すと ON"));
            loop.onClick = [this]
            {
                sounds.setLoop(sound.id, loop.getToggleState());
                if (section.onEdited) section.onEdited();
            };
            addAndMakeVisible(loop);
        }
        addAndMakeVisible(name);
        setRepaintsOnMouseActivity(true);
    }
    void paint(juce::Graphics& g) override
    {
        const auto area = getLocalBounds();
        Numbers::area(*this, Numbers::dynamicNumber("sound:" + juce::String(sound.id) + ":number"), {tile ? 12 : 8, 0, 20, area.getHeight()});
        if (!tile) Numbers::area(*this, Numbers::dynamicNumber("sound:" + juce::String(sound.id) + ":length"), {area.getWidth() - 44, 0, 38, area.getHeight()});
        if (playing) g.setColour(sound.kind == SoundLibrary::Kind::bgm ? bgmColour.withAlpha(0.14f) : seColour.withAlpha(0.18f));
        else if (isMouseOver()) g.setColour(juce::Colour(0xffeeebe3));
        else g.setColour(juce::Colour(0xfff2efe8));
        g.fillRoundedRectangle(area.toFloat().reduced(0.5f), 4.0f);
        g.setColour(border); g.drawRoundedRectangle(area.toFloat().reduced(0.5f), 4.0f, 1.0f);
        const auto accent = sound.kind == SoundLibrary::Kind::bgm ? bgmColour : seColour;
        if (playing) { g.setColour(accent); g.fillEllipse(6.0f, 10.0f, 5.0f, 5.0f); }
        g.setColour(ink); g.setFont(juce::FontOptions(11.0f));
        g.drawText(juce::String(number), tile ? 12 : 8, 0, 20, area.getHeight(), juce::Justification::centredLeft);
        if (sound.kind == SoundLibrary::Kind::bgm)
        {
            g.setFont(juce::FontOptions(10.0f));
            g.setColour(muted);
            g.drawText(lengthText(sound.seconds), area.getWidth() - 44, 0, 38, area.getHeight(), juce::Justification::centredRight);
        }
        // Progress bar along the bottom edge.
        const float progress = sound.seconds > 0.0 ? juce::jlimit(0.0f, 1.0f, position / static_cast<float>(sound.seconds)) : 0.0f;
        g.setColour(juce::Colour(0xffdedbd2));
        g.fillRect(2.0f, static_cast<float>(area.getHeight()) - 4.0f, static_cast<float>(area.getWidth()) - 4.0f, 2.0f);
        if (progress > 0.0f)
        {
            g.setColour(accent);
            g.fillRect(2.0f, static_cast<float>(area.getHeight()) - 4.0f, progress * static_cast<float>(area.getWidth() - 4), 2.0f);
        }
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced(2, 1);
        if (tile)
        {
            r.removeFromTop(2);
            remove.setBounds(r.removeFromRight(18));
            pencil.setBounds(r.removeFromRight(18));
            name.setBounds(r.withTrimmedLeft(28));
            return;
        }
        remove.setBounds(r.removeFromRight(20));
        r.removeFromRight(2);
        pencil.setBounds(r.removeFromRight(20));
        r.removeFromRight(2);
        if (loop.isVisible()) { r.removeFromRight(4); loop.setBounds(r.removeFromRight(20).reduced(0, 1)); }
        name.setBounds(r.withTrimmedLeft(30).withTrimmedRight(52));
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isRightButtonDown())
        {
            juce::PopupMenu menu;
            menu.addItem(1, juce::String::fromUTF8("名前を変更"));
            menu.addItem(2, juce::String::fromUTF8("削除"));
            const juce::Component::SafePointer<Entry> safe(this);
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [safe](int choice)
            {
                if (safe == nullptr) return;
                if (choice == 1) safe->name.showEditor();
                if (choice == 2) safe->confirmRemove();
            });
            return;
        }
        if (e.x > dragHandleWidth) togglePlayback();
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (e.getMouseDownX() <= dragHandleWidth && e.getDistanceFromDragStart() > 4 && !dragging)
        {
            dragging = true;
            section.startDragging((sound.kind == SoundLibrary::Kind::bgm ? "bgm:" : "se:") + juce::String(index), this);
        }
    }
    void mouseUp(const juce::MouseEvent&) override { dragging = false; }
    void mouseEnter(const juce::MouseEvent&) override { pencil.setVisible(true); remove.setVisible(true); }
    void mouseExit(const juce::MouseEvent&) override { pencil.setVisible(false); remove.setVisible(false); }
    void setNumber(int value) { number = value; }
    void setIndex(int value) { index = value; }
    void setTile(bool value) { tile = value; }
    int64_t id() const { return sound.id; }
    void tick()
    {
        bool nowPlaying = false;
        float nowPosition = 0.0f;
        if (sound.kind == SoundLibrary::Kind::bgm)
        {
            if (sounds.bgmPlayingId() == sound.id)
            {
                nowPlaying = true;
                nowPosition = sounds.bgmPositionSeconds();
            }
        }
        else
        {
            for (const auto& voice : sounds.seVoiceStates())
                if (voice.id == sound.id)
                {
                    nowPlaying = true;
                    nowPosition = std::max(nowPosition, voice.position);
                }
        }
        const bool changed = nowPlaying != playing || std::abs(nowPosition - position) > 0.005f;
        playing = nowPlaying;
        position = nowPosition;
        if (changed) repaint();
    }
private:
    SoundSection& section;
    SoundLibrary& sounds;
    SoundLibrary::Item sound;
    juce::Label name;
    juce::TextButton loop, pencil, remove;
    bool playing = false, dragging = false, tile = false;
    float position = 0.0f;
    int number = 0, index = 0;
    static constexpr int dragHandleWidth = 24;
    void togglePlayback()
    {
        if (sound.kind == SoundLibrary::Kind::bgm)
        {
            if (sounds.bgmPlayingId() == sound.id) sounds.stopBgm();
            else sounds.playBgm(sound.id);
        }
        else
        {
            if (sounds.isSePlaying(sound.id)) sounds.stopSe(sound.id);
            else sounds.playSe(sound.id);
        }
    }
    void confirmRemove()
    {
        const auto id = sound.id;
        auto& owner = section; // The entry is destroyed while the modal callback runs.
        const juce::Component::SafePointer<Entry> safe(this);
        juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon,
            juce::String::fromUTF8("サウンドを削除"), sound.name + juce::String::fromUTF8(" を一覧から削除します。sounds フォルダのコピーも消えます。"),
            juce::String::fromUTF8("削除"), juce::String::fromUTF8("キャンセル"), nullptr,
            juce::ModalCallbackFunction::create([safe, id, &owner](int answer)
            {
                if (answer != 1 || safe == nullptr) return;
                safe->sounds.remove(id);
                if (owner.onEdited) owner.onEdited();
            }));
    }
    void labelTextChanged(juce::Label*) override
    {
        const auto text = name.getText(true).trim();
        if (text.isNotEmpty() && text != sound.name)
        {
            sounds.rename(sound.id, text);
            sound.name = text;
            if (section.onEdited) section.onEdited();
        }
    }
    void editorShown(juce::Label*, juce::TextEditor& editor) override { editor.setText(sound.name, false); }
};

SoundSection::SoundSection(SoundLibrary& library, SoundLibrary::Kind kind) : library(library), kind(kind)
{
    viewport.setViewedComponent(&content, false);
    viewport.setScrollBarsShown(true, false);
    viewport.setScrollBarThickness(6);
    addAndMakeVisible(viewport);
    addAndMakeVisible(stop);
    stop.onClick = [this]
    {
        // Qualified: the constructor parameters shadow these member names.
        if (this->kind == SoundLibrary::Kind::bgm) this->library.stopBgm();
        else this->library.stopSeAll();
    };
    stop.setTooltip(kind == SoundLibrary::Kind::bgm ? juce::String::fromUTF8("BGM を1秒でフェードして止めます")
                                                    : juce::String::fromUTF8("SE をすべて止めます"));
    Numbers::id(stop, kind == SoundLibrary::Kind::bgm ? 401 : 411);
    rebuild();
}

SoundSection::~SoundSection()
{
    viewport.setViewedComponent(nullptr);
}

void SoundSection::rebuild()
{
    entries.clear();
    const auto items = library.items(kind);
    const bool tiles = kind == SoundLibrary::Kind::se;
    for (size_t i = 0; i < items.size(); ++i)
    {
        auto entry = std::make_unique<Entry>(*this, library, items[i]);
        entry->setNumber(static_cast<int>(i) + 1);
        entry->setIndex(static_cast<int>(i));
        entry->setTile(tiles);
        content.addAndMakeVisible(*entry);
        entries.push_back(std::move(entry));
    }
    resized();
    repaint();
}

void SoundSection::paint(juce::Graphics& g)
{
    card(g, getLocalBounds());
    const auto accent = kind == SoundLibrary::Kind::bgm ? bgmColour : seColour;
    g.setColour(accent); g.fillEllipse(16.0f, 12.0f, 8.0f, 8.0f);
    g.setColour(ink); g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    g.drawText(kind == SoundLibrary::Kind::bgm ? "BGM" : "SE", 30, 2, 60, 26, juce::Justification::centredLeft);
    g.setFont(juce::FontOptions(12.0f));
    g.setColour(muted);
    g.drawText(juce::String(entries.size()) + (kind == SoundLibrary::Kind::bgm ? juce::String::fromUTF8(" 曲") : juce::String::fromUTF8(" 個")),
               72, 2, 60, 26, juce::Justification::centredLeft);
    if (entries.empty())
    {
        g.setFont(juce::FontOptions(11.0f));
        g.drawFittedText(juce::String::fromUTF8("ファイルをここへドロップして追加"), getLocalBounds().withTrimmedTop(34).reduced(18, 8),
            juce::Justification::centredTop, 2);
    }
    if (fileHover)
    {
        g.setColour(accent.withAlpha(0.25f));
        g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(2.0f), 9.0f);
    }
}

void SoundSection::resized()
{
    const int number = kind == SoundLibrary::Kind::bgm ? 400 : 410;
    Numbers::area(*this, number, {30, 2, 42, 26}); Numbers::area(*this, number + 2, {72, 2, 60, 26});
    Numbers::area(*this, number + 3, entries.empty() ? getLocalBounds().withTrimmedTop(34).reduced(18, 8) : juce::Rectangle<int>());
    auto area = getLocalBounds().reduced(8, 6);
    stop.setBounds(area.removeFromTop(20).removeFromRight(52).translated(0, 2));
    area.removeFromTop(4);
    viewport.setBounds(area);
    if (kind == SoundLibrary::Kind::bgm)
    {
        const int rowHeight = 28, count = std::max(1, static_cast<int>(entries.size()));
        const int width = std::max(area.getWidth(), count * 1);
        content.setSize(width, count * rowHeight + 2);
        for (size_t i = 0; i < entries.size(); ++i)
            entries[i]->setBounds(0, static_cast<int>(i) * rowHeight, content.getWidth(), rowHeight - 2);
    }
    else
    {
        const int columns = 3, tileHeight = 44;
        const int rows = std::max(1, (static_cast<int>(entries.size()) + columns - 1) / columns);
        const int tileWidth = (std::max(area.getWidth(), 60) - 2 * 4) / columns;
        content.setSize(std::max(area.getWidth(), 60), rows * (tileHeight + 4) + 2);
        for (size_t i = 0; i < entries.size(); ++i)
        {
            const int column = static_cast<int>(i) % columns, row = static_cast<int>(i) / columns;
            entries[i]->setBounds(column * (tileWidth + 4), row * (tileHeight + 4), tileWidth, tileHeight);
        }
    }
}

bool SoundSection::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& file : files)
        if (knownAudioExtension(file)) return true;
    return false;
}

void SoundSection::fileDragEnter(const juce::StringArray&, int, int) { fileHover = true; repaint(); }
void SoundSection::fileDragExit(const juce::StringArray&) { fileHover = false; repaint(); }

void SoundSection::filesDropped(const juce::StringArray& files, int, int)
{
    fileHover = false;
    repaint();
    juce::StringArray audio;
    for (const auto& file : files)
        if (knownAudioExtension(file)) audio.add(file);
    if (audio.isEmpty()) return;
    import(audio);
}

void SoundSection::import(const juce::StringArray& files, std::function<void()> onFinished)
{
    const juce::Component::SafePointer<SoundSection> safe(this);
    library.addFiles(files, kind, [safe, onFinished](juce::StringArray failures)
    {
        if (safe != nullptr)
        {
            safe->rebuild();
            if (safe->onEdited) safe->onEdited();
        }
        if (!failures.isEmpty())
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                juce::String::fromUTF8("追加できなかったファイル"), failures.joinIntoString("\n"));
        if (onFinished != nullptr) onFinished();
    });
}

bool SoundSection::isInterestedInDragSource(const SourceDetails& details)
{
    return details.description.toString().startsWith(kind == SoundLibrary::Kind::bgm ? "bgm:" : "se:");
}

int SoundSection::dropIndexAt(juce::Point<int> position) const
{
    const auto local = content.getLocalPoint(this, position);
    if (kind == SoundLibrary::Kind::bgm)
        return juce::jlimit(0, static_cast<int>(entries.size()), (local.y + 13) / 28);
    const int columns = 3, tileHeight = 44;
    const int tileWidth = (std::max(content.getWidth(), 60) - 8) / columns;
    const int column = juce::jlimit(0, columns - 1, local.x / std::max(1, tileWidth + 4));
    const int row = std::max(0, local.y / (tileHeight + 4));
    return juce::jlimit(0, static_cast<int>(entries.size()), row * columns + column);
}

void SoundSection::itemDropped(const SourceDetails& details)
{
    const int source = details.description.toString().fromFirstOccurrenceOf(":", false, false).getIntValue();
    const auto items = library.items(kind);
    if (!juce::isPositiveAndBelow(source, static_cast<int>(items.size()))) return;
    library.move(items[static_cast<size_t>(source)].id, dropIndexAt(details.localPosition));
    if (onEdited) onEdited();
    rebuild();
}

void SoundSection::tick()
{
    for (auto& entry : entries)
        if (auto* e = dynamic_cast<Entry*>(entry.get())) e->tick();
}

SoundPanel::SoundPanel(SoundLibrary& library)
    : library(library), bgm(library, SoundLibrary::Kind::bgm), se(library, SoundLibrary::Kind::se)
{
    addAndMakeVisible(bgm);
    addAndMakeVisible(se);
    bgm.onEdited = [this] { if (onEdited) onEdited(); };
    se.onEdited = [this] { if (onEdited) onEdited(); };
    library.onItemsChanged = [this] { bgm.rebuild(); se.rebuild(); };
    startTimerHz(30);
}

SoundPanel::~SoundPanel()
{
    stopTimer();
    library.onItemsChanged = nullptr;
}

void SoundPanel::paint(juce::Graphics&) {}

void SoundPanel::resized()
{
    auto area = getLocalBounds();
    const int bgmHeight = juce::roundToInt(static_cast<float>(area.getHeight()) * 0.54f);
    bgm.setBounds(area.removeFromTop(bgmHeight));
    area.removeFromTop(6);
    se.setBounds(area);
}

void SoundPanel::timerCallback()
{
    bgm.tick();
    se.tick();
}

void SoundPanel::importForRun(const juce::StringArray& paths, SoundLibrary::Kind kind, std::function<void()> onFinished)
{
    (kind == SoundLibrary::Kind::bgm ? bgm : se).import(paths, onFinished);
}

void SoundPanel::playForRun(SoundLibrary::Kind kind, int number)
{
    const auto items = library.items(kind);
    if (!juce::isPositiveAndBelow(number - 1, static_cast<int>(items.size())))
    {
        juce::Logger::writeToLog("SOUND run play not found " + juce::String(number));
        return;
    }
    const auto& item = items[static_cast<size_t>(number - 1)];
    if (kind == SoundLibrary::Kind::bgm) library.playBgm(item.id);
    else library.playSe(item.id);
    juce::Logger::writeToLog("SOUND run play " + juce::String(kind == SoundLibrary::Kind::bgm ? "bgm" : "se")
                             + " #" + juce::String(number) + " " + item.name);
}
}
