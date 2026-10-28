#include "Panels.h"
#include "Guidance.h"
#include "AppSelectionPanel.h"
#include "../project/Settings.h"

namespace ksm
{
namespace
{
const juce::Colour ink(0xff30342f), muted(0xff686c63), surface(0xfff7f5ef), border(0xffcfcac0);
const std::array<const char*, 6> titles { "INST INPUT", "INST MON", "STREAM", "VOCAL MON", "BGM", "SE" };
const std::array<juce::Colour, 6> accents { juce::Colour(0xffc56a15), juce::Colour(0xff347cba), juce::Colour(0xff398529),
    juce::Colour(0xff915bb4), juce::Colour(0xff00856d), juce::Colour(0xffe078a4) };
void card(juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour(surface); g.fillRoundedRectangle(area.toFloat().reduced(0.5f), 9.0f);
    g.setColour(border); g.drawRoundedRectangle(area.toFloat().reduced(0.5f), 9.0f, 1.0f);
}
void text(juce::Graphics& g, const juce::String& s, juce::Rectangle<int> r, float size = 12.0f,
          juce::Justification justification = juce::Justification::centredLeft)
{
    g.setColour(ink); g.setFont(juce::FontOptions(size)); g.drawText(s, r, justification);
}
juce::String dbText(double db) { return db <= -60.0 ? "-inf" : juce::String(db, 1) + " dB"; }
Meter& stripMeter(int index, Router& r)
{
    switch (index) { case 0: return r.inputMeter; case 1: return r.instMonMeter; case 2: return r.streamMeter;
                     case 3: return r.vocalMeter; case 4: return r.bgmMeter; default: return r.seMeter; }
}
}
PlaceholderPanel::PlaceholderPanel(juce::String heading, juce::StringArray rows, juce::Colour accent)
    : title(std::move(heading)), lines(std::move(rows)), colour(accent) { setName(title); }
void PlaceholderPanel::paint(juce::Graphics& g)
{
    card(g, getLocalBounds());
    auto content = getLocalBounds().reduced(16);
    auto heading = content.removeFromTop(28);
    g.setColour(colour); g.fillEllipse(static_cast<float>(heading.getX()), static_cast<float>(heading.getCentreY() - 4), 8.0f, 8.0f);
    text(g, title, heading.withTrimmedLeft(16), 15.0f);
    for (const auto& line : lines) text(g, line, content.removeFromTop(40).reduced(8, 0), 13.0f);
}

DevicePanel::DevicePanel(AudioEngine& e) : engine(e)
{
    Numbers::id(apps, 110); Numbers::id(mode, 111); Numbers::id(micChannels, 112); Numbers::id(micMon, 113);
    for (size_t i = 0; i < devices.size(); ++i) Numbers::id(devices[i], 120 + static_cast<int>(i));
    apps.onClick = [this] { showAppSelection(); };
    addAndMakeVisible(apps);
    mode.addItem("APP", 1); mode.addItem("CABLE", 2);
    mode.onChange = [this] { apply(); };
    addAndMakeVisible(mode);
    micChannels.addItem("1", 1); micChannels.addItem("2", 2); micChannels.addItem("1+2", 3);
    micChannels.onChange = [this] { apply(); };
    addAndMakeVisible(micChannels);
    for (auto& box : devices)
    {
        box.setTextWhenNothingSelected("(empty)");
        box.onChange = [this] { apply(); };
        addAndMakeVisible(box);
    }
    micMon.setSliderStyle(juce::Slider::LinearHorizontal);
    micMon.setRange(0.0, 2.0, 0.01);
    micMon.setTextBoxStyle(juce::Slider::TextBoxRight, false, 48, 22);
    micMon.setNumDecimalPlacesToDisplay(2);
    micMon.setColour(juce::Slider::textBoxTextColourId, ink);
    micMon.setColour(juce::Slider::textBoxBackgroundColourId, surface);
    micMon.setDoubleClickReturnValue(true, 1.0);
    micMon.setValue(engine.router.micMonGain.load(), juce::dontSendNotification);
    micMon.onValueChange = [this] { engine.router.micMonGain.store(static_cast<float>(micMon.getValue())); if (onEdited) onEdited(); };
    micMon.setTooltip(juce::String::fromUTF8("MIC MON：声だけを MIC MON OUT へ送る倍率（0.00〜2.00）"));
    addAndMakeVisible(micMon);
    refresh();
}
void DevicePanel::refresh()
{
    refreshing = true;
    const auto& s = engine.selection();
    const std::array<juce::String, 5> selected { s.cableUID, s.micUID, s.monitorUID, s.streamUID, s.micMonUID };
    mode.setSelectedId(s.instMode == "CABLE" ? 2 : 1, juce::dontSendNotification);
    micChannels.setSelectedId(s.micChannels, juce::dontSendNotification);
    micMon.setValue(engine.router.micMonGain.load(), juce::dontSendNotification);
    for (size_t row = 0; row < devices.size(); ++row)
    {
        auto& box = devices[row]; box.clear(juce::dontSendNotification); uids[row].clear();
        box.addItem("(empty)", 1); uids[row].add("");
        for (const auto& d : engine.devices())
        {
            if ((row <= 1 ? d.inputs : d.outputs) == 0) continue;
            uids[row].add(d.uid); box.addItem(d.name, uids[row].size());
        }
        int index = uids[row].indexOf(selected[row]);
        if (index < 0 && selected[row].isNotEmpty())
        {
            uids[row].add(selected[row]); box.addItem(juce::String::fromUTF8("未接続: ") + selected[row], uids[row].size()); index = uids[row].size() - 1;
        }
        box.setSelectedId(index + 1, juce::dontSendNotification);
        const bool missing = selected[row].isNotEmpty() && !engine.routeAvailable(static_cast<int>(row));
        box.setColour(juce::ComboBox::outlineColourId, missing ? juce::Colour(0xffbd4747) : border);
        box.setTooltip(missing ? engine.statusText() + juce::String::fromUTF8(" / 接続と権限を確認してください") : "");
    }
    const bool appMode = s.instMode == "APP";
    devices[0].setVisible(!appMode); apps.setVisible(appMode);
    const auto count = engine.appCapture.selection().bundleIDs.size();
    apps.setButtonText(count == 0 ? juce::String::fromUTF8("アプリを選択") : juce::String(count) + juce::String::fromUTF8("アプリを選択中 · 変更"));
    apps.setTooltip(engine.appCapture.statusText());
    refreshing = false;
    repaint();
}
void DevicePanel::showAppSelection()
{
    if (appWindow != nullptr) { appWindow->toFront(true); return; }
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = juce::String::fromUTF8("INST INPUT · アプリ音声");
    options.dialogBackgroundColour = surface;
    const juce::Component::SafePointer<DevicePanel> safe(this);
    options.content.setOwned(new AppSelectionPanel(engine.appCapture, Settings().directory(), [safe]
    {
        if (safe != nullptr) { safe->refresh(); if (safe->onEdited) safe->onEdited(); }
    }));
    options.componentToCentreAround = this; options.useNativeTitleBar = true; options.resizable = false;
    appWindow = options.launchAsync();
    if (appWindow != nullptr) appWindow->setLookAndFeel(&getLookAndFeel());
}
void DevicePanel::closeAppSelection() { if (appWindow != nullptr) delete appWindow.getComponent(); }
juce::Component* DevicePanel::appSelectionContent() const { return appWindow != nullptr ? appWindow->getContentComponent() : nullptr; }
void DevicePanel::apply()
{
    if (refreshing) return;
    auto s = engine.selection();
    s.instMode = mode.getSelectedId() == 2 ? "CABLE" : "APP";
    std::array<juce::String*, 5> targets { &s.cableUID, &s.micUID, &s.monitorUID, &s.streamUID, &s.micMonUID };
    for (size_t row = 0; row < devices.size(); ++row)
    {
        if (row == 0 && engine.selection().instMode == "APP") continue;
        const int index = devices[row].getSelectedId() - 1;
        if (index >= 0 && index < uids[row].size()) *targets[row] = uids[row][index];
    }
    s.micChannels = micChannels.getSelectedId();
    const auto result = engine.selectDevices(s);
    if (result.failed())
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, juce::String::fromUTF8("デバイスを変更できません"), result.getErrorMessage());
    refresh(); if (result.wasOk() && onEdited) onEdited();
}
void DevicePanel::paint(juce::Graphics& g)
{
    card(g, getLocalBounds());
    text(g, "PROJECT", {16, 8, 72, 28});
    const std::array<const char*, 5> names { "INST INPUT", "MIC INPUT", "MONITOR OUT", "STREAM OUT", "MIC MON OUT" };
    for (int row = 0; row < 5; ++row) text(g, names[static_cast<size_t>(row)], {16, 44 + row * 34, 112, 28});
    g.setColour(border); g.drawHorizontalLine(215, 16.0f, static_cast<float>(getWidth() - 16));
    g.drawHorizontalLine(256, 16.0f, static_cast<float>(getWidth() - 16));
}
void DevicePanel::resized()
{
    Numbers::area(*this, 100, {16, 8, 72, 28});
    for (int row = 0; row < 5; ++row)
    {
        Numbers::area(*this, 101 + row, {16, 44 + row * 34, 112, 28});
        auto r = juce::Rectangle<int>(130, 44 + row * 34, getWidth() - 146, 28);
        if (row == 0) { mode.setBounds(r.removeFromLeft(78)); r.removeFromLeft(6); }
        if (row == 1) { micChannels.setBounds(r.removeFromRight(70)); r.removeFromRight(6); }
        if (row == 4) { micMon.setBounds(r.removeFromRight(142)); r.removeFromRight(6); }
        devices[static_cast<size_t>(row)].setBounds(r);
        if (row == 0) apps.setBounds(r);
    }
}

