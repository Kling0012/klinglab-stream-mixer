#include "AudioEngine.h"
#include "ClockBridge.h"
#import <CoreAudio/CoreAudio.h>
#import <AVFoundation/AVFoundation.h>
#import <AppKit/AppKit.h>
#include <iostream>
#include <cstring>

namespace ksm
{
namespace
{
void audioLog(const juce::String& text)
{
    const auto line = juce::Time::getCurrentTime().toISO8601(true) + " " + text;
    juce::Logger::writeToLog(line);
    std::cout << line << std::endl;
}
AudioObjectPropertyAddress address(AudioObjectPropertySelector selector, AudioObjectPropertyScope scope = kAudioObjectPropertyScopeGlobal)
{
    return { selector, scope, kAudioObjectPropertyElementMain };
}
template<class T> T property(AudioObjectID object, AudioObjectPropertySelector selector,
                              AudioObjectPropertyScope scope = kAudioObjectPropertyScopeGlobal, T fallback = {})
{
    auto a = address(selector, scope);
    UInt32 size = sizeof(T);
    T result = fallback;
    if (AudioObjectGetPropertyData(object, &a, 0, nullptr, &size, &result) != noErr) return fallback;
    return result;
}
juce::String stringProperty(AudioObjectID object, AudioObjectPropertySelector selector)
{
    const auto value = property<CFStringRef>(object, selector);
    if (value == nullptr) return {};
    const auto result = juce::String::fromCFString(value);
    CFRelease(value);
    return result;
}
unsigned channels(AudioObjectID device, bool input)
{
    auto a = address(kAudioDevicePropertyStreamConfiguration, input ? kAudioObjectPropertyScopeInput : kAudioObjectPropertyScopeOutput);
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(device, &a, 0, nullptr, &size) != noErr || size == 0) return 0;
    std::vector<char> storage(size);
    auto* list = reinterpret_cast<AudioBufferList*>(storage.data());
    if (AudioObjectGetPropertyData(device, &a, 0, nullptr, &size, list) != noErr) return 0;
    unsigned count = 0;
    for (UInt32 b = 0; b < list->mNumberBuffers; ++b) count += list->mBuffers[b].mNumberChannels;
    return count;
}
struct DeviceStreams
{
    std::vector<AudioStreamID> ids;
    OSStatus status = noErr;
};
DeviceStreams deviceStreams(AudioObjectID device, bool input)
{
    DeviceStreams result;
    auto a = address(kAudioDevicePropertyStreams, input ? kAudioObjectPropertyScopeInput : kAudioObjectPropertyScopeOutput);
    UInt32 bytes = 0;
    result.status = AudioObjectGetPropertyDataSize(device, &a, 0, nullptr, &bytes);
    if (result.status != noErr || bytes == 0) return result;
    if (bytes % sizeof(AudioStreamID) != 0)
    {
        result.status = kAudioHardwareBadPropertySizeError;
        return result;
    }
    result.ids.resize(bytes / sizeof(AudioStreamID));
    result.status = AudioObjectGetPropertyData(device, &a, 0, nullptr, &bytes, result.ids.data());
    if (result.status != noErr || bytes % sizeof(AudioStreamID) != 0)
    {
        if (result.status == noErr) result.status = kAudioHardwareBadPropertySizeError;
        result.ids.clear();
    }
    else result.ids.resize(bytes / sizeof(AudioStreamID));
    return result;
}
enum class FormatSupport { supported, unsupported, unknown };
struct FloatFormat
{
    FormatSupport support = FormatSupport::unknown;
    juce::String description;
};
juce::String formatDescription(const AudioStreamBasicDescription& format)
{
    if (format.mFormatID == kAudioFormatLinearPCM)
        return juce::String(format.mBitsPerChannel) + ((format.mFormatFlags & kAudioFormatFlagIsFloat) != 0
            ? "bit float" : juce::String::fromUTF8("bit 整数"));
    if (format.mFormatID == kAudioFormatAC3 || format.mFormatID == kAudioFormat60958AC3) return "AC-3";
    char code[5] {};
    for (unsigned i = 0; i < 4; ++i)
    {
        const auto c = (format.mFormatID >> (24 - i * 8)) & 0xff;
        code[i] = c >= 32 && c <= 126 ? static_cast<char>(c) : '?';
    }
    return juce::String(code) + " (0x" + juce::String::toHexString(static_cast<int>(format.mFormatID)) + ")";
}
FloatFormat floatFormat(AudioObjectID device, bool input)
{
    const auto context = " device=" + stringProperty(device, kAudioObjectPropertyName)
                         + " scope=" + (input ? "input" : "output");
    const auto streams = deviceStreams(device, input);
    if (streams.status != noErr || streams.ids.empty())
    {
        audioLog("AUDIO virtual format unknown; " + juce::String(streams.status != noErr ? "stream list read failed" : "no streams")
                 + context + " status=" + juce::String(streams.status) + "; allowing open");
        return {};
    }
    FloatFormat result { FormatSupport::supported, {} };
    for (const auto stream : streams.ids)
    {
        auto a = address(kAudioStreamPropertyVirtualFormat);
        AudioStreamBasicDescription format {};
        UInt32 bytes = sizeof(format);
        const auto status = AudioObjectGetPropertyData(stream, &a, 0, nullptr, &bytes, &format);
        if (status != noErr || bytes != sizeof(format) || format.mFormatID == 0
            || (format.mFormatID == kAudioFormatLinearPCM && format.mBitsPerChannel == 0))
        {
            audioLog("AUDIO virtual format unknown; " + juce::String(status != noErr ? "format read failed" : "incomplete format")
                     + context + " stream=" + juce::String(stream) + " status=" + juce::String(status) + "; allowing open if no unsupported streams");
            if (result.support != FormatSupport::unsupported) result.support = FormatSupport::unknown;
            continue;
        }
        const auto description = formatDescription(format);
        audioLog("AUDIO virtual format" + context + " stream=" + juce::String(stream) + " format=" + description + " status=0");
        if (format.mFormatID != kAudioFormatLinearPCM || (format.mFormatFlags & kAudioFormatFlagIsFloat) == 0
            || format.mBitsPerChannel != 32)
        {
            result.support = FormatSupport::unsupported;
            if (result.description.isNotEmpty()) result.description += ", ";
            result.description += description;
        }
    }
    return result;
}
juce::String unsupportedFormat(const juce::String& route, const juce::String& name, bool input, const FloatFormat& format)
{
    return route + juce::String::fromUTF8("（") + name + juce::String::fromUTF8("）の")
           + juce::String::fromUTF8(input ? "入力" : "出力")
           + juce::String::fromUTF8("音声形式には対応していません（") + format.description + juce::String::fromUTF8("）");
}
void waitForMainStreams(AudioDeviceID device)
{
    for (unsigned elapsed = 0; ; elapsed += 20)
    {
        const auto input = deviceStreams(device, true), output = deviceStreams(device, false);
        if (!input.ids.empty() && !output.ids.empty()) return;
        if (elapsed >= 500)
        {
            audioLog("AUDIO aggregate streams not ready after 500ms device=" + stringProperty(device, kAudioObjectPropertyName)
                     + " scope=input count=" + juce::String(input.ids.size()) + " status=" + juce::String(input.status)
                     + " scope=output count=" + juce::String(output.ids.size()) + " status=" + juce::String(output.status) + "; continuing open");
            return;
        }
        juce::Thread::sleep(20);
    }
}
float readChannel(const AudioBufferList* list, unsigned channel, unsigned frame) noexcept
{
    if (list == nullptr) return 0.0f;
    for (UInt32 b = 0; b < list->mNumberBuffers; ++b)
    {
        const auto& buffer = list->mBuffers[b];
        if (channel < buffer.mNumberChannels)
        {
            const auto index = frame * buffer.mNumberChannels + channel;
            return buffer.mData != nullptr && (index + 1) * sizeof(float) <= buffer.mDataByteSize
                       ? static_cast<const float*>(buffer.mData)[index] : 0.0f;
        }
        channel -= buffer.mNumberChannels;
    }
    return 0.0f;
}
void writeChannel(AudioBufferList* list, unsigned channel, unsigned frame, float value) noexcept
{
    if (list == nullptr) return;
    for (UInt32 b = 0; b < list->mNumberBuffers; ++b)
    {
        auto& buffer = list->mBuffers[b];
        if (channel < buffer.mNumberChannels)
        {
            const auto index = frame * buffer.mNumberChannels + channel;
            if (buffer.mData != nullptr && (index + 1) * sizeof(float) <= buffer.mDataByteSize)
                static_cast<float*>(buffer.mData)[index] = value;
            return;
        }
        channel -= buffer.mNumberChannels;
    }
}
unsigned frameCount(const AudioBufferList* list) noexcept
{
    if (list == nullptr) return 0;
    for (UInt32 b = 0; b < list->mNumberBuffers; ++b)
        if (list->mBuffers[b].mNumberChannels > 0)
            return list->mBuffers[b].mDataByteSize / (sizeof(float) * list->mBuffers[b].mNumberChannels);
    return 0;
}
void clearOutput(AudioBufferList* output) noexcept
{
    if (output == nullptr) return;
    for (UInt32 b = 0; b < output->mNumberBuffers; ++b)
        if (output->mBuffers[b].mData != nullptr)
            std::memset(output->mBuffers[b].mData, 0, output->mBuffers[b].mDataByteSize);
}
bool disableInput(AudioDeviceID device, AudioDeviceIOProcID proc)
{
    auto a = address(kAudioDevicePropertyStreams, kAudioObjectPropertyScopeInput);
    UInt32 bytes = 0;
    if (AudioObjectGetPropertyDataSize(device, &a, 0, nullptr, &bytes) != noErr) return false;
    const auto count = bytes / sizeof(AudioStreamID);
    if (count == 0) return true;
    const auto size = offsetof(AudioHardwareIOProcStreamUsage, mStreamIsOn) + count * sizeof(UInt32);
    std::vector<uint64_t> storage((size + sizeof(uint64_t) - 1) / sizeof(uint64_t), 0);
    auto* usage = reinterpret_cast<AudioHardwareIOProcStreamUsage*>(storage.data());
    usage->mIOProc = reinterpret_cast<void*>(proc);
    usage->mNumberStreams = static_cast<UInt32>(count);
    a = address(kAudioDevicePropertyIOProcStreamUsage, kAudioObjectPropertyScopeInput);
    return AudioObjectSetPropertyData(device, &a, 0, nullptr, static_cast<UInt32>(size), usage) == noErr;
}
uint64_t ticksNow() noexcept { return static_cast<uint64_t>(juce::Time::getHighResolutionTicks()); }
}

