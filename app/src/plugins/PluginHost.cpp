#include "PluginHost.h"
#include <iostream>
#include <stdexcept>

namespace ksm
{
namespace
{
void pluginLog(const juce::String& text)
{
    juce::Logger::writeToLog(text);
    std::cout << text << std::endl;
}
class EditorWindow final : public juce::DocumentWindow
{
public:
    explicit EditorWindow(juce::AudioPluginInstance& instance)
        : DocumentWindow(instance.getName(), juce::Colour(0xfff7f5ef), closeButton)
    {
        setUsingNativeTitleBar(true);
        auto* editor = instance.createEditorAndMakeActive();
        if (editor == nullptr) editor = new juce::GenericAudioProcessorEditor(instance);
        setContentOwned(editor, true);
        setResizable(editor->isResizable(), false);
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
    }
    void closeButtonPressed() override { setVisible(false); }
};
struct Delay
{
    std::vector<std::array<float, 2>> data;
    size_t position = 0;
    explicit Delay(int samples = 0) : data(static_cast<size_t>(samples)) {}
    void process(juce::AudioBuffer<float>& buffer, int frames) noexcept
    {
        if (data.empty()) return;
        for (int f = 0; f < frames; ++f)
        {
            const auto input = std::array { buffer.getSample(0, f), buffer.getSample(1, f) };
            buffer.setSample(0, f, data[position][0]); buffer.setSample(1, f, data[position][1]);
            data[position] = input;
            if (++position == data.size()) position = 0;
        }
    }
};
void copy(juce::AudioBuffer<float>& to, const juce::AudioBuffer<float>& from, int frames) noexcept
{
    for (int c = 0; c < 2; ++c) to.copyFrom(c, 0, from, c, 0, frames);
}
}
struct PluginHost::Node : juce::AudioProcessorListener
{
    juce::String id = juce::Uuid().toString();
    juce::PluginDescription description;
    std::unique_ptr<juce::AudioPluginInstance> instance;
    std::unique_ptr<EditorWindow> editor;
    std::atomic<bool> failed { false }, editing { false };
    std::atomic<unsigned> users { 0 };
    std::atomic<int> reportedLatency { 0 };
    std::atomic<bool> stateChanged { false };
    std::atomic<uint64_t> blocks { 0 };
    bool listening = false;
    juce::MemoryBlock savedState;
    void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override { stateChanged.store(true, std::memory_order_relaxed); }
    void audioProcessorChanged(juce::AudioProcessor* processor, const ChangeDetails& details) override
    {
        if (details.latencyChanged) reportedLatency.store(juce::jlimit(0, 960000, processor->getLatencySamples()), std::memory_order_relaxed);
        stateChanged.store(true, std::memory_order_relaxed);
    }
    ~Node() override
    {
        editor.reset();
        if (instance != nullptr)
        {
            if (listening) instance->removeListener(this);
            instance->releaseResources();
        }
    }
    // The callback skips this node while state is accessed; only the message thread waits.
    void beginEdit()
    {
        editing.store(true);
        while (users.load() != 0) juce::Thread::yield();
    }
    void endEdit() { editing.store(false); }
};
struct PluginHost::Graph
{
    struct Stage
    {
        std::shared_ptr<Node> node;
        bool enabled;
        int latency;
        std::shared_ptr<Delay> bypass;
        Stage(const Entry& entry) : node(entry.node), enabled(entry.enabled),
            latency(node->reportedLatency.load(std::memory_order_relaxed)), bypass(std::make_shared<Delay>(latency)) {}
    };
    std::vector<Stage> vocal, stream;
    juce::AudioBuffer<float> lastInput, dry;
    std::shared_ptr<Delay> lastDelay;
    juce::MidiBuffer midi;
    int vocalLatency = 0, capacity;
    double sampleRate;
    struct InstDelay
    {
        std::vector<std::array<float, 2>> data;
        size_t position = 0;
        double manualSamples = 0.0;
        explicit InstDelay(size_t size) : data(size) {}
    };
    std::shared_ptr<InstDelay> instDelay;
    Graph(const std::vector<Entry>& v, const std::vector<Entry>& s, int block, double hz, const Graph* previous)
        : lastInput(2, block), dry(2, block), capacity(block), sampleRate(hz)
    {
        for (const auto& e : v) { vocal.emplace_back(e); vocalLatency += vocal.back().latency; }
        for (const auto& e : s) stream.emplace_back(e);
        if (previous != nullptr && juce::approximatelyEqual(previous->sampleRate, hz))
        {
            for (auto pair : { std::pair{&vocal, &previous->vocal}, std::pair{&stream, &previous->stream} })
                for (auto& stage : *pair.first)
                    for (const auto& old : *pair.second)
                        if (stage.node == old.node && stage.latency == old.latency) stage.bypass = old.bypass;
            if (previous->vocalLatency == vocalLatency) instDelay = previous->instDelay;
            if (!vocal.empty() && !previous->vocal.empty() && vocal.back().node == previous->vocal.back().node
                && vocal.back().latency == previous->vocal.back().latency) lastDelay = previous->lastDelay;
        }
        if (!vocal.empty() && !lastDelay) lastDelay = std::make_shared<Delay>(vocal.back().latency);
        if (!instDelay) instDelay = std::make_shared<InstDelay>(static_cast<size_t>(vocalLatency + hz * 2.0 + 2));
        midi.ensureSize(65536);
    }
    void process(Stage& stage, juce::AudioBuffer<float>& buffer, int frames) noexcept
    {
        auto& node = *stage.node;
        // Dry compensation is always advanced, so bypass changes preserve latency.
        copy(dry, buffer, frames);
        stage.bypass->process(dry, frames);
        node.users.fetch_add(1);
        const bool run = stage.enabled && !node.failed.load() && !node.editing.load();
        if (run)
        {
            try
            {
                juce::AudioBuffer<float> view(buffer.getArrayOfWritePointers(), 2, frames);
                midi.clear();
                node.instance->processBlock(view, midi);
                node.blocks.fetch_add(1, std::memory_order_relaxed);
            }
            catch (...) { node.failed.store(true); copy(buffer, dry, frames); }
        }
        else copy(buffer, dry, frames);
        node.users.fetch_sub(1);
    }
};
PluginHost::ReadScope::ReadScope(PluginHost& h) noexcept : host(h)
{
    host.readers.fetch_add(1);
    graph = host.active.load();
}
PluginHost::ReadScope::~ReadScope() { host.readers.fetch_sub(1); }
PluginHost::PluginHost()
{
    formats.addFormat(std::make_unique<juce::VST3PluginFormat>());
    formats.addFormat(std::make_unique<juce::AudioUnitPluginFormat>());
    publish(); startTimer(250);
}
PluginHost::~PluginHost()
{
    stopTimer(); lifetime.reset(); active.store(nullptr);
    jassert(readers.load() == 0);
    closeEditors(); current.reset(); retired.clear();
}
std::vector<PluginHost::Entry>& PluginHost::nodes(Chain c) { return c == Chain::vocal ? vocalNodes : streamNodes; }
const std::vector<PluginHost::Entry>& PluginHost::nodes(Chain c) const { return c == Chain::vocal ? vocalNodes : streamNodes; }
juce::Result PluginHost::configure(Node& node, double hz, int block)
{
    auto& p = *node.instance;
    if (p.getBusCount(true) < 1 || p.getBusCount(false) < 1 || p.getPluginDescription().isInstrument)
        return juce::Result::fail(juce::String::fromUTF8("音声エフェクトを選んでください。"));
    p.releaseResources();
    for (bool input : { true, false })
        for (int b = 1; b < p.getBusCount(input); ++b) p.getBus(input, b)->enable(false);
    auto layout = p.getBusesLayout();
    layout.inputBuses.getReference(0) = juce::AudioChannelSet::stereo();
    layout.outputBuses.getReference(0) = juce::AudioChannelSet::stereo();
    if (!p.setBusesLayout(layout))
        return juce::Result::fail(juce::String::fromUTF8("ステレオ入出力に対応していません。"));
    p.setRateAndBufferSizeDetails(hz, block);
    p.prepareToPlay(hz, block);
    if (p.getLatencySamples() > static_cast<int>(hz * 10.0))
        return juce::Result::fail(juce::String::fromUTF8("遅延が10秒を超えるため、このエフェクトを使えません。"));
    node.reportedLatency.store(std::max(0, p.getLatencySamples()));
    if (!node.listening) { p.addListener(&node); node.listening = true; }
    node.failed.store(false);
    return juce::Result::ok();
}
void PluginHost::release()
{
    jassert(readers.load() == 0);
    for (auto* list : { &vocalNodes, &streamNodes }) for (auto& e : *list) e.node->instance->releaseResources();
}
void PluginHost::prepare(double hz, int block, bool preserveRestore)
{
    rate = hz; blockSize = std::max(32, block);
    if (!preserveRestore) ++generation;
    for (auto* list : { &vocalNodes, &streamNodes })
        for (auto& e : *list)
        {
            try
            {
                auto result = configure(*e.node, rate, blockSize);
                if (result.failed()) { e.node->failed.store(true); pluginLog("FX prepare failed " + result.getErrorMessage()); }
            }
            catch (...) { e.node->failed.store(true); }
        }
    publish();
}
void PluginHost::publish()
{
    auto next = std::make_unique<Graph>(vocalNodes, streamNodes, blockSize, rate, current.get());
    auto* pointer = next.get();
    active.store(pointer);
    if (current) retired.push_back(std::move(current));
    current = std::move(next);
    latency.store(current->vocalLatency); count.store(static_cast<int>(vocalNodes.size()));
    pluginLog("FX graph vocal=" + juce::String(vocalNodes.size()) + " stream=" + juce::String(streamNodes.size())
        + " latencySamples=" + juce::String(current->vocalLatency));
    if (readers.load() == 0) retired.clear();
    if (onChange) onChange();
}
void PluginHost::add(Chain chain, const juce::PluginDescription& description, std::function<void(juce::Result)> callback)
{
    if (restoring) { if (callback) callback(juce::Result::fail(juce::String::fromUTF8("読み込みが終わってから追加してください。"))); return; }
    const std::weak_ptr<int> alive = lifetime;
    const auto version = generation;
    formats.createPluginInstanceAsync(description, rate, blockSize,
        [this, alive, chain, description, version, callback](std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
    {
        if (alive.expired()) return;
        if (instance == nullptr) { if (callback) callback(juce::Result::fail(error)); return; }
        if (version != generation) { if (callback) callback(juce::Result::fail(juce::String::fromUTF8("音声形式が変わりました。もう一度追加してください。"))); return; }
        auto node = std::make_shared<Node>(); node->description = description; node->instance = std::move(instance);
        juce::Result result = juce::Result::ok();
        try { result = configure(*node, rate, blockSize); }
        catch (...) { result = juce::Result::fail(juce::String::fromUTF8("エフェクトの準備に失敗しました。")); }
        if (result.wasOk())
        {
            nodes(chain).push_back({node, true}); publish();
            pluginLog("FX added " + juce::String(chain == Chain::vocal ? "FX CHAIN " : "STREAM FX ") + description.name + " latency=" + juce::String(node->instance->getLatencySamples()));
        }
        if (callback) callback(result);
    });
}
void PluginHost::remove(Chain c, const juce::String& id)
{
    if (restoring) return;
    auto& list = nodes(c);
    for (auto& e : list) if (e.node->id == id) e.node->editor.reset();
    std::erase_if(list, [&](const Entry& e) { return e.node->id == id; }); publish();
}
void PluginHost::toggle(Chain c, const juce::String& id)
{
    for (auto& e : nodes(c)) if (e.node->id == id) e.enabled = !e.enabled;
    publish();
}
void PluginHost::move(Chain c, const juce::String& id, int destination)
{
    if (restoring) return;
    auto& list = nodes(c);
    auto it = std::find_if(list.begin(), list.end(), [&](const Entry& e) { return e.node->id == id; });
    if (it == list.end() || list.empty()) return;
    auto entry = *it; list.erase(it);
    list.insert(list.begin() + juce::jlimit(0, static_cast<int>(list.size()), destination), std::move(entry)); publish();
}
std::vector<PluginHost::Item> PluginHost::items(Chain c) const
{
    std::vector<Item> result;
    for (const auto& e : nodes(c)) result.push_back({e.node->id, e.node->description.name, e.enabled && !e.node->failed.load()});
    return result;
}
void PluginHost::showEditor(Chain c, const juce::String& id)
{
    for (auto& e : nodes(c)) if (e.node->id == id)
    {
        if (!e.node->editor)
        {
            e.node->editor = std::make_unique<EditorWindow>(*e.node->instance);
            pluginLog("FX editor opened " + e.node->description.name);
        }
        e.node->editor->setVisible(true); e.node->editor->toFront(true);
    }
}
void PluginHost::closeEditors()
{
    for (auto* list : { &vocalNodes, &streamNodes }) for (auto& e : *list) e.node->editor.reset();
}
int PluginHost::graphLatency(const Graph* graph) noexcept { return graph != nullptr ? graph->vocalLatency : 0; }
void PluginHost::processVocal(Graph* graph, juce::AudioBuffer<float>& buffer, juce::AudioBuffer<float>& monitor, int frames, float wet) noexcept
{
    if (graph == nullptr || frames > graph->capacity) { copy(monitor, buffer, frames); return; }
    for (size_t i = 0; i < graph->vocal.size(); ++i)
    {
        const bool last = i + 1 == graph->vocal.size();
        if (last && i > 0) { copy(graph->lastInput, buffer, frames); graph->lastDelay->process(graph->lastInput, frames); }
        graph->process(graph->vocal[i], buffer, frames);
    }
    copy(monitor, buffer, frames);
    if (graph->vocal.size() > 1)
        for (int c = 0; c < 2; ++c)
            for (int f = 0; f < frames; ++f)
                monitor.setSample(c, f, graph->lastInput.getSample(c, f) * (1.0f - wet) + buffer.getSample(c, f) * wet);
}
void PluginHost::processStream(Graph* graph, juce::AudioBuffer<float>& buffer, int frames) noexcept
{
    if (graph == nullptr || frames > graph->capacity) return;
    for (auto& stage : graph->stream) graph->process(stage, buffer, frames);
}
void PluginHost::processInst(Graph* graph, juce::AudioBuffer<float>& buffer, int frames, int manualMs) noexcept
{
    if (graph == nullptr) return;
    auto& delay = *graph->instDelay;
    const auto target = std::clamp(graph->sampleRate * manualMs / 1000.0,
        -static_cast<double>(graph->vocalLatency), graph->sampleRate * 2.0);
    for (int f = 0; f < frames; ++f)
    {
        delay.manualSamples += std::clamp(target - delay.manualSamples, -0.01, 0.01);
        const auto samples = std::max(0.0, graph->vocalLatency + delay.manualSamples);
        const auto whole = static_cast<size_t>(samples);
        const auto fraction = static_cast<float>(samples - static_cast<double>(whole));
        delay.data[delay.position] = {buffer.getSample(0, f), buffer.getSample(1, f)};
        const auto a = (delay.position + delay.data.size() - whole) % delay.data.size();
        const auto b = (a + delay.data.size() - 1) % delay.data.size();
        for (int c = 0; c < 2; ++c) buffer.setSample(c, f, delay.data[a][static_cast<size_t>(c)] * (1.0f - fraction) + delay.data[b][static_cast<size_t>(c)] * fraction);
        if (++delay.position == delay.data.size()) delay.position = 0;
    }
}
juce::var PluginHost::saveState() const
{
    auto* result = new juce::DynamicObject();
    for (auto chain : { Chain::vocal, Chain::stream })
    {
        juce::Array<juce::var> entries;
        for (const auto& e : nodes(chain))
        {
            juce::MemoryBlock state;
            e.node->beginEdit();
            try { e.node->instance->getStateInformation(state); } catch (...) {}
            e.node->endEdit();
            e.node->savedState = state;
            auto* object = new juce::DynamicObject();
            object->setProperty("description", e.node->description.createXml()->toString());
            object->setProperty("enabled", e.enabled); object->setProperty("encoding", "base64");
            object->setProperty("state", juce::Base64::toBase64(state.getData(), state.getSize()));
            entries.add(juce::var(object));
        }
        result->setProperty(chain == Chain::vocal ? "vocal" : "stream", entries);
    }
    return juce::var(result);
}
void PluginHost::restoreState(const juce::var& state, std::function<void(juce::Result)> callback, bool transactional,
                              std::function<juce::Result()> beforeCommit,
                              std::function<juce::Result(std::function<juce::Result()>)> transaction)
{
    if (restoring) { if (callback) callback(juce::Result::fail(juce::String::fromUTF8("プラグインを読み込み中です。"))); return; }
    struct Restore
    {
        std::vector<Entry> vocal, stream;
        juce::Array<juce::var> pending;
        juce::StringArray errors;
        int index = 0;
        uint64_t version;
        std::function<void(juce::Result)> done;
        std::function<juce::Result()> beforeCommit;
        std::function<juce::Result(std::function<juce::Result()>)> transaction;
        double preparedRate = 48000.0;
        bool transactional = false;
    };
    auto work = std::make_shared<Restore>(); work->version = ++generation; work->done = std::move(callback);
    work->transactional = transactional; work->beforeCommit = std::move(beforeCommit);
    work->transaction = std::move(transaction); work->preparedRate = rate;
    if (!state.isVoid() && (!state.isObject() || !state["vocal"].isArray() || !state["stream"].isArray()))
        work->errors.add(juce::String::fromUTF8("プラグイン列を読めません。"));
    for (auto c : { Chain::vocal, Chain::stream })
        if (auto* array = state[c == Chain::vocal ? "vocal" : "stream"].getArray())
            for (const auto& value : *array)
            {
                auto object = value.clone();
                if (auto* o = object.getDynamicObject()) { o->setProperty("chain", c == Chain::vocal ? 0 : 1); work->pending.add(object); }
            }
    restoring = true;
    const std::weak_ptr<int> alive = lifetime;
    auto step = std::make_shared<std::function<void()>>();
    // Each asynchronous callback owns the next step; the function itself holds only a weak reference.
    const std::weak_ptr<std::function<void()>> weakStep = step;
    *step = [this, alive, work, weakStep]
    {
        if (alive.expired()) return;
        if (work->index >= work->pending.size() || work->version != generation)
        {
            if (work->version != generation)
                work->errors.add(juce::String::fromUTF8("音声形式が変わったため、読み込みを中止しました。"));
            if (work->errors.isEmpty() && work->beforeCommit)
            {
                const auto result = work->beforeCommit();
                if (result.failed()) work->errors.add(result.getErrorMessage());
            }
            if (work->version != generation && work->errors.isEmpty())
                work->errors.add(juce::String::fromUTF8("音声形式が変わったため、読み込みを中止しました。"));
            auto commit = [this, work]() -> juce::Result
            {
                // The engine invokes this with processing suspended after the new device and tap started.
                if (std::abs(work->preparedRate - rate) > 0.5)
                    for (auto* list : { &work->vocal, &work->stream })
                        for (auto& e : *list)
                        {
                            try { if (auto result = configure(*e.node, rate, blockSize); result.failed()) return result; }
                            catch (...) { return juce::Result::fail("Plugin preparation failed at the new sample rate"); }
                        }
                closeEditors(); vocalNodes = std::move(work->vocal); streamNodes = std::move(work->stream); publish();
                pluginLog("FX restored vocal=" + juce::String(vocalNodes.size()) + " stream=" + juce::String(streamNodes.size()));
                return juce::Result::ok();
            };
            if (work->version == generation && (!work->transactional || work->errors.isEmpty()))
            {
                const auto result = work->transaction ? work->transaction(commit) : commit();
                if (result.failed()) work->errors.add(result.getErrorMessage());
            }
            restoring = false;
            if (work->done) work->done(work->errors.isEmpty() ? juce::Result::ok() : juce::Result::fail(work->errors.joinIntoString("\n")));
            return;
        }
        const auto value = work->pending[work->index++];
        juce::PluginDescription description;
        const auto xml = juce::parseXML(value["description"].toString());
        if (xml == nullptr || !description.loadFromXml(*xml))
        {
            work->errors.add(juce::String::fromUTF8("プラグイン情報を読めません。"));
            if (auto next = weakStep.lock()) (*next)(); return;
        }
        auto next = weakStep.lock();
        formats.createPluginInstanceAsync(description, rate, blockSize,
            [this, alive, work, next, value, description](std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
        {
            if (alive.expired()) return;
            if (instance == nullptr) work->errors.add(description.name + ": " + error);
            else
            {
                auto node = std::make_shared<Node>(); node->description = description; node->instance = std::move(instance);
                try
                {
                    juce::MemoryBlock data;
                    bool decoded = false;
                    if (value["encoding"].toString() == "base64")
                    {
                        juce::MemoryOutputStream output;
                        decoded = juce::Base64::convertFromBase64(output, value["state"].toString());
                        data = output.getMemoryBlock();
                    }
                    else decoded = data.fromBase64Encoding(value["state"].toString()); // M3 saved JUCE's older encoding.
                    if (!decoded) throw std::runtime_error("Invalid plugin state encoding");
                    node->instance->setStateInformation(data.getData(), static_cast<int>(data.getSize()));
                    auto result = configure(*node, rate, blockSize);
                    node->stateChanged.store(false);
                    node->savedState = data;
                    if (result.wasOk()) (static_cast<int>(value["chain"]) == 0 ? work->vocal : work->stream).push_back({node, static_cast<bool>(value["enabled"])});
                    else work->errors.add(description.name + ": " + result.getErrorMessage());
                }
                catch (...) { work->errors.add(description.name + ": state restore failed"); }
            }
            if (next) (*next)();
        });
    };
    (*step)();
}
void PluginHost::timerCallback()
{
    if (readers.load() == 0) retired.clear();
    int sum = 0;
    for (auto& e : vocalNodes) sum += e.node->reportedLatency.load(std::memory_order_relaxed);
    bool changed = sum != latency.load();
    for (auto* list : { &vocalNodes, &streamNodes })
        for (auto& e : *list)
            if (e.node->failed.load() && e.enabled)
            {
                e.enabled = false; changed = true;
                const auto message = e.node->description.name + juce::String::fromUTF8(" の処理を停止しました。他の音声経路は継続します。");
                pluginLog("FX processing failed " + message); if (onError) onError(message);
            }
    if (current)
        for (const auto* stages : { &current->vocal, &current->stream })
            for (const auto& stage : *stages)
                if (stage.latency != stage.node->reportedLatency.load(std::memory_order_relaxed)) changed = true;
    bool stateChanged = false;
    for (auto* list : { &vocalNodes, &streamNodes }) for (auto& e : *list)
        if (e.node->stateChanged.exchange(false))
        {
            juce::MemoryBlock state;
            e.node->beginEdit();
            try { e.node->instance->getStateInformation(state); } catch (...) {}
            e.node->endEdit();
            if (state != e.node->savedState) { e.node->savedState = std::move(state); stateChanged = true; }
        }
    if (changed) publish();
    else if (stateChanged && onChange) onChange();
    for (auto* list : { &vocalNodes, &streamNodes })
        for (auto& e : *list)
        {
            const auto blocks = e.node->blocks.exchange(0);
            if (blocks > 0 && !reported.contains(e.node->id))
            {
                reported.add(e.node->id);
                pluginLog("FX processing " + e.node->description.name + " blocks=" + juce::String(blocks) + " stereo=2");
            }
        }
}
}