MeterView::MeterView(Meter& m, juce::Colour c) : source(m), colour(c)
{
    setTooltip(juce::String::fromUTF8("L / R · dBFS · 直近1.5秒のピークを赤線で表示。クリックでクリップを解除"));
}
void MeterView::tick(double now)
{
    const auto values = source.take();
    for (size_t c = 0; c < 2; ++c)
    {
        displayed[c] = std::max(values[c], displayed[c] * 0.85f);
        auto& q = history[c];
        while (!q.empty() && q.front().first < now - 1.5) q.pop_front();
        while (!q.empty() && q.back().second <= values[c]) q.pop_back();
        q.emplace_back(now, values[c]);
        held[c] = q.front().second;
    }
    repaint();
}
void MeterView::mouseDown(const juce::MouseEvent&) { source.clearClip(); repaint(); }
void MeterView::paint(juce::Graphics& g)
{
    const int top = 26, bottom = getHeight() - 28, height = bottom - top;
    if (height < 10) return;
    const int barX = 25, barWidth = std::max(5, (getWidth() - 29) / 2);
    auto yAt = [&](float db) { return static_cast<float>(bottom) - std::clamp((db + 60.0f) / 66.0f, 0.0f, 1.0f) * static_cast<float>(height); };
    g.setFont(juce::FontOptions(9.0f));
    for (int db = 0; db >= -60; db -= 6)
    {
        const auto y = yAt(static_cast<float>(db));
        g.setColour(db == 0 ? juce::Colour(0xffb83d3d) : border);
        g.drawHorizontalLine(juce::roundToInt(y), static_cast<float>(barX - 2), static_cast<float>(getWidth()));
        Numbers::area(*this, static_cast<int>(getProperties()["ksmNumber"]) + 10 - db / 6, {0, juce::roundToInt(y) - 6, 22, 12});
        g.setColour(muted); g.drawText(juce::String(db), 0, juce::roundToInt(y) - 6, 22, 12, juce::Justification::centredRight);
    }
    for (size_t c = 0; c < 2; ++c)
    {
        const int x = barX + static_cast<int>(c) * (barWidth + 3);
        const float peak = displayed[c] > 0.0f ? 20.0f * std::log10(displayed[c]) : -100.0f;
        g.setColour(juce::Colour(0xffdedbd2)); g.fillRect(x, top, barWidth, height);
        for (const auto segment : { std::pair{-60.0f, -6.0f}, std::pair{-6.0f, 0.0f}, std::pair{0.0f, 6.0f} })
        {
            const float low = segment.first, high = std::min(peak, segment.second);
            if (high <= low) continue;
            g.setColour(low < -6.0f ? colour : low < 0.0f ? juce::Colour(0xffd4a62f) : juce::Colour(0xffc54848));
            g.fillRect(static_cast<float>(x), yAt(high), static_cast<float>(barWidth), yAt(low) - yAt(high));
        }
        g.setColour(juce::Colour(0xffb83d3d));
        g.drawHorizontalLine(juce::roundToInt(yAt(0.0f)), static_cast<float>(x), static_cast<float>(x + barWidth));
        if (held[c] > 0.0f)
        {
            const float db = 20.0f * std::log10(held[c]);
            g.setColour(juce::Colour(0xffb83d3d)); g.fillRect(static_cast<float>(x), yAt(db), static_cast<float>(barWidth), 1.5f);
        }
        Numbers::area(*this, static_cast<int>(getProperties()["ksmNumber"]) + 22 + static_cast<int>(c), {x, bottom + 2, barWidth, 14});
        text(g, c == 0 ? "L" : "R", {x, bottom + 2, barWidth, 14}, 9.0f, juce::Justification::centred);
    }
    g.setColour(source.isClipped() ? juce::Colour(0xffc54848) : juce::Colour(0xffdedbd2));
    g.fillRoundedRectangle(static_cast<float>(barX), 3.0f, static_cast<float>(getWidth() - barX - 1), 14.0f, 3.0f);
    Numbers::area(*this, static_cast<int>(getProperties()["ksmNumber"]) + 24, {barX, 3, getWidth() - barX - 1, 14});
    Numbers::area(*this, static_cast<int>(getProperties()["ksmNumber"]) + 25, {0, getHeight() - 13, getWidth(), 13});
    text(g, "CLIP", {barX, 3, getWidth() - barX - 1, 14}, 8.0f, juce::Justification::centred);
    const auto highest = std::max(displayed[0], displayed[1]);
    text(g, highest > 0.0f ? juce::String(20.0f * std::log10(highest), 1) : "-inf", {0, getHeight() - 13, getWidth(), 13}, 10.0f, juce::Justification::centred);
}

