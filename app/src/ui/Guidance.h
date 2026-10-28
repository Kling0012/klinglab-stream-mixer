#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <map>

namespace ksm
{
namespace Numbers
{
inline void id(juce::Component& component, int number) { component.getProperties().set("ksmNumber", number); }
inline int dynamicNumber(const juce::String& key)
{
    static std::map<juce::String, int> assigned;
    static std::map<int, juce::String> occupied;
    if (const auto found = assigned.find(key); found != assigned.end()) return found->second;
    int number = static_cast<int>(10000 + static_cast<uint64_t>(key.hashCode64()) % 990000);
    while (occupied.contains(number)) number = number == 999999 ? 10000 : number + 1;
    assigned.emplace(key, number); occupied.emplace(number, key);
    return number;
}
inline void dynamicId(juce::Component& component, const juce::String& key)
{
    component.getProperties().set("ksmNumber", dynamicNumber(key));
}
inline void area(juce::Component& component, int number, juce::Rectangle<int> bounds)
{
    auto& properties = component.getProperties();
    auto data = properties["ksmTextNumbers"];
    if (!data.isObject()) data = juce::var(new juce::DynamicObject());
    juce::Array<juce::var> rectangle {bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight()};
    data.getDynamicObject()->setProperty(juce::Identifier("n" + juce::String(number)), rectangle);
    properties.set("ksmTextNumbers", data);
}
inline void badge(juce::Graphics& g, int number, juce::Rectangle<int> target, juce::Rectangle<int> clip)
{
    if (!target.intersects(clip)) return;
    const auto caption = juce::String(number);
    const int width = 7 + caption.length() * 5;
    auto r = juce::Rectangle<int>(target.getX(), target.getY(), width, 12).constrainedWithin(clip);
    g.setColour(juce::Colour(0xff30342f)); g.fillRoundedRectangle(r.toFloat(), 2.0f);
    g.setColour(juce::Colour(0xffffef87)); g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
    g.drawText(caption, r, juce::Justification::centred);
}
inline void draw(juce::Graphics& g, juce::Component& root, juce::Component& node, juce::Rectangle<int> clip)
{
    if (!node.isVisible()) return;
    const auto bounds = root.getLocalArea(&node, node.getLocalBounds());
    clip = clip.getIntersection(bounds); if (clip.isEmpty()) return;
    const auto& properties = node.getProperties();
    const int number = static_cast<int>(properties["ksmNumber"]);
    if (number > 0 && !static_cast<bool>(properties["ksmNumberGroup"])) badge(g, number, bounds, clip);
    if (auto* texts = properties["ksmTextNumbers"].getDynamicObject())
        for (const auto& entry : texts->getProperties())
            if (auto* r = entry.value.getArray(); r != nullptr && r->size() == 4)
                badge(g, entry.name.toString().substring(1).getIntValue(), root.getLocalArea(&node, juce::Rectangle<int>(static_cast<int>((*r)[0]), static_cast<int>((*r)[1]), static_cast<int>((*r)[2]), static_cast<int>((*r)[3]))), clip);
    for (auto* child : node.getChildren()) draw(g, root, *child, clip);
}
}
class GuidanceOverlay final : public juce::Component
{
public:
    GuidanceOverlay() { setInterceptsMouseClicks(false, false); }
    bool numbers = false;
    juce::Array<juce::Rectangle<int>> highlights;
    void paint(juce::Graphics& g) override
    {
        g.setColour(juce::Colour(0xff477aaf));
        for (auto r : highlights) g.drawRoundedRectangle(r.toFloat().expanded(3.0f), 5.0f, 3.0f);
        if (numbers && getParentComponent() != nullptr)
            Numbers::draw(g, *this, *getParentComponent(), getLocalBounds());
    }
};
class TutorialCard final : public juce::Component
{
public:
    TutorialCard();
    void showStep(int step, bool installed, bool pluginAvailable);
    void setInstallStatus(const juce::String&);
    void paint(juce::Graphics&) override;
    void resized() override;
    std::function<void()> onSkip, onSkipFx, onInstall, onHelp;
private:
    juce::Label title, instructions, state;
    juce::TextButton skip, skipFx, install, help;
};
bool obsPluginInstalled();
juce::Result installOBSPlugin();
}
