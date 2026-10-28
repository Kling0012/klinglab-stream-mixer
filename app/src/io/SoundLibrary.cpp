#include "SoundLibrary.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <optional>

namespace ksm
{
namespace
{
void soundLog(const juce::String& text)
{
    juce::Logger::writeToLog(text);
    std::cout << text << std::endl;
}
const SoundLibrary::Item* findItem(const SoundLibrary::Payload* payload, int64_t id)
{
    if (payload == nullptr) return nullptr;
    for (const auto& item : *payload)
        if (item.id == id) return &item;
    return nullptr;
}
}

SoundLibrary::SoundLibrary(juce::File settingsDirectory)
    : Thread("ksm-sounds"), soundsDirectory(std::move(settingsDirectory).getChildFile("sounds"))
{
    formats.registerBasicFormats(); // WAV, AIFF, MP3, FLAC, OGG and Core Audio (AAC/M4A) on macOS.
    publish();
    startTimer(500);
    startThread();
}

SoundLibrary::~SoundLibrary()
{
    stopTimer();
    stopThread(2000);
    lifetime.reset();
    active.store(nullptr);
    current.reset();
    retired.clear();
}

int64_t SoundLibrary::makeId()
{
    const auto uuid = juce::Uuid();
    uint64_t a = 0, b = 0;
    std::memcpy(&a, uuid.getRawData(), sizeof(a));
    std::memcpy(&b, uuid.getRawData() + sizeof(a), sizeof(b));
    const auto id = static_cast<int64_t>(a ^ b);
    return id != 0 && id != -1 ? id : 1;
}

juce::String SoundLibrary::lengthText(double seconds)
{
    const auto total = static_cast<int>(std::floor(seconds + 0.5));
    const auto minutes = total / 60, rest = total % 60;
    if (minutes >= 60)
        return juce::String(minutes / 60) + ":" + juce::String(minutes % 60).paddedLeft('0', 2) + ":" + juce::String(rest).paddedLeft('0', 2);
    return juce::String(minutes) + ":" + juce::String(rest).paddedLeft('0', 2);
}

void SoundLibrary::publish()
{
    auto next = std::make_unique<Payload>(list);
    const auto* pointer = next.get();
    active.store(pointer);
    if (current != nullptr) retired.push_back(std::move(current));
    current = std::move(next);
    if (readers.load() == 0) retired.clear();
    if (onItemsChanged != nullptr) onItemsChanged();
}

void SoundLibrary::enqueue(const Job& job)
{
    {
        const std::lock_guard<std::mutex> lock(jobMutex);
        jobs.push_back(job);
    }
    notify();
}

std::vector<SoundLibrary::Item> SoundLibrary::items(Kind kind) const
{
    std::vector<Item> result;
    for (const auto& item : list)
        if (item.kind == kind) result.push_back(item);
    return result;
}

int SoundLibrary::count(Kind kind) const
{
    int total = 0;
    for (const auto& item : list)
        if (item.kind == kind) ++total;
    return total;
}

const SoundLibrary::Item* SoundLibrary::find(int64_t id) const
{
    for (const auto& item : list)
        if (item.id == id) return &item;
    return nullptr;
}

void SoundLibrary::rename(int64_t id, const juce::String& name)
{
    if (auto* item = const_cast<Item*>(find(id)))
    {
        auto trimmed = name.trim();
        item->name = trimmed.isEmpty() ? item->name : trimmed.substring(0, 80);
        publish();
    }
}

void SoundLibrary::move(int64_t id, int destination)
{
    std::optional<size_t> source;
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].id == id) { source = i; break; }
    if (!source.has_value()) return;
    // The drop index counts only items of the same kind.
    Kind kind = list[*source].kind;
    std::vector<size_t> sameKind;
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].kind == kind) sameKind.push_back(i);
    int within = 0;
    for (size_t i = 0; i < sameKind.size(); ++i)
        if (sameKind[i] == *source) { within = static_cast<int>(i); break; }
    int target = juce::jlimit(0, static_cast<int>(sameKind.size()), destination);
    if (target == within) return;
    if (target > within) --target;
    auto item = list[*source];
    list.erase(list.begin() + static_cast<long>(*source));
    std::optional<size_t> insert;
    if (target >= static_cast<int>(sameKind.size()))
    {
        for (size_t i = list.size(); i-- > 0;)
            if (list[i].kind == kind) { insert = i + 1; break; }
        if (!insert.has_value()) insert = list.size();
    }
    else
    {
        int seen = 0;
        for (size_t i = 0; i < list.size(); ++i)
        {
            if (list[i].kind != kind) continue;
            if (seen == target) { insert = i; break; }
            ++seen;
        }
        if (!insert.has_value()) insert = list.size();
    }
    list.insert(list.begin() + static_cast<long>(*insert), item);
    publish();
}