FaderControl::FaderControl(Fader& f, juce::String name, double maximum, bool isHorizontal)
    : fader(f), title(std::move(name)), horizontal(isHorizontal)
{
    slider.setSliderStyle(horizontal ? juce::Slider::LinearHorizontal : juce::Slider::LinearVertical);
    slider.setRange(-60.0, maximum, 0.1);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setValue(fader.getDecibels(), juce::dontSendNotification);
    slider.setDoubleClickReturnValue(true, 0.0);
    slider.setVelocityModeParameters(0.1, 1, 0.0, true, juce::ModifierKeys::altModifier);
    slider.setTooltip(juce::String::fromUTF8("ダブルクリックで 0.0 dB、⌥ドラッグで微調整。下端は無音"));
    slider.onValueChange = [this] { fader.setDecibels(static_cast<float>(slider.getValue())); repaint(); if (onEdited) onEdited(); };
    addAndMakeVisible(slider);
}
void FaderControl::paint(juce::Graphics& g)
{
    if (horizontal)
    {
        text(g, title, {0, 0, 64, getHeight()}, 10.0f);
        text(g, dbText(slider.getValue()), {getWidth() - 62, 0, 62, getHeight()}, 10.0f, juce::Justification::centredRight);
    }
    else
    {
        text(g, title, {0, 0, getWidth(), 18}, 9.0f, juce::Justification::centred);
        text(g, dbText(slider.getValue()), {0, getHeight() - 21, getWidth(), 21}, getWidth() < 43 ? 8.0f : 10.0f, juce::Justification::centred);
    }
}
void FaderControl::resized()
{
    const int number = static_cast<int>(getProperties()["ksmNumber"]);
    Numbers::id(slider, number + 1);
    Numbers::area(*this, number + 2, horizontal ? juce::Rectangle<int>(0, 0, 64, getHeight()) : juce::Rectangle<int>(0, 0, getWidth(), 18));
    Numbers::area(*this, number + 3, horizontal ? juce::Rectangle<int>(getWidth() - 62, 0, 62, getHeight()) : juce::Rectangle<int>(0, getHeight() - 21, getWidth(), 21));
    slider.setBounds(horizontal ? getLocalBounds().withTrimmedLeft(64).withTrimmedRight(64)
                                : getLocalBounds().withTrimmedTop(20).withTrimmedBottom(23));
}

