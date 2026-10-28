#pragma once

#include <juce_events/juce_events.h>
#include <memory>

namespace ksm
{
class Hotkeys
{
public:
    Hotkeys();
    ~Hotkeys();
    bool isRegistered() const noexcept;
    juce::String errorText() const;
    std::function<void(int)> onProject, onFx;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