void SoundLibrary::remove(int64_t id)
{
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].id == id)
        {
            const auto name = list[i].name;
            soundsDirectory.getChildFile(list[i].fileName).deleteFile();
            list.erase(list.begin() + static_cast<long>(i));
            publish();
            soundLog("SOUNDS removed " + name);
            return;
        }
}

void SoundLibrary::setLoop(int64_t id, bool loop)
{
    if (auto* item = const_cast<Item*>(find(id)))
    {
        item->loop = loop;
        publish();
    }
}

bool SoundLibrary::queue(Command::Type type, int64_t id)
{
    const Command command { type, id, nextSequence + 1 };
    if (!commands.push(command)) return false;
    ++nextSequence; pendingCommands.push_back(command);
    if (type == Command::Type::bgmPlay || type == Command::Type::bgmStop) bgmSequence = nextSequence;
    return true;
}
int64_t SoundLibrary::bgmPlayingId() const noexcept
{
    return consumedSequence.load(std::memory_order_acquire) < bgmSequence ? requestedBgm : bgmPlaying.load(std::memory_order_acquire);
}
bool SoundLibrary::bgmBusy() const noexcept
{
    return consumedSequence.load(std::memory_order_acquire) < bgmSequence || bgmTransition.load(std::memory_order_acquire);
}
juce::String SoundLibrary::playBgm(int64_t id)
{
    const auto* item = find(id);
    if (item == nullptr || item->kind != Kind::bgm) return "EMPTY_SLOT";
    if (bgmBusy()) return "BUSY";
    if (bgmPlayingId() == id) return stopSounds("bgm");
    const auto previous = bgmPlayingId();
    if (!queue(Command::Type::bgmPlay, id)) return "BUSY";
    requestedBgm = previous != -1 ? previous : id;
    return {};
}
void SoundLibrary::stopBgm() { stopSounds("bgm"); }
juce::String SoundLibrary::playSe(int64_t id)
{
    const auto* item = find(id);
    if (item == nullptr || item->kind != Kind::se) return "EMPTY_SLOT";
    if (isSePlaying(id)) return queue(Command::Type::seStop, id) ? juce::String() : juce::String("BUSY");
    const auto voices = seVoiceStates();
    if (std::count_if(voices.begin(), voices.end(), [](const auto& v) { return v.id != 0; }) >= maximumSeVoices) return "LIMIT_REACHED";
    return queue(Command::Type::sePlay, id) ? juce::String() : juce::String("BUSY");
}
void SoundLibrary::stopSe(int64_t id) { queue(Command::Type::seStop, id); }
void SoundLibrary::stopSeAll() { stopSounds("se"); }
juce::String SoundLibrary::stopSounds(const juce::String& kind)
{
    // One queue command makes stopping both kinds indivisible even near capacity.
    const auto type = kind == "all" ? Command::Type::stopAll : kind == "bgm" ? Command::Type::bgmStop : Command::Type::seStopAll;
    const auto playing = bgmPlayingId();
    if (!queue(type)) return "BUSY";
    if (kind != "se") { bgmSequence = nextSequence; requestedBgm = playing; }
    return {};
}

void SoundLibrary::addFiles(const juce::StringArray& paths, Kind kind,
                            std::function<void(juce::StringArray failures)> onFinished)
{
    Job job;
    job.type = Job::Type::import;
    job.sources = paths;
    job.kind = kind;
    job.rate = targetRate > 0.0 ? targetRate : 48000.0;
    job.generation = decodeGeneration;
    job.onFinished = std::move(onFinished);
    soundLog("SOUNDS import requested " + juce::String(paths.size()) + " file(s) as "
             + (kind == Kind::bgm ? "BGM" : "SE"));
    enqueue(job);
}

