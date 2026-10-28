#pragma once

#include "../engine/Router.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <atomic>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace ksm
{
// BGM / SE playback. The message thread owns the item list and publishes decoded
// payloads; the audio callback only reads the published list and never touches
// files, locks or allocations.
class SoundLibrary final : private juce::Thread, private juce::Timer
{
public:
    enum class Kind { bgm, se };
    static constexpr int maximumSeVoices = 16;
    struct Item
    {
        int64_t id = 0;
        juce::String name, fileName;
        Kind kind = Kind::bgm;
        bool loop = false;
        double seconds = 0.0, decodedRate = 0.0;
        std::shared_ptr<const juce::AudioBuffer<float>> buffer; // null until decoded
    };
    using Payload = std::vector<Item>;
    class ReadScope
    {
    public:
        explicit ReadScope(SoundLibrary& library) noexcept
            : owner(library)
        {
            owner.readers.fetch_add(1);
            payload = library.active.load();
        }
        ~ReadScope() { owner.readers.fetch_sub(1, std::memory_order_acq_rel); }
        const Payload* get() const noexcept { return payload; }
    private:
        SoundLibrary& owner;
        const Payload* payload;
    };

    explicit SoundLibrary(juce::File settingsDirectory);
    ~SoundLibrary() override;

    // Message thread.
    void addFiles(const juce::StringArray& paths, Kind, std::function<void(juce::StringArray failures)> onFinished);
    std::vector<Item> items(Kind) const;
    void rename(int64_t id, const juce::String& name);
    void move(int64_t id, int destination);
    void remove(int64_t id);
    void setLoop(int64_t id, bool loop);
    juce::String playBgm(int64_t id);
    void stopBgm();
    juce::String playSe(int64_t id);
    void stopSe(int64_t id);
    void stopSeAll();
    juce::String stopSounds(const juce::String& kind);
    int count(Kind) const;

    juce::var saveState() const;
    void restoreState(const juce::var&);
    std::function<void()> onItemsChanged;

    // Engine wiring. prepare() re-decodes when the mixer rate changes.
    void prepare(double sampleRate);
    void release();
    void render(Stereo* bgm, Stereo* se, int frames) noexcept; // audio thread

    // Audio thread -> UI observations.
    int64_t bgmPlayingId() const noexcept;
    bool bgmBusy() const noexcept;
    float bgmPositionSeconds() const noexcept { return bgmPosition.load(std::memory_order_relaxed); }
    struct VoiceState { int64_t id = 0; float position = 0.0f; };
    std::array<VoiceState, maximumSeVoices> seVoiceStates() const noexcept;
    bool isSePlaying(int64_t id) const noexcept;
    float peakSince(float& previous) noexcept
    {
        const auto value = renderPeak.exchange(0.0f, std::memory_order_relaxed);
        const auto peak = std::max(value, previous);
        previous = peak;
        return peak;
    }

private:
    struct Command
    {
        enum class Type { bgmPlay, bgmStop, sePlay, seStop, seStopAll, stopAll };
        Type type = Type::bgmStop;
        int64_t id = 0;
        uint64_t sequence = 0;
    };
    class CommandQueue // single producer (message) and single consumer (audio)
    {
    public:
        bool push(const Command& command) noexcept
        {
            const auto w = write.load(std::memory_order_relaxed);
            if (w - read.load(std::memory_order_acquire) >= capacity) return false;
            slots[w & (capacity - 1)] = command;
            write.store(w + 1, std::memory_order_release);
            return true;
        }
        bool pop(Command& command) noexcept
        {
            const auto r = read.load(std::memory_order_relaxed);
            if (r == write.load(std::memory_order_acquire)) return false;
            command = slots[r & (capacity - 1)];
            read.store(r + 1, std::memory_order_release);
            return true;
        }
        void clear() noexcept { read.store(write.load(std::memory_order_relaxed), std::memory_order_relaxed); }
    private:
        static constexpr size_t capacity = 64;
        std::array<Command, capacity> slots {};
        std::atomic<size_t> write { 0 }, read { 0 };
    };
    struct Job
    {
        enum class Type { import, decodeAll };
        Type type = Type::import;
        juce::StringArray sources;
        Kind kind = Kind::bgm;
        std::vector<Item> snapshot;
        double rate = 48000.0;
        uint64_t generation = 0;
        std::function<void(juce::StringArray)> onFinished;
    };
    struct BgmVoice
    {
        bool active = false;
        int64_t id = 0;
        int position = 0;
        float gain = 0.0f, slope = 0.0f;
    };
    struct SeVoice
    {
        bool active = false;
        int64_t id = 0;
        int position = 0;
        float gain = 1.0f, slope = 0.0f;
    };

    juce::File soundsDirectory;
    juce::AudioFormatManager formats;
    // Item list: only the message thread writes. Published snapshots feed the audio callback.
    std::vector<Item> list;
    std::unique_ptr<Payload> current;
    std::vector<std::unique_ptr<Payload>> retired;
    std::atomic<const Payload*> active { nullptr };
    std::atomic<unsigned> readers { 0 };
    CommandQueue commands;
    uint64_t nextSequence = 0, bgmSequence = 0;
    int64_t requestedBgm = -1;
    std::vector<Command> pendingCommands;
    std::atomic<uint64_t> consumedSequence { 0 };
    std::atomic<bool> bgmTransition { false };
    int64_t nextBgm = 0;
    bool queue(Command::Type type, int64_t id = 0);
    std::deque<Job> jobs;
    std::mutex jobMutex;
    std::atomic<int> pendingImports { 0 };
    double targetRate = 0.0;
    uint64_t decodeGeneration = 0;
    // Audio-callback state.
    std::array<BgmVoice, 3> bgmVoices {};
    std::array<SeVoice, maximumSeVoices> seVoices {};
    std::atomic<int64_t> bgmPlaying { -1 };
    std::atomic<float> bgmPosition { 0.0f };
    std::array<std::atomic<int64_t>, maximumSeVoices> sePlayingIds {};
    std::array<std::atomic<float>, maximumSeVoices> sePlayingPositions {};
    std::atomic<uint64_t> voiceRevision { 0 };
    mutable std::array<VoiceState, maximumSeVoices> observedSe {};
    mutable uint64_t observedSequence = 0;
    std::atomic<float> renderPeak { 0.0f };
    std::shared_ptr<void> lifetime = std::make_shared<int>(0);
    double fadePerSample = 1.0f / 48000.0f;

    void publish();
    void enqueue(const Job&);
    void run() override;
    void timerCallback() override; // retires payloads no reader references
    void startDecodeAll();
    std::shared_ptr<const juce::AudioBuffer<float>> decode(const juce::File& source, double rate, double& secondsOut);
    static int64_t makeId();
    static juce::String lengthText(double seconds);
    const Item* find(int64_t id) const;
};
}