MixerStrip::MixerStrip(int i, Router& r) : index(i), router(r), accent(accents[static_cast<size_t>(i)]),
    title(titles[static_cast<size_t>(i)]), meter(stripMeter(i, r), accent)
{
    Numbers::id(meter, 1000 + i * 100); meter.getProperties().set("ksmNumberGroup", true);
    Numbers::id(mon, 1040 + i * 100); Numbers::id(minus, 1041 + i * 100); Numbers::id(plus, 1042 + i * 100);
    Numbers::id(delay, 1043 + i * 100); Numbers::id(wet, 1044 + i * 100);
    addAndMakeVisible(meter);
    auto add = [this](std::unique_ptr<FaderControl>& control, Fader& f, const char* label, double maximum, bool horizontal = false)
    {
        control = std::make_unique<FaderControl>(f, label, maximum, horizontal);
        Numbers::id(*control, 1050 + index * 100 + (juce::String(label) == "MASTER" ? 20 : juce::String(label) == "VOCAL" || juce::String(label) == "MON" ? 10 : 0));
        control->getProperties().set("ksmNumberGroup", true);
        control->onEdited = [this] { if (onEdited) onEdited(); };
        addAndMakeVisible(*control);
    };
    if (i == 0)
    {
        nrPanel = std::make_unique<NRPanel>(r.nr);
        nrPanel->onEdited = [this] { if (onEdited) onEdited(); };
        addAndMakeVisible(*nrPanel);
    }
    if (i == 1) add(first, r.instMonitor, "MONITOR", 0);
    if (i == 2)
    {
        add(first, r.streamInst, "INST", 6); add(second, r.streamVocal, "VOCAL", 6); add(master, r.master, "MASTER", 6, true);
        addAndMakeVisible(minus); addAndMakeVisible(plus); addAndMakeVisible(delay);
        delay.setJustificationType(juce::Justification::centred);
        minus.onClick = [this]
        {
            const int minimum = -static_cast<int>(std::floor(router.plugins.latencySamples() * 1000.0 / std::max(1.0, router.plugins.currentSampleRate()) / 5.0)) * 5;
            router.manualDelayMs.store(std::max(minimum, router.manualDelayMs.load() - 5)); if (onEdited) onEdited();
        };
        plus.onClick = [this] { router.manualDelayMs.store(std::min(2000, router.manualDelayMs.load() + 5)); if (onEdited) onEdited(); };
        minus.setTooltip(juce::String::fromUTF8("INST を 5ms 前へ（自動遅延から差し引き、合計は0ms以上）")); plus.setTooltip(juce::String::fromUTF8("INST を 5ms 遅らせる"));
    }
    if (i == 3) add(first, r.vocalMonitor, "MONITOR", 0);
    if (i == 4) { add(first, r.bgmStream, "STREAM", 6); add(second, r.bgmMonitor, "MON", 6); }
    if (i == 5) { add(first, r.seStream, "STREAM", 6); add(second, r.seMonitor, "MON", 6); }
    if (i >= 3)
    {
        addAndMakeVisible(mon); mon.setClickingTogglesState(true);
        mon.setToggleState(i == 3 ? r.vocalMonEnabled.load() : i == 4 ? r.bgmMonEnabled.load() : r.seMonEnabled.load(), juce::dontSendNotification);
        mon.setColour(juce::TextButton::buttonOnColourId, ink);
        mon.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        mon.onClick = [this]
        {
            auto& value = index == 3 ? router.vocalMonEnabled : index == 4 ? router.bgmMonEnabled : router.seMonEnabled;
            value.store(mon.getToggleState()); if (onEdited) onEdited();
        };
    }
    if (i == 3)
    {
        wet.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag); wet.setRange(0, 1, 0.01);
        wet.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 20); wet.setNumDecimalPlacesToDisplay(2);
        wet.setColour(juce::Slider::textBoxTextColourId, ink); wet.setColour(juce::Slider::textBoxBackgroundColourId, surface);
        wet.setValue(r.monWet.load(), juce::dontSendNotification); wet.setEnabled(r.plugins.vocalCount() > 1);
        wet.onValueChange = [this] { router.monWet.store(static_cast<float>(wet.getValue())); if (onEdited) onEdited(); };
        wet.setTooltip(juce::String::fromUTF8("MON WET：FX CHAIN が2つ以上のときに有効になります")); addAndMakeVisible(wet);
    }
}
void MixerStrip::paint(juce::Graphics& g)
{
    card(g, getLocalBounds());
    g.setColour(accent); g.fillEllipse(12.0f, 16.0f, 7.0f, 7.0f);
    text(g, title, {25, 5, getWidth() - 32, 30}, 12.0f);
    if (index == 3) text(g, "MON WET", {12, 65, 66, 24}, 10.0f);
}
void MixerStrip::resized()
{
    Numbers::area(*this, 1030 + index * 100, {25, 5, getWidth() - 32, 30});
    if (index == 3) Numbers::area(*this, 1345, {12, 65, 66, 24});
    auto area = getLocalBounds().reduced(10); area.removeFromTop(28);
    if (index == 2)
    {
        auto row = area.removeFromTop(28); minus.setBounds(row.removeFromLeft(26)); plus.setBounds(row.removeFromRight(26)); delay.setBounds(row);
        area.removeFromTop(4);
        master->setBounds(area.removeFromBottom(32));
    }
    else if (index >= 3)
    {
        mon.setBounds(area.removeFromTop(26));
        if (index == 3) { auto row = area.removeFromTop(32); wet.setBounds(row.withTrimmedLeft(65)); }
        else area.removeFromTop(6);
    }
    if (index == 0 && getHeight() < 330)
    {
        nrPanel->setBounds(area.removeFromRight(std::max(64, area.getWidth() - 62))); area.removeFromRight(4);
        meter.setBounds(area); return;
    }
    if (index == 0) { nrPanel->setBounds(area.removeFromBottom(108)); area.removeFromBottom(4); }
    const int meterWidth = index == 0 ? std::min(104, area.getWidth()) : std::min(74, std::max(49, area.getWidth() / (second != nullptr ? 3 : 2)));
    if (index == 0) { meter.setBounds(area.withSizeKeepingCentre(meterWidth, area.getHeight())); return; }
    meter.setBounds(area.removeFromLeft(meterWidth)); area.removeFromLeft(2);
    if (second != nullptr) { first->setBounds(area.removeFromLeft(area.getWidth() / 2)); second->setBounds(area); }
    else if (first != nullptr) first->setBounds(area);
}
void MixerStrip::tick(double now)
{
    meter.tick(now);
    for (auto* control : { first.get(), second.get(), master.get() }) if (control != nullptr) control->refresh();
    if (index >= 3)
        mon.setToggleState(index == 3 ? router.vocalMonEnabled.load() : index == 4 ? router.bgmMonEnabled.load() : router.seMonEnabled.load(), juce::dontSendNotification);
    if (index == 3 && !wet.isMouseButtonDown()) wet.setValue(router.monWet.load(), juce::dontSendNotification);
    if (nrPanel != nullptr) nrPanel->refresh();
    if (index == 2)
    {
        const int minimum = -static_cast<int>(std::floor(router.plugins.latencySamples() * 1000.0 / router.plugins.currentSampleRate() / 5.0)) * 5;
        if (router.manualDelayMs.load() < minimum) router.manualDelayMs.store(minimum);
        const int manual = router.manualDelayMs.load();
        const double automatic = router.plugins.latencySamples() * 1000.0 / router.plugins.currentSampleRate();
        delay.setText((manual >= 0 ? "+" : "") + juce::String(manual) + ".0 ms", juce::dontSendNotification);
        delay.setTooltip(juce::String::fromUTF8("自動：") + juce::String(automatic, 1) + juce::String::fromUTF8("ms / 合計：") + juce::String(std::max(0.0, automatic + manual), 1) + "ms");
    }
    if (index == 3) wet.setEnabled(router.plugins.vocalCount() > 1);
}
}