juce::var SoundLibrary::saveState() const
{
    auto* root = new juce::DynamicObject();
    root->setProperty("version", 1);
    juce::Array<juce::var> array;
    for (const auto& item : list)
    {
        auto* entry = new juce::DynamicObject();
        entry->setProperty("id", item.id);
        entry->setProperty("file", item.fileName);
        entry->setProperty("name", item.name);
        entry->setProperty("kind", item.kind == Kind::bgm ? "bgm" : "se");
        entry->setProperty("loop", item.loop);
        entry->setProperty("seconds", item.seconds);
        array.add(juce::var(entry));
    }
    root->setProperty("items", array);
    return juce::var(root);
}

void SoundLibrary::restoreState(const juce::var& state)
{
    if (!state.isObject() || !state["items"].isArray()) return;
    std::vector<Item> restored;
    for (const auto& entry : *state["items"].getArray())
    {
        Item item;
        item.id = static_cast<int64_t>(entry["id"]);
        item.fileName = entry["file"].toString();
        item.name = entry["name"].toString();
        item.kind = entry["kind"].toString() == "se" ? Kind::se : Kind::bgm;
        item.loop = static_cast<bool>(entry["loop"]);
        item.seconds = static_cast<double>(entry["seconds"]);
        if (item.id == 0 || item.fileName.isEmpty() || !soundsDirectory.getChildFile(item.fileName).existsAsFile())
        {
            soundLog("SOUNDS restore skipped missing file " + item.fileName);
            continue;
        }
        restored.push_back(item);
    }
    list = std::move(restored);
    publish();
    soundLog("SOUNDS restored bgm=" + juce::String(count(Kind::bgm)) + " se=" + juce::String(count(Kind::se)));
}

void SoundLibrary::prepare(double sampleRate)
{
    if (sampleRate <= 0.0) return;
    if (juce::approximatelyEqual(sampleRate, targetRate)) return;
    // Callbacks are stopped. Convert the cursor and remaining fades through seconds.
    const double ratio = sampleRate / (targetRate > 0.0 ? targetRate : sampleRate);
    for (auto& voice : bgmVoices)
    {
        voice.position = static_cast<int>(std::round(voice.position * ratio));
        voice.slope = static_cast<float>(voice.slope / ratio);
    }
    for (auto& voice : seVoices)
    {
        voice.position = static_cast<int>(std::round(voice.position * ratio));
        voice.slope = static_cast<float>(voice.slope / ratio);
    }
    targetRate = sampleRate; ++decodeGeneration;
    fadePerSample = static_cast<float>(1.0 / (sampleRate * 1.0));   // 1-second BGM crossfade
    if (!list.empty()) startDecodeAll();
}

void SoundLibrary::release()
{
    // Audio callbacks are stopped. Keep the published payload; the next open() calls prepare().
    for (auto& voice : bgmVoices) { voice.active = false; voice.gain = 0.0f; voice.slope = 0.0f; }
    for (auto& voice : seVoices) { voice.active = false; voice.gain = 0.0f; voice.slope = 0.0f; }
    commands.clear(); pendingCommands.clear(); nextBgm = 0;
    consumedSequence.store(nextSequence, std::memory_order_release); bgmTransition.store(false);
    bgmPlaying.store(-1, std::memory_order_relaxed);
    bgmPosition.store(0.0f, std::memory_order_relaxed);
    for (auto& id : sePlayingIds) id.store(0);
    observedSe = {}; observedSequence = nextSequence;
}

void SoundLibrary::startDecodeAll()
{
    Job job;
    job.type = Job::Type::decodeAll;
    job.snapshot = list;
    job.rate = targetRate;
    job.generation = decodeGeneration;
    enqueue(job);
    soundLog("SOUNDS decoding " + juce::String(list.size()) + " item(s) at "
             + juce::String(targetRate, 0) + " Hz");
}

std::shared_ptr<const juce::AudioBuffer<float>>
SoundLibrary::decode(const juce::File& source, double rate, double& secondsOut) // non-const: AudioFormatManager::createReaderFor is not const
{
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(source));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0) return nullptr;
    const auto length = static_cast<int>(reader->lengthInSamples);
    // Padding keeps the interpolator from reading past the end.
    juce::AudioBuffer<float> input(2, length + 16);
    input.clear();
    reader->read(&input, 0, length, 0, true, true);
    if (reader->numChannels == 1) input.copyFrom(1, 0, input, 0, 0, length);
    const double ratio = reader->sampleRate / rate; // input samples per output sample
    auto output = std::make_shared<juce::AudioBuffer<float>>(2, static_cast<int>(std::ceil(length / ratio)) + 2);
    if (std::abs(ratio - 1.0) < 1.0e-9)
    {
        for (int c = 0; c < 2; ++c) output->copyFrom(c, 0, input, c, 0, length);
    }
    else
    {
        for (int c = 0; c < 2; ++c)
        {
            juce::LagrangeInterpolator resampler;
            const int produced = resampler.process(ratio, input.getReadPointer(c), output->getWritePointer(c),
                                                    output->getNumSamples());
            if (produced < output->getNumSamples()) output->clear(produced, output->getNumSamples() - produced);
        }
    }
    secondsOut = output->getNumSamples() / rate;
    return output;
}