struct AudioEngine::Impl
{
    explicit Impl(AudioEngine& e) : owner(e)
    {
        auto a = address(kAudioHardwarePropertyDevices);
        AudioObjectAddPropertyListener(kAudioObjectSystemObject, &a, changed, this);
        wakeObserver = [[[NSWorkspace sharedWorkspace] notificationCenter]
            addObserverForName:NSWorkspaceDidWakeNotification object:nil queue:nil
            usingBlock:^(NSNotification*) { dirty.store(true, std::memory_order_release); }];
    }
    ~Impl()
    {
        [[[NSWorkspace sharedWorkspace] notificationCenter] removeObserver:wakeObserver];
        auto a = address(kAudioHardwarePropertyDevices);
        AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &a, changed, this);
        stop();
    }
    struct Endpoint
    {
        Impl* engine = nullptr;
        AudioDeviceID device = 0;
        AudioDeviceIOProcID proc = nullptr;
        bool input = false, active = false;
        unsigned count = 0;
        double rate = 48000.0;
        ClockBridge bridge;
    };
    AudioEngine& owner;
    AudioDeviceID mainDevice = 0, aggregate = 0;
    AudioDeviceIOProcID mainProc = nullptr;
    bool running = false, prepared = false, diagnostic = false;
    std::atomic<bool> processing { false };
    std::atomic<bool> dirty { false }, inventoryDirty { false };
    unsigned retryTicks = 0;
    bool applyRequestedRate = true;
    std::atomic<uint64_t> lastCallback { 0 }, callbacks { 0 };
    std::atomic<float> maxInput { 0.0f }, averageLoad { 0.0f }, peakLoad { 0.0f };
    std::array<Stereo, Router::maximumBlock> instBlock {}, micBlock {}, bgmBlock {}, seBlock {};
    std::array<Router::Outputs, Router::maximumBlock> outputBlock {};
    std::array<bool, 5> available {};
    double rate = 48000.0, latency = 0.0;
    UInt32 buffer = 128;
    unsigned micCount = 0, monitorCount = 0, monitorOffset = 0;
    int micChoice = 1;
    Endpoint cable, stream, micMon;
    juce::String status;
    id wakeObserver = nil;
    struct Listener { AudioObjectID device; AudioObjectPropertyAddress a; };
    std::vector<Listener> listeners;

    static OSStatus changed(AudioObjectID object, UInt32, const AudioObjectPropertyAddress*, void* context)
    {
        auto& state = *static_cast<Impl*>(context);
        (object == kAudioObjectSystemObject ? state.inventoryDirty : state.dirty).store(true, std::memory_order_release);
        return noErr;
    }
    void watch(AudioDeviceID device)
    {
        for (auto selector : std::array<AudioObjectPropertySelector, 4> { kAudioDevicePropertyDeviceIsAlive, kAudioDevicePropertyNominalSampleRate,
                               kAudioDevicePropertyBufferFrameSize, kAudioDevicePropertyStreamConfiguration })
        {
            auto a = address(selector);
            if (selector == kAudioDevicePropertyStreamConfiguration)
            {
                for (auto scope : { kAudioObjectPropertyScopeInput, kAudioObjectPropertyScopeOutput })
                {
                    a.mScope = scope;
                    if (AudioObjectAddPropertyListener(device, &a, changed, this) == noErr) listeners.push_back({device, a});
                }
            }
            else if (AudioObjectAddPropertyListener(device, &a, changed, this) == noErr) listeners.push_back({device, a});
        }
    }
    void stopEndpoint(Endpoint& endpoint)
    {
        if (endpoint.proc != nullptr)
        {
            AudioDeviceStop(endpoint.device, endpoint.proc);
            AudioDeviceDestroyIOProcID(endpoint.device, endpoint.proc);
        }
        endpoint.proc = nullptr; endpoint.active = false; endpoint.device = 0;
    }
    void stop()
    {
        processing.store(false, std::memory_order_release);
        if (mainProc != nullptr)
        {
            AudioDeviceStop(mainDevice, mainProc);
            AudioDeviceDestroyIOProcID(mainDevice, mainProc);
        }
        mainProc = nullptr; running = false;
        owner.appCapture.setClock(rate, buffer, false);
        for (auto* e : { &cable, &stream, &micMon }) stopEndpoint(*e);
        for (const auto& l : listeners) AudioObjectRemovePropertyListener(l.device, &l.a, changed, this);
        listeners.clear();
        if (aggregate != 0) AudioHardwareDestroyAggregateDevice(aggregate);
        aggregate = 0; mainDevice = 0;
        owner.sender.close();
        available.fill(false);
    }
    const DeviceInfo* find(const juce::String& uid) const
    {
        for (const auto& device : owner.deviceList) if (device.uid == uid) return &device;
        return nullptr;
    }
    static OSStatus mainIO(AudioDeviceID, const AudioTimeStamp*, const AudioBufferList* input,
                           const AudioTimeStamp*, AudioBufferList* output, const AudioTimeStamp*, void* context)
    {
        static_cast<Impl*>(context)->process(input, output);
        return noErr;
    }
    void process(const AudioBufferList* input, AudioBufferList* output) noexcept
    {
        const auto began = ticksNow();
        clearOutput(output);
        if (!processing.load(std::memory_order_acquire)) return;
        const unsigned frames = std::max(frameCount(input), frameCount(output));
        const unsigned first = micChoice == 2 ? 1u : 0u;
        ProcessTapCapture::ReadScope appInput(owner.appCapture);
        for (unsigned offset = 0; offset < frames; offset += Router::maximumBlock)
        {
            const auto count = std::min(static_cast<unsigned>(Router::maximumBlock), frames - offset);
            for (unsigned f = 0; f < count; ++f)
            {
                const float l = first < micCount ? readChannel(input, first, offset + f) : 0.0f;
                const float r = micChoice == 3 && micCount > 1 ? readChannel(input, 1, offset + f) : l;
                instBlock[f] = cable.active ? cable.bridge.pull() : appInput.pull(); micBlock[f] = {l, r};
                const auto level = std::max(std::abs(l), std::abs(r));
                if (level > maxInput.load(std::memory_order_relaxed)) maxInput.store(level, std::memory_order_relaxed);
            }
            owner.sounds.render(bgmBlock.data(), seBlock.data(), static_cast<int>(count));
            owner.router.processBlock(instBlock.data(), micBlock.data(), outputBlock.data(), static_cast<int>(count),
                                      bgmBlock.data(), seBlock.data());
            for (unsigned f = 0; f < count; ++f)
            {
                const auto& out = outputBlock[f];
                if (monitorCount == 1) writeChannel(output, monitorOffset, offset + f, (out.monitor.l + out.monitor.r) * 0.5f);
                else if (monitorCount > 1)
                {
                    writeChannel(output, monitorOffset, offset + f, out.monitor.l);
                    writeChannel(output, monitorOffset + 1, offset + f, out.monitor.r);
                }
                if (stream.active) stream.bridge.push(out.stream);
                if (micMon.active) micMon.bridge.push(out.mic);
                owner.sender.push(out.stream.l, out.stream.r);
            }
        }
        const auto ended = ticksNow();
        const auto load = frames > 0 ? static_cast<float>(static_cast<double>(ended - began) * rate * 100.0
            / (static_cast<double>(juce::Time::getHighResolutionTicksPerSecond()) * frames)) : 0.0f;
        averageLoad.store(averageLoad.load(std::memory_order_relaxed) * 0.98f + load * 0.02f, std::memory_order_relaxed);
        auto peak = peakLoad.load(std::memory_order_relaxed);
        while (load > peak && !peakLoad.compare_exchange_weak(peak, load, std::memory_order_relaxed)) {}
        lastCallback.store(ended, std::memory_order_relaxed);
        callbacks.fetch_add(1, std::memory_order_relaxed);
    }
    static OSStatus endpointIO(AudioDeviceID, const AudioTimeStamp*, const AudioBufferList* input,
                               const AudioTimeStamp*, AudioBufferList* output, const AudioTimeStamp*, void* context)
    {
        auto& e = *static_cast<Endpoint*>(context);
        clearOutput(output);
        if (e.input)
        {
            const auto frames = frameCount(input);
            for (unsigned f = 0; f < frames; ++f)
            {
                const float l = readChannel(input, 0, f);
                e.bridge.push({l, e.count > 1 ? readChannel(input, 1, f) : l});
            }
        }
        else
        {
            const auto frames = frameCount(output);
            for (unsigned f = 0; f < frames; ++f)
            {
                const auto s = e.bridge.pull();
                writeChannel(output, 0, f, e.count == 1 ? (s.l + s.r) * 0.5f : s.l);
                if (e.count > 1) writeChannel(output, 1, f, s.r);
            }
        }
        return noErr;
    }
    bool openEndpoint(Endpoint& e, const juce::String& uid, bool input, const juce::String& route, juce::String& formatError)
    {
        const auto* d = find(uid);
        if (d == nullptr || (input ? d->inputs : d->outputs) == 0) return false;
        e.engine = this; e.device = d->id; e.input = input;
        e.count = input ? d->inputs : d->outputs;
        e.rate = property<Float64>(e.device, kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, 48000.0);
        const auto format = floatFormat(e.device, input);
        if (format.support == FormatSupport::unsupported)
        {
            const auto reason = unsupportedFormat(route, d->name, input, format);
            if (formatError.isNotEmpty()) formatError += "\n";
            formatError += reason;
            audioLog("AUDIO " + reason);
            return false;
        }
        const auto endpointBuffer = property<UInt32>(e.device, kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal, buffer);
        const auto sourceRate = input ? e.rate : rate;
        const auto destinationRate = input ? rate : e.rate;
        const auto sourceBuffer = input ? endpointBuffer : buffer;
        const auto outputBuffer = input ? buffer : endpointBuffer;
        const auto destinationBuffer = std::max(outputBuffer, static_cast<UInt32>(std::ceil(sourceBuffer * destinationRate / sourceRate)));
        e.bridge.prepare(sourceRate, destinationRate, destinationBuffer);
        auto result = AudioDeviceCreateIOProcID(e.device, endpointIO, &e, &e.proc);
        if (result == noErr && !input && !disableInput(e.device, e.proc)) result = kAudioHardwareIllegalOperationError;
        if (result == noErr) result = AudioDeviceStart(e.device, e.proc);
        if (result != noErr)
        {
            audioLog("AUDIO endpoint failed " + d->name + " status=" + juce::String(result));
            stopEndpoint(e);
            return false;
        }
        e.active = true;
        watch(e.device);
        audioLog("AUDIO endpoint started " + d->name + " rate=" + juce::String(e.rate));
        return true;
    }
    bool createMain(const DeviceInfo& mic, const DeviceInfo& monitor)
    {
        mainDevice = mic.id;
        monitorOffset = 0;
        if (mic.id != monitor.id)
        {
            NSString* micUID = [NSString stringWithUTF8String:mic.uid.toRawUTF8()];
            NSString* monitorUID = [NSString stringWithUTF8String:monitor.uid.toRawUTF8()];
            NSDictionary* description = @{
                @kAudioAggregateDeviceNameKey: @"Klinglab Stream Mixer Private",
                @kAudioAggregateDeviceUIDKey: [[NSUUID UUID] UUIDString],
                @kAudioAggregateDeviceIsPrivateKey: @YES,
                @kAudioAggregateDeviceIsStackedKey: @YES,
                @kAudioAggregateDeviceMasterSubDeviceKey: micUID,
                @kAudioAggregateDeviceSubDeviceListKey: @[
                    @{ @kAudioSubDeviceUIDKey: micUID, @kAudioSubDeviceDriftCompensationKey: @NO },
                    @{ @kAudioSubDeviceUIDKey: monitorUID, @kAudioSubDeviceDriftCompensationKey: @YES }
                ]
            };
            const auto result = AudioHardwareCreateAggregateDevice((__bridge CFDictionaryRef) description, &aggregate);
            if (result != noErr)
            {
                status = juce::String::fromUTF8("音声デバイスをまとめられませんでした（") + juce::String(result) + juce::String::fromUTF8("）");
                return false;
            }
            mainDevice = aggregate;
            monitorOffset = mic.outputs;
            audioLog("AUDIO private aggregate created; MIC clock master; monitor drift correction ON");
        }
        micCount = mic.inputs; monitorCount = monitor.outputs;
        micChoice = owner.selected.micChannels;
        return true;
    }
    juce::Result validateSelection(const DeviceSelection& s) const
    {
        for (const auto& uid : { s.micUID, s.monitorUID, s.streamUID, s.micMonUID,
                                 s.instMode == "CABLE" ? s.cableUID : juce::String() })
            if (uid.isNotEmpty() && find(uid) == nullptr)
                return juce::Result::fail(juce::String::fromUTF8("音声デバイスが見つかりません：") + uid);
        for (const auto& uid : { s.monitorUID, s.streamUID, s.micMonUID })
            if (const auto* d = find(uid); d != nullptr && d->outputs == 0)
                return juce::Result::fail(juce::String::fromUTF8("出力を開けません：") + d->name);
        for (const auto& uid : { s.micUID, s.instMode == "CABLE" ? s.cableUID : juce::String() })
            if (const auto* d = find(uid); d != nullptr && d->inputs == 0)
                return juce::Result::fail(juce::String::fromUTF8("入力を開けません：") + d->name);
        return juce::Result::ok();
    }
    enum class MainRoute { both, micOnly, monitorOnly };
    juce::Result open(bool strict = false, bool hold = false, MainRoute mainRoute = MainRoute::both)
    {
        stop();
        const auto& s = owner.selected;
        auto fail = [this](const juce::String& reason)
        {
            stop(); status = reason; return juce::Result::fail(reason);
        };
        if (strict)
            if (auto result = validateSelection(s); result.failed()) return fail(result.getErrorMessage());
        if (diagnostic)
        {
            rate = s.requestedRate > 0.0 ? s.requestedRate : 48000.0;
            buffer = static_cast<UInt32>(s.bufferFrames);
            owner.router.prepare(rate, prepared, hold); owner.sounds.prepare(rate); prepared = true;
            audioLog("AUDIO diagnostic retained bgm=" + juce::String(owner.sounds.bgmPositionSeconds(), 4)
                     + " loudLearning=" + juce::String(owner.router.loudness.isLearning() ? 1 : 0)
                     + " loudSeconds=" + juce::String(owner.router.loudness.learnSeconds(), 4)
                     + " loudDb=" + juce::String(owner.router.loudness.learnedDecibels.load(), 2)
                     + " nr=" + juce::JSON::toString(owner.router.nr.saveState(), true));
            for (const auto& voice : owner.sounds.seVoiceStates())
                if (voice.id != 0) audioLog("AUDIO diagnostic retained se=" + juce::String(voice.id) + " position=" + juce::String(voice.position, 4));
            if (s.micUID == "ksm-diagnostic-start-failure") return fail("Diagnostic device start failed");
            const auto tapResult = owner.appCapture.setClock(rate, buffer, s.instMode == "APP");
            if ((strict || hold) && tapResult.failed()) return fail(tapResult.getErrorMessage());
            running = true; processing.store(!hold, std::memory_order_release);
            status = "Diagnostic: hardware devices remain closed";
            audioLog("AUDIO diagnostic prepared rate=" + juce::String(rate) + " buffer=" + juce::String(buffer));
            return juce::Result::ok();
        }
        const auto* mic = find(s.micUID);
        const auto* monitor = find(s.monitorUID);
        const bool permitted = owner.microphoneAllowed();
        const bool haveMic = mainRoute != MainRoute::monitorOnly && mic != nullptr && mic->inputs > 0 && permitted;
        const bool haveMonitor = mainRoute != MainRoute::micOnly && monitor != nullptr && monitor->outputs > 0;
        if ((mainRoute == MainRoute::micOnly && !haveMic) || (mainRoute == MainRoute::monitorOnly && !haveMonitor))
            return fail(juce::String::fromUTF8(mainRoute == MainRoute::micOnly ? "MIC INPUT を再開できません。" : "MONITOR OUT を再開できません。"));
        if (haveMic)
        {
            const auto format = floatFormat(mic->id, true);
            if (format.support == FormatSupport::unsupported) return fail(unsupportedFormat("MIC INPUT", mic->name, true, format));
        }
        if (haveMonitor)
        {
            const auto format = floatFormat(monitor->id, false);
            if (format.support == FormatSupport::unsupported) return fail(unsupportedFormat("MONITOR OUT", monitor->name, false, format));
        }
        micCount = 0; monitorCount = 0; monitorOffset = 0;
        micChoice = s.micChannels;
        const DeviceInfo* clock = haveMic ? mic : haveMonitor ? monitor : find(s.streamUID);
        if (clock == nullptr || (!haveMic && !haveMonitor && clock->outputs == 0)) clock = find(s.micMonUID);
        if (clock == nullptr || (!haveMic && !haveMonitor && clock->outputs == 0))
        {
            return fail(juce::String::fromUTF8("MIC INPUT または出力デバイスを接続してください"));
        }
        if (s.requestedRate > 0.0 && haveMic && applyRequestedRate)
        {
            applyRequestedRate = false;
            auto a = address(kAudioDevicePropertyNominalSampleRate);
            Float64 requested = s.requestedRate;
            if (AudioObjectSetPropertyData(mic->id, &a, 0, nullptr, sizeof(requested), &requested) != noErr)
                audioLog("AUDIO sample rate request rejected; using MIC rate");
        }
        rate = property<Float64>(clock->id, kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, 48000.0);
        if (haveMic && haveMonitor)
        {
            if (!createMain(*mic, *monitor)) return fail(status);
            if (aggregate != 0) waitForMainStreams(aggregate);
        }
        else
        {
            mainDevice = clock->id;
            micCount = haveMic ? mic->inputs : 0;
            monitorCount = haveMonitor ? monitor->outputs : 0;
            audioLog("AUDIO fallback clock=" + clock->name + "; missing routes remain silent");
        }
        auto rateAddress = address(kAudioDevicePropertyNominalSampleRate);
        auto aggregateRate = rate;
        AudioObjectSetPropertyData(mainDevice, &rateAddress, 0, nullptr, sizeof(aggregateRate), &aggregateRate);
        auto bufferAddress = address(kAudioDevicePropertyBufferFrameSize);
        UInt32 desired = static_cast<UInt32>(s.bufferFrames);
        if (AudioObjectSetPropertyData(mainDevice, &bufferAddress, 0, nullptr, sizeof(desired), &desired) != noErr)
            audioLog("AUDIO buffer request rejected; using actual frame size");
        buffer = property<UInt32>(mainDevice, kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal, desired);
        rate = property<Float64>(mainDevice, kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, rate);
        if (strict && ((s.requestedRate > 0.0 && std::abs(rate - s.requestedRate) > 0.5) || buffer != desired))
            return fail(juce::String::fromUTF8("指定したサンプルレート・バッファサイズで開始できませんでした。"));
        if (haveMic)
        {
            const auto format = floatFormat(mainDevice, true);
            if (format.support == FormatSupport::unsupported) return fail(unsupportedFormat("MIC INPUT", mic->name, true, format));
        }
        if (haveMonitor || clock->outputs > 0)
        {
            const auto format = floatFormat(mainDevice, false);
            const auto route = haveMonitor ? "MONITOR OUT" : haveMic ? "MIC INPUT" : clock->uid == s.streamUID ? "STREAM OUT" : "MIC MON OUT";
            if (format.support == FormatSupport::unsupported)
                return fail(unsupportedFormat(route, haveMonitor ? monitor->name : clock->name, false, format));
        }
        if (strict && !permitted && (s.micUID.isNotEmpty() || (s.instMode == "CABLE" && s.cableUID.isNotEmpty())))
            return fail(juce::String::fromUTF8("マイクの許可がないため入力を開始できません。"));
        owner.router.prepare(rate, prepared, hold);
        owner.sounds.prepare(rate); prepared = true;
        if (!owner.sender.open(static_cast<uint32_t>(rate))) audioLog("SHM unavailable; device routing remains active");
        else audioLog("SHM connected " KSM_SHM_NAME " rate=" + juce::String(rate));
        juce::String endpointFormatError;
        available[0] = s.instMode == "CABLE" && permitted && openEndpoint(cable, s.cableUID, true, "INST INPUT (CABLE)", endpointFormatError);
        available[3] = openEndpoint(stream, s.streamUID, false, "STREAM OUT", endpointFormatError);
        available[4] = openEndpoint(micMon, s.micMonUID, false, "MIC MON OUT", endpointFormatError);
        if (strict && ((s.instMode == "CABLE" && s.cableUID.isNotEmpty() && !available[0])
                       || (s.streamUID.isNotEmpty() && !available[3]) || (s.micMonUID.isNotEmpty() && !available[4])))
            return fail(endpointFormatError.isNotEmpty() ? endpointFormatError : juce::String::fromUTF8("選んだ入出力デバイスを開始できませんでした。"));
        // Start silent callbacks first; voices and DSP advance only after commit.
        processing.store(!hold, std::memory_order_release);
        auto result = AudioDeviceCreateIOProcID(mainDevice, mainIO, this, &mainProc);
        if (result == noErr && !haveMic && !disableInput(mainDevice, mainProc)) result = kAudioHardwareIllegalOperationError;
        if (result == noErr) result = AudioDeviceStart(mainDevice, mainProc);
        if (result != noErr)
        {
            audioLog("AUDIO start failed status=" + juce::String(result));
            return fail(juce::String::fromUTF8("音声を開始できませんでした（") + juce::String(result) + juce::String::fromUTF8("）"));
        }
        running = true;
        const auto tapResult = owner.appCapture.setClock(rate, buffer, s.instMode == "APP");
        if (tapResult.failed())
        {
            if (strict || (hold && mainRoute == MainRoute::both)) return fail(tapResult.getErrorMessage());
            audioLog("AUDIO app capture unavailable; device routing remains active: " + tapResult.getErrorMessage());
        }
        available[1] = haveMic; available[2] = haveMonitor;
        lastCallback.store(ticksNow()); callbacks.store(0); maxInput.store(0.0f);
        if (haveMic) watch(mic->id);
        if (haveMonitor && (!haveMic || mic->id != monitor->id)) watch(monitor->id);
        watch(mainDevice);
        const auto inLatency = haveMic ? property<UInt32>(mic->id, kAudioDevicePropertyLatency, kAudioObjectPropertyScopeInput)
                             + property<UInt32>(mic->id, kAudioDevicePropertySafetyOffset, kAudioObjectPropertyScopeInput) : 0;
        const auto outLatency = haveMonitor ? property<UInt32>(monitor->id, kAudioDevicePropertyLatency, kAudioObjectPropertyScopeOutput)
                              + property<UInt32>(monitor->id, kAudioDevicePropertySafetyOffset, kAudioObjectPropertyScopeOutput) : 0;
        latency = (static_cast<double>(inLatency) + static_cast<double>(outLatency) + buffer * 2.0) / rate * 1000.0;
        status = !permitted ? juce::String::fromUTF8("マイクの許可待ちです。「マイクを許可」を押してください")
               : !haveMic ? juce::String::fromUTF8("MIC INPUT 未接続：声は無音です")
               : !haveMonitor ? juce::String::fromUTF8("MONITOR OUT 未接続：返しは無音です") : juce::String::fromUTF8("音声処理中");
        if (endpointFormatError.isNotEmpty()) status += "\n" + endpointFormatError;
        audioLog("AUDIO started MIC=" + (haveMic ? mic->name : "(silent)") + " MONITOR=" + (haveMonitor ? monitor->name : "(silent)") + " rate=" + juce::String(rate)
                 + " buffer=" + juce::String(buffer) + " RT=" + juce::String(latency, 2) + "ms");
        dirty.store(false);
        return juce::Result::ok();
    }
};

