#include <juce_gui_basics/juce_gui_basics.h>
#import <AppKit/AppKit.h>

namespace ksm
{
int nativeWindowNumber(juce::Component&);
void postNumberShortcut(juce::Component&);
int nativeWindowNumber(juce::Component& component)
{
    if (auto* peer = component.getPeer())
    {
        auto* view = static_cast<NSView*>(peer->getNativeHandle());
        if (view != nil && [view window] != nil) return static_cast<int>([[view window] windowNumber]);
    }
    return 0;
}
void postNumberShortcut(juce::Component& component)
{
    const auto number = nativeWindowNumber(component);
    auto* event = [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint
        modifierFlags:NSEventModifierFlagOption timestamp:[[NSProcessInfo processInfo] systemUptime] windowNumber:number
        context:nil characters:@"n" charactersIgnoringModifiers:@"n" isARepeat:NO keyCode:45];
    [NSApp postEvent:event atStart:NO];
    auto* release = [NSEvent keyEventWithType:NSEventTypeKeyUp location:NSZeroPoint
        modifierFlags:NSEventModifierFlagOption timestamp:[[NSProcessInfo processInfo] systemUptime] windowNumber:number
        context:nil characters:@"n" charactersIgnoringModifiers:@"n" isARepeat:NO keyCode:45];
    [NSApp postEvent:release atStart:NO];
}
}