void SoundLibrary::run()
{
    while (!threadShouldExit())
    {
        Job job;
        bool have = false;
        {
            const std::lock_guard<std::mutex> lock(jobMutex);
            if (!jobs.empty()) { job = std::move(jobs.front()); jobs.pop_front(); have = true; }
        }
        if (!have)
        {
            wait(-1);
            continue;
        }
        if (job.type == Job::Type::import)
        {
            juce::StringArray failures;
            std::vector<Item> added;
            for (const auto& path : job.sources)
            {
                const juce::File source(path);
                const auto baseName = source.getFileNameWithoutExtension();
                std::unique_ptr<juce::AudioFormatReader> probe(formats.createReaderFor(source));
                if (probe == nullptr || probe->lengthInSamples <= 0)
                {
                    failures.add(baseName + juce::String::fromUTF8("：読み込めない形式です"));
                    continue;
                }
                const double seconds = probe->lengthInSamples / probe->sampleRate;
                probe.reset();
                if (job.kind == Kind::se && seconds > 180.0)
                {
                    failures.add(baseName + "（" + lengthText(seconds) + juce::String::fromUTF8("）は3分を超えるため、SE には入れられません。BGM 欄に入れてください。"));
                    continue;
                }
                Item item;
                item.id = makeId();
                item.name = baseName.substring(0, 80);
                item.kind = job.kind;
                item.fileName = juce::Uuid().toString().removeCharacters("-").toLowerCase()
                    + (source.getFileExtension().isEmpty() ? "" : "." + source.getFileExtension().toLowerCase());
                const auto destination = soundsDirectory.getChildFile(item.fileName);
                if (!source.copyFileTo(destination))
                {
                    failures.add(baseName + juce::String::fromUTF8("：sounds へコピーできませんでした"));
                    continue;
                }
                double decoded = 0.0;
                item.buffer = decode(destination, job.rate, decoded);
                item.decodedRate = job.rate;
                if (item.buffer == nullptr)
                {
                    failures.add(baseName + juce::String::fromUTF8("：読み込みに失敗しました"));
                    destination.deleteFile();
                    continue;
                }
                item.seconds = decoded;
                added.push_back(std::move(item));
            }
            const std::weak_ptr<void> guard = lifetime;
            juce::MessageManager::callAsync([this, guard, added, failures, onFinished = job.onFinished, generation = job.generation]() mutable
            {
                if (guard.expired()) return;
                if (!added.empty())
                {
                    list.insert(list.end(), added.begin(), added.end());
                    publish();
                    if (generation != decodeGeneration) startDecodeAll();
                    soundLog("SOUNDS added " + juce::String(added.size()) + " item(s); bgm="
                             + juce::String(count(Kind::bgm)) + " se=" + juce::String(count(Kind::se)));
                }
                if (onFinished != nullptr) onFinished(failures);
                if (!failures.isEmpty())
                    soundLog("SOUNDS import rejected: " + failures.joinIntoString(" | "));
            });
        }
        else if (job.type == Job::Type::decodeAll)
        {
            std::vector<std::pair<int64_t, std::shared_ptr<const juce::AudioBuffer<float>>>> decoded;
            std::vector<int64_t> missing;
            for (const auto& item : job.snapshot)
            {
                double seconds = 0.0;
                auto buffer = decode(soundsDirectory.getChildFile(item.fileName), job.rate, seconds);
                if (buffer == nullptr) { missing.push_back(item.id); continue; }
                decoded.emplace_back(item.id, std::move(buffer));
            }
            const std::weak_ptr<void> guard = lifetime;
            juce::MessageManager::callAsync([this, guard, decoded, missing, rate = job.rate, generation = job.generation]()
            {
                if (guard.expired() || generation != decodeGeneration) return;
                for (const auto& [id, buffer] : decoded)
                    if (auto* item = const_cast<Item*>(find(id))) { item->buffer = buffer; item->decodedRate = rate; }
                for (auto id : missing)
                    if (auto* item = const_cast<Item*>(find(id))) item->buffer.reset();
                // A missing/undecodable item must not wait forever or use an old-rate buffer.
                std::erase_if(list, [&](const Item& item)
                {
                    return item.buffer == nullptr && std::find(missing.begin(), missing.end(), item.id) != missing.end();
                });
                publish();
                soundLog("SOUNDS decoded " + juce::String(decoded.size()) + " item(s); missing "
                         + juce::String(missing.size()));
            });
        }
    }
}