AudioEngine::AudioEngine() : impl(std::make_unique<Impl>(*this)) {}
AudioEngine::~AudioEngine() { stop(); }
bool AudioEngine::isRunning() const noexcept { return impl->running; }
bool AudioEngine::microphoneAllowed() const
{
    return [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio] == AVAuthorizationStatusAuthorized;
}
void AudioEngine::requestMicrophone()
{
    const auto auth = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
    if (auth == AVAuthorizationStatusNotDetermined)
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL) {}];
    else if (auth != AVAuthorizationStatusAuthorized)
    {
        NSURL* url = [NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_Microphone"];
        if (url != nil) [[NSWorkspace sharedWorkspace] openURL:url];
    }
    impl->dirty.store(true);
}
void AudioEngine::refresh()
{
    deviceList.clear();
    if (impl->diagnostic)
    {
        deviceList.push_back({ 0, "ksm-diagnostic-device", "Diagnostic device (no hardware)", 2, 2, 48000.0 });
        deviceList.push_back({ 0, "ksm-diagnostic-start-failure", "Diagnostic start failure (no hardware)", 2, 2, 44100.0 });
        return;
    }
    auto a = address(kAudioHardwarePropertyDevices);
    UInt32 bytes = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &a, 0, nullptr, &bytes) != noErr) return;
    std::vector<AudioDeviceID> ids(bytes / sizeof(AudioDeviceID));
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, nullptr, &bytes, ids.data()) != noErr) return;
    for (auto id : ids)
    {
        auto uid = stringProperty(id, kAudioDevicePropertyDeviceUID);
        auto name = stringProperty(id, kAudioObjectPropertyName);
        if (name == "Klinglab Stream Mixer Private" || name == "Klinglab Stream Mixer App Tap" || uid.isEmpty()) continue;
        const auto in = channels(id, true), out = channels(id, false);
        if (in + out == 0) continue;
        deviceList.push_back({ id, uid, name, in, out,
            property<Float64>(id, kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, 48000.0) });
    }
}
void AudioEngine::defaults()
{
    if (selected.micUID.isEmpty())
    {
        const auto id = property<AudioDeviceID>(kAudioObjectSystemObject, kAudioHardwarePropertyDefaultInputDevice);
        for (const auto& d : deviceList) if (d.id == id) selected.micUID = d.uid;
    }
    if (selected.monitorUID.isEmpty())
    {
        const auto id = property<AudioDeviceID>(kAudioObjectSystemObject, kAudioHardwarePropertyDefaultOutputDevice);
        for (const auto& d : deviceList) if (d.id == id) selected.monitorUID = d.uid;
    }
}
void AudioEngine::start()
{
    started = true;
    refresh(); defaults();
    audioLog("DEVICES count=" + juce::String(deviceList.size()));
    for (const auto& d : deviceList)
        audioLog("DEVICE " + d.name + " uid=" + d.uid + " in=" + juce::String(d.inputs)
                 + " out=" + juce::String(d.outputs) + " rate=" + juce::String(d.sampleRate));
    appCapture.onChange = [this] { if (onChange) onChange(); };
    appCapture.start();
    rebuild(); startTimer(1000);
}
void AudioEngine::startDiagnostic()
{
    impl->diagnostic = true; started = true;
    appCapture.startDiagnostic(); refresh();
    impl->open();
}
void AudioEngine::stop()
{
    stopTimer();
    if (impl != nullptr)
    {
        if (started) audioLog("AUDIO stop callbacks=" + juce::String(impl->callbacks.load())
                              + " micPeak=" + juce::String(impl->maxInput.load(), 6));
        impl->stop();
    }
    appCapture.onChange = nullptr;
    appCapture.stop();
    router.plugins.release(); sounds.release();
    started = false;
}
void AudioEngine::rebuild() { impl->open(); if (onChange) onChange(); }
juce::Result AudioEngine::selectDevices(const DeviceSelection& s, const CaptureSelection* capture,
                                         std::function<juce::Result()> commit)
{
    if (s.instMode == "CABLE" && s.cableUID.isNotEmpty() && s.cableUID == s.streamUID)
        return juce::Result::fail(juce::String::fromUTF8("INST INPUT と STREAM OUT に同じデバイスは選べません。音がループします。"));
    const auto old = selected;
    const double oldRate = impl->rate;
    const auto oldBuffer = impl->buffer;
    const auto oldCapture = appCapture.selection();
    const bool same = impl->running && selected.instMode == s.instMode && selected.cableUID == s.cableUID && selected.micUID == s.micUID
        && selected.monitorUID == s.monitorUID && selected.streamUID == s.streamUID && selected.micMonUID == s.micMonUID
        && selected.micChannels == s.micChannels && selected.bufferFrames == s.bufferFrames
        && std::abs(selected.requestedRate - s.requestedRate) < 0.5;
    if (!started)
    {
        selected = s;
        return capture != nullptr ? appCapture.select(*capture) : juce::Result::ok();
    }
    refresh();
    if (auto result = impl->validateSelection(s); result.failed()) return result;
    if (capture != nullptr)
        if (auto result = appCapture.validate(*capture); result.failed()) return result;
    juce::Result result = juce::Result::ok();
    if (!same)
    {
        impl->stop(); selected = s; impl->applyRequestedRate = true;
        if (capture != nullptr) result = appCapture.select(*capture);
        if (result.wasOk()) result = impl->open(true, true);
    }
    else
    {
        if (capture != nullptr) result = appCapture.select(*capture);
        if (result.wasOk()) result = appCapture.setClock(impl->rate, impl->buffer, s.instMode == "APP");
    }
    if (result.wasOk() && commit) result = commit();
    if (result.failed())
    {
        const auto reason = result.getErrorMessage();
        if (!same) impl->stop();
        selected = old;
        const auto captureResult = appCapture.select(oldCapture);
        impl->applyRequestedRate = true;
        // Restore the actual old format, then retain its original requested settings.
        if (!same)
        {
            selected.requestedRate = oldRate;
            selected.bufferFrames = static_cast<int>(oldBuffer);
        }
        const auto resumed = !same ? impl->open(true, true) : captureResult;
        if (resumed.failed())
        {
            bool recovered = false;
            if (!same)
            {
                for (const auto route : { Impl::MainRoute::micOnly, Impl::MainRoute::monitorOnly })
                {
                    const auto fallback = impl->open(false, true, route);
                    const auto routeName = route == Impl::MainRoute::micOnly ? "MIC INPUT" : "MONITOR OUT";
                    if (fallback.wasOk())
                    {
                        recovered = true;
                        result = juce::Result::fail(reason + "\n" + routeName
                            + juce::String::fromUTF8(" のみで再開しました。ほかの利用できない経路は無音です。"));
                        audioLog("AUDIO rollback recovered with " + juce::String(routeName) + " only");
                        break;
                    }
                    audioLog("AUDIO rollback fallback failed " + juce::String(routeName) + ": " + fallback.getErrorMessage());
                }
            }
            if (!recovered)
                result = juce::Result::fail(reason + juce::String::fromUTF8("\n旧デバイスの再開にも失敗しました：") + resumed.getErrorMessage());
        }
        selected = old;
        impl->status = result.getErrorMessage();
        audioLog("AUDIO selection rolled back: " + result.getErrorMessage());
    }
    impl->processing.store(true, std::memory_order_release);
    if (onChange) onChange();
    return result;
}
void AudioEngine::timerCallback()
{
    bool changed = impl->dirty.exchange(false);
    const bool inventoryChanged = impl->inventoryDirty.exchange(false);
    const bool retry = !impl->running && ++impl->retryTicks >= 5;
    if (impl->running || retry) impl->retryTicks = 0;
    const bool permissionChanged = (microphoneAllowed() && impl->status.contains(juce::String::fromUTF8("許可")))
                                   || (!microphoneAllowed() && impl->available[1]);
    if (impl->running)
    {
        const auto gap = static_cast<double>(ticksNow() - impl->lastCallback.load()) / static_cast<double>(juce::Time::getHighResolutionTicksPerSecond());
        if (gap > 3.0) changed = true;
    }
    if (changed || inventoryChanged || permissionChanged || retry)
    {
        const auto before = deviceList;
        refresh();
        bool listChanged = before.size() != deviceList.size();
        if (!listChanged)
            for (size_t i = 0; i < before.size(); ++i)
                if (before[i].uid != deviceList[i].uid || std::abs(before[i].sampleRate - deviceList[i].sampleRate) > 0.5
                    || before[i].inputs != deviceList[i].inputs || before[i].outputs != deviceList[i].outputs)
                    listChanged = true;
        if (changed || permissionChanged || listChanged || retry) rebuild();
    }
}
bool AudioEngine::routeAvailable(int row) const
{
    if (row < 0 || row >= 5) return false;
    if (row == 0 && selected.instMode == "APP") return appCapture.isActive();
    return impl->available[static_cast<size_t>(row)];
}
juce::String AudioEngine::statusText() const { return impl->status; }
double AudioEngine::roundTripMs() const noexcept { return impl->latency + router.plugins.latencySamples() * 1000.0 / impl->rate; }
float AudioEngine::averageLoad() const noexcept { return impl->averageLoad.load(std::memory_order_relaxed); }
float AudioEngine::maximumLoad() const noexcept { return impl->peakLoad.load(std::memory_order_relaxed); }
double AudioEngine::sampleRate() const noexcept { return impl->rate; }
int AudioEngine::bufferFrames() const noexcept { return static_cast<int>(impl->buffer); }
}
