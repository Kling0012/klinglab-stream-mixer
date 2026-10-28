#include "Hotkeys.h"
#include <Carbon/Carbon.h>
#include <array>

namespace ksm
{
struct Hotkeys::Impl
{
    explicit Impl(Hotkeys& h) : owner(h)
    {
        const EventTypeSpec type { kEventClassKeyboard, kEventHotKeyPressed };
        const auto installed = InstallEventHandler(GetApplicationEventTarget(), handler, 1, &type, this, &eventHandler);
        if (installed != noErr) { failures.add("handler: " + juce::String(installed)); return; }
        const std::array<UInt32, 9> digits { kVK_ANSI_1, kVK_ANSI_2, kVK_ANSI_3, kVK_ANSI_4, kVK_ANSI_5, kVK_ANSI_6, kVK_ANSI_7, kVK_ANSI_8, kVK_ANSI_9 };
        const std::array<UInt32, 9> functions { kVK_F1, kVK_F2, kVK_F3, kVK_F4, kVK_F5, kVK_F6, kVK_F7, kVK_F8, kVK_F9 };
        for (size_t i = 0; i < keys.size(); ++i)
        {
            const auto n = i % 9;
            EventHotKeyID id { signature, static_cast<UInt32>(i + 1) };
            const auto status = RegisterEventHotKey(i < 9 ? digits[n] : functions[n], controlKey | optionKey | shiftKey,
                                                    id, GetApplicationEventTarget(), 0, &keys[i]);
            if (status != noErr) failures.add(juce::String(i < 9 ? "" : "F") + juce::String(n + 1) + ": " + juce::String(status));
        }
        juce::Logger::writeToLog("HOTKEYS registered=" + juce::String(18 - failures.size()) + "/18 " + failures.joinIntoString(", "));
    }
    ~Impl()
    {
        for (auto key : keys) if (key != nullptr) UnregisterEventHotKey(key);
        if (eventHandler != nullptr) RemoveEventHandler(eventHandler);
    }
    static constexpr OSType signature = 0x4b534d37;
    Hotkeys& owner;
    EventHandlerRef eventHandler = nullptr;
    std::array<EventHotKeyRef, 18> keys {};
    juce::StringArray failures;
    static OSStatus handler(EventHandlerCallRef, EventRef event, void* context)
    {
        EventHotKeyID id {};
        if (GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID, nullptr, sizeof(id), nullptr, &id) != noErr || id.signature != signature)
            return eventNotHandledErr;
        auto& owner = static_cast<Impl*>(context)->owner;
        if (id.id >= 1 && id.id <= 9) { if (owner.onProject) owner.onProject(static_cast<int>(id.id)); }
        else if (id.id >= 10 && id.id <= 18) { if (owner.onFx) owner.onFx(static_cast<int>(id.id) - 9); }
        return noErr;
    }
};
Hotkeys::Hotkeys() : impl(std::make_unique<Impl>(*this)) {}
Hotkeys::~Hotkeys() = default;
bool Hotkeys::isRegistered() const noexcept { return impl->failures.isEmpty(); }
juce::String Hotkeys::errorText() const
{
    return isRegistered() ? juce::String() : juce::String::fromUTF8("他のソフトが同じキーを使用中か、キーを登録できませんでした。 ⌃⌥⇧ ") + impl->failures.joinIntoString(", ");
}
}