void SoundLibrary::timerCallback()
{
    if (readers.load() == 0) retired.clear();
    seVoiceStates();
    std::erase_if(pendingCommands, [this](const auto& c) { return c.sequence <= observedSequence; });
}

void SoundLibrary::render(Stereo* bgm, Stereo* se, int frames) noexcept
{
    for (int f = 0; f < frames; ++f) { bgm[f] = {}; se[f] = {}; }
    ReadScope scope(*this);
    const Payload* payload = scope.get();
    voiceRevision.fetch_add(1);
    Command command;
    auto lastSequence = consumedSequence.load(std::memory_order_relaxed);
    while (commands.pop(command))
    {
        switch (command.type)
        {
        case Command::Type::bgmPlay:
        {
            bool fading = false;
            for (auto& voice : bgmVoices)
                if (voice.active) { voice.slope = -fadePerSample; fading = true; }
            nextBgm = fading ? command.id : 0;
            if (!fading)
            {
                auto& slot = bgmVoices[0]; slot.active = true; slot.id = command.id;
                slot.position = 0; slot.gain = 1.0f; slot.slope = 0.0f;
            }
            break;
        }
        case Command::Type::stopAll:
            for (auto& voice : seVoices) voice.active = false;
            [[fallthrough]];
        case Command::Type::bgmStop:
            nextBgm = 0;
            for (auto& voice : bgmVoices)
                if (voice.active) voice.slope = -fadePerSample;
            break;
        case Command::Type::sePlay:
            // Voices already releasing count as free; at most 16 sound at once.
            for (auto& voice : seVoices)
                if (!voice.active || voice.slope < 0.0f)
                {
                    voice.active = true; voice.id = command.id;
                    voice.position = 0; voice.gain = 1.0f; voice.slope = 0.0f;
                    break;
                }
            break;
        case Command::Type::seStop:
            for (auto& voice : seVoices)
                if (voice.active && voice.id == command.id) voice.active = false;
            break;
        case Command::Type::seStopAll:
            for (auto& voice : seVoices)
                voice.active = false;
            break;
        }
        lastSequence = command.sequence;
    }
    float blockPeak = 0.0f;
    int64_t playingId = -1;
    float playingPosition = 0.0f;
    for (auto& voice : bgmVoices)
    {
        if (!voice.active) continue;
        const auto* item = findItem(payload, voice.id);
        if (item == nullptr) { voice.active = false; continue; } // the item was removed
        const auto* buffer = item->buffer.get();
        if (buffer == nullptr || std::abs(item->decodedRate - targetRate) > 0.5)
        {
            if (voice.slope < 0.0f && buffer == nullptr)
            {
                voice.gain += voice.slope * static_cast<float>(frames);
                if (voice.gain <= 0.0f) voice.active = false;
            }
            if (voice.active)
            {
                playingId = voice.id;
                playingPosition = static_cast<float>(voice.position / targetRate);
            }
            continue;
        }
        const int length = buffer->getNumSamples();
        const bool loop = item->loop;
        const bool primary = true;
        for (int f = 0; f < frames && voice.active; ++f)
        {
            if (voice.position >= length)
            {
                if (loop && voice.slope >= 0.0f) voice.position %= length;
                else { voice.active = false; break; }
            }
            const auto l = buffer->getSample(0, voice.position) * voice.gain;
            const auto r = buffer->getSample(1, voice.position) * voice.gain;
            bgm[f] = bgm[f] + Stereo { l, r };
            blockPeak = std::max(blockPeak, std::max(std::abs(l), std::abs(r)));
            voice.gain += voice.slope;
            if (voice.slope > 0.0f && voice.gain >= 1.0f) { voice.gain = 1.0f; voice.slope = 0.0f; }
            else if (voice.slope < 0.0f && voice.gain <= 0.0f) { voice.active = false; break; }
            ++voice.position;
            if (voice.position >= length)
            {
                if (loop && voice.slope >= 0.0f) voice.position = 0;
                else { voice.active = false; break; }
            }
        }
        if (primary && voice.active)
        {
            playingId = voice.id;
            playingPosition = static_cast<float>(voice.position) / static_cast<float>(targetRate > 0.0 ? targetRate : 48000.0);
        }
    }
    bool fading = false, activeBgm = false;
    for (const auto& voice : bgmVoices) { fading = fading || (voice.active && voice.slope < 0.0f); activeBgm = activeBgm || voice.active; }
    if (!activeBgm && nextBgm != 0)
    {
        auto& slot = bgmVoices[0]; slot.active = true; slot.id = nextBgm;
        slot.position = 0; slot.gain = 1.0f; slot.slope = 0.0f;
        playingId = nextBgm; nextBgm = 0;
    }
    bgmTransition.store(fading || nextBgm != 0, std::memory_order_release);
    bgmPlaying.store(playingId, std::memory_order_relaxed);
    bgmPosition.store(playingPosition, std::memory_order_relaxed);
    for (size_t i = 0; i < seVoices.size(); ++i)
    {
        auto& voice = seVoices[i];
        if (!voice.active)
        {
            sePlayingIds[i].store(0);
            continue;
        }
        const auto* item = findItem(payload, voice.id);
        if (item == nullptr) { voice.active = false; sePlayingIds[i].store(0); continue; }
        const auto* buffer = item->buffer.get();
        if (buffer == nullptr || std::abs(item->decodedRate - targetRate) > 0.5)
        {
            sePlayingIds[i].store(voice.id); sePlayingPositions[i].store(static_cast<float>(voice.position / targetRate));
            continue; // still decoding; keep the play queued
        }
        const int length = buffer->getNumSamples();
        for (int f = 0; f < frames && voice.active; ++f)
        {
            if (voice.position >= length) { voice.active = false; break; }
            const auto l = buffer->getSample(0, voice.position) * voice.gain;
            const auto r = buffer->getSample(1, voice.position) * voice.gain;
            se[f] = se[f] + Stereo { l, r };
            blockPeak = std::max(blockPeak, std::max(std::abs(l), std::abs(r)));
            voice.gain += voice.slope;
            if (voice.slope < 0.0f && voice.gain <= 0.0f) { voice.active = false; break; }
            ++voice.position;
        }
        if (voice.active)
        {
            sePlayingIds[i].store(voice.id);
            sePlayingPositions[i].store(static_cast<float>(voice.position)
                / static_cast<float>(targetRate > 0.0 ? targetRate : 48000.0));
        }
        else sePlayingIds[i].store(0);
    }
    consumedSequence.store(lastSequence);
    voiceRevision.fetch_add(1);
    auto previous = renderPeak.load(std::memory_order_relaxed);
    while (blockPeak > previous && !renderPeak.compare_exchange_weak(previous, blockPeak, std::memory_order_relaxed)) {}
}

