#pragma once

#include <juce_events/juce_events.h>
#include <memory>

namespace ksm
{
// Socket I/O and request handlers run on the message thread, independently of audio.
class WebSocketServer final : private juce::Timer
{
public:
    WebSocketServer();
    ~WebSocketServer() override;
    juce::Result start();
    void stop();
    bool isListening() const noexcept;
    void update(const juce::var& state);
    void error(const juce::String& code, const juce::String& message);
    void completePending(const juce::var& error = {});
    // Empty return value means success; otherwise return { code, message }.
    std::function<juce::var(const juce::var&)> onRequest;
    static juce::var failure(const juce::String& code);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    void timerCallback() override;
};
}
