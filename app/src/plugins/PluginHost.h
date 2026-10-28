#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <memory>
#include <vector>

namespace ksm
{
class PluginHost final : private juce::Timer
{
public:
    enum class Chain { vocal, stream };
    struct Item { juce::String id, name; bool enabled = true; };
    struct Graph;
    class ReadScope
    {
    public:
        explicit ReadScope(PluginHost&) noexcept;
        ~ReadScope();
        Graph* get() const noexcept { return graph; }
    private:
        PluginHost& host;
        Graph* graph;
    };

    PluginHost();
    ~PluginHost() override;
    void prepare(double sampleRate, int maximumBlockSize, bool preserveRestore = false); // Call with device callbacks stopped.
    void release();
    void add(Chain, const juce::PluginDescription&, std::function<void(juce::Result)>);
    void remove(Chain, const juce::String& id);
    void toggle(Chain, const juce::String& id);
    void move(Chain, const juce::String& id, int destination);
    std::vector<Item> items(Chain) const;
    void showEditor(Chain, const juce::String& id);
    void closeEditors();
    bool isRestoring() const noexcept { return restoring; }
    int latencySamples() const noexcept { return latency.load(std::memory_order_relaxed); }
    int vocalCount() const noexcept { return count.load(std::memory_order_relaxed); }
    double currentSampleRate() const noexcept { return rate; }
    juce::var saveState() const;
    void restoreState(const juce::var&, std::function<void(juce::Result)> = {}, bool transactional = false,
                      std::function<juce::Result()> beforeCommit = {},
                      std::function<juce::Result(std::function<juce::Result()>)> transaction = {});
    void processVocal(Graph*, juce::AudioBuffer<float>& vocal, juce::AudioBuffer<float>& monitor, int frames, float wet) noexcept;
    void processStream(Graph*, juce::AudioBuffer<float>&, int frames) noexcept;
    void processInst(Graph*, juce::AudioBuffer<float>&, int frames, int manualMs) noexcept;
    static int graphLatency(const Graph*) noexcept;
    std::function<void()> onChange;
    std::function<void(const juce::String&)> onError;
    juce::AudioPluginFormatManager formats;

private:
    struct Node;
    struct Entry { std::shared_ptr<Node> node; bool enabled = true; };
    std::vector<Entry> vocalNodes, streamNodes;
    std::unique_ptr<Graph> current;
    std::vector<std::unique_ptr<Graph>> retired;
    std::atomic<Graph*> active { nullptr };
    std::atomic<unsigned> readers { 0 };
    std::atomic<int> latency { 0 }, count { 0 };
    double rate = 48000.0;
    int blockSize = 1024;
    uint64_t generation = 0;
    bool restoring = false;
    juce::StringArray reported;
    std::shared_ptr<int> lifetime = std::make_shared<int>(0);
    std::vector<Entry>& nodes(Chain);
    const std::vector<Entry>& nodes(Chain) const;
    void publish();
    void timerCallback() override;
    static juce::Result configure(Node&, double, int);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginHost)
};
}