std::array<SoundLibrary::VoiceState, SoundLibrary::maximumSeVoices> SoundLibrary::seVoiceStates() const noexcept
{
    // Take a complete audio-block observation, then overlay unconsumed commands.
    // Bounded retries keep the message thread responsive during a busy callback.
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        const auto before = voiceRevision.load();
        if ((before & 1) != 0) continue;
        std::array<VoiceState, maximumSeVoices> snapshot {};
        for (size_t i = 0; i < snapshot.size(); ++i)
        {
            snapshot[i].id = sePlayingIds[i].load();
            snapshot[i].position = sePlayingPositions[i].load();
        }
        const auto sequence = consumedSequence.load();
        if (voiceRevision.load() == before) { observedSe = snapshot; observedSequence = sequence; break; }
    }
    auto result = observedSe;
    const auto consumed = observedSequence;
    for (const auto& command : pendingCommands)
    {
        if (command.sequence <= consumed) continue;
        if (command.type == Command::Type::seStopAll || command.type == Command::Type::stopAll)
            for (auto& voice : result) voice.id = 0;
        if (command.type == Command::Type::seStop)
            for (auto& voice : result) if (voice.id == command.id) voice.id = 0;
        if (command.type == Command::Type::sePlay)
            for (auto& voice : result) if (voice.id == 0) { voice.id = command.id; voice.position = 0; break; }
    }
    return result;
}

bool SoundLibrary::isSePlaying(int64_t id) const noexcept
{
    for (const auto& slot : seVoiceStates()) if (slot.id == id) return true;
    return false;
}
}
