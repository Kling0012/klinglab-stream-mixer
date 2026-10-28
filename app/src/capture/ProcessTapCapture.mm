#include "ProcessTapCapture.h"
#import <CoreAudio/CoreAudio.h>
#import <CoreAudio/AudioHardwareTapping.h>
#import <CoreAudio/CATapDescription.h>
#import <AppKit/AppKit.h>
#include <libproc.h>
#include <unistd.h>
#include <algorithm>
#include <iostream>
#include <map>

namespace ksm
{
namespace
{
void captureLog(const juce::String& text)
{
    const auto line = juce::Time::getCurrentTime().toISO8601(true) + " " + text;
    juce::Logger::writeToLog(line); std::cout << line << std::endl;
}
AudioObjectPropertyAddress address(AudioObjectPropertySelector selector)
{
    return { selector, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
}
template<class T> T property(AudioObjectID object, AudioObjectPropertySelector selector, T fallback = {})
{
    auto a = address(selector); UInt32 size = sizeof(T); T value = fallback;
    return AudioObjectGetPropertyData(object, &a, 0, nullptr, &size, &value) == noErr ? value : fallback;
}
juce::String stringProperty(AudioObjectID object, AudioObjectPropertySelector selector)
{
    const auto value = property<CFStringRef>(object, selector);
    if (value == nullptr) return {};
    auto text = juce::String::fromCFString(value); CFRelease(value); return text;
}
juce::String string(NSString* text) { return text != nil ? juce::String::fromUTF8([text UTF8String]) : juce::String{}; }
NSURL* outerApp(NSURL* url)
{
    if (url == nil) return nil;
    NSString* path = @"/";
    for (NSString* part in [[url path] pathComponents])
    {
        path = [path stringByAppendingPathComponent:part];
        if ([[part pathExtension] isEqualToString:@"app"]) return [NSURL fileURLWithPath:path];
    }
    return nil;
}
juce::Image image(NSImage* source)
{
    if (source == nil) return {};
    NSImage* small = [source copy]; [small setSize:NSMakeSize(32, 32)];
    NSData* tiff = [small TIFFRepresentation];
    auto* bitmap = tiff != nil ? [NSBitmapImageRep imageRepWithData:tiff] : nil;
    NSData* data = [bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    auto result = data != nil ? juce::ImageFileFormat::loadFrom([data bytes], [data length]) : juce::Image{};
    [small release]; return result;
}
struct Identity { juce::String id, name; NSURL* url = nil; NSImage* icon = nil; };
Identity identify(pid_t pid, const juce::String& audioBundle)
{
    char path[PROC_PIDPATHINFO_MAXSIZE] {};
    proc_pidpath(pid, path, sizeof(path));
    NSRunningApplication* running = [NSRunningApplication runningApplicationWithProcessIdentifier:pid];
    NSURL* url = outerApp([running bundleURL]);
    NSString* executablePath = path[0] != 0 ? [NSString stringWithUTF8String:path] : nil;
    if (url == nil && executablePath != nil) url = outerApp([NSURL fileURLWithPath:executablePath]);
    // Helpers with no nested bundle path can still be related by their parent PID.
    auto parent = pid;
    for (int depth = 0; url == nil && parent > 1 && depth < 16; ++depth)
    {
        proc_bsdinfo info {};
        if (proc_pidinfo(parent, PROC_PIDTBSDINFO, 0, &info, sizeof(info)) != sizeof(info) || info.pbi_ppid == static_cast<uint32_t>(parent)) break;
        parent = static_cast<pid_t>(info.pbi_ppid);
        auto* ancestor = [NSRunningApplication runningApplicationWithProcessIdentifier:parent];
        url = outerApp([ancestor bundleURL]);
    }
    if (url != nil)
    {
        NSBundle* bundle = [NSBundle bundleWithURL:url];
        auto bundleID = string([bundle bundleIdentifier]);
        if (bundleID.isNotEmpty())
        {
            NSString* name = [[bundle infoDictionary] objectForKey:@"CFBundleDisplayName"];
            if (name == nil) name = [[bundle infoDictionary] objectForKey:@"CFBundleName"];
            if (name == nil) name = [[[url path] lastPathComponent] stringByDeletingPathExtension];
            NSString* identifier = [bundle bundleIdentifier];
            auto* mainApp = identifier != nil ? [[NSRunningApplication runningApplicationsWithBundleIdentifier:identifier] firstObject] : nil;
            return { bundleID, string(name), url, [mainApp icon] };
        }
    }
    auto name = string([running localizedName]);
    if (name.isEmpty()) name = juce::File(juce::String::fromUTF8(path)).getFileName();
    if (name.isEmpty()) name = audioBundle.isNotEmpty() ? audioBundle : "PID " + juce::String(pid);
    // CLI clients have no bundle ID; retain their executable identity across restarts.
    auto id = audioBundle.isNotEmpty() ? audioBundle : path[0] != 0 ? "executable:" + juce::String::fromUTF8(path) : juce::String{};
    return { id, name, nil, [running icon] };
}
unsigned frames(const AudioBufferList* list) noexcept
{
    if (list == nullptr) return 0;
    for (UInt32 b = 0; b < list->mNumberBuffers; ++b)
        if (list->mBuffers[b].mNumberChannels != 0)
            return list->mBuffers[b].mDataByteSize / (sizeof(float) * list->mBuffers[b].mNumberChannels);
    return 0;
}
float sample(const AudioBufferList* list, unsigned channel, unsigned frame) noexcept
{
    if (list == nullptr) return 0;
    for (UInt32 b = 0; b < list->mNumberBuffers; ++b)
    {
        const auto& buffer = list->mBuffers[b];
        if (channel < buffer.mNumberChannels)
        {
            const auto index = frame * buffer.mNumberChannels + channel;
            return buffer.mData != nullptr && (index + 1) * sizeof(float) <= buffer.mDataByteSize ? static_cast<const float*>(buffer.mData)[index] : 0;
        }
        channel -= buffer.mNumberChannels;
    }
    return 0;
}
}
juce::var CaptureSelection::toJson() const
{
    auto* object = new juce::DynamicObject(); juce::Array<juce::var> ids;
    for (const auto& id : bundleIDs) ids.add(id);
    object->setProperty("bundleIDs", ids); object->setProperty("muteDirect", muteDirect);
    return juce::var(object);
}
CaptureSelection CaptureSelection::fromJson(const juce::var& data)
{
    CaptureSelection result;
    if (const auto* ids = data["bundleIDs"].getArray())
        for (const auto& id : *ids) if (id.isString() && id.toString().isNotEmpty()) result.bundleIDs.addIfNotAlreadyThere(id.toString());
    if (data.hasProperty("muteDirect")) result.muteDirect = static_cast<bool>(data["muteDirect"]);
    return result;
}
struct ProcessTapCapture::Impl
{
    explicit Impl(ProcessTapCapture& capture) : owner(capture) {}
    struct Stream
    {
        AudioObjectID tap = 0;
        AudioDeviceID device = 0;
        AudioDeviceIOProcID proc = nullptr;
        ClockBridge bridge;
        unsigned channels = 2;
        std::atomic<uint64_t> callbacks { 0 };
        std::atomic<float> peak { 0 };
        void stop()
        {
            if (proc != nullptr) { AudioDeviceStop(device, proc); AudioDeviceDestroyIOProcID(device, proc); proc = nullptr; }
            if (device != 0) { AudioHardwareDestroyAggregateDevice(device); device = 0; }
            if (tap != 0) { AudioHardwareDestroyProcessTap(tap); tap = 0; }
        }
        ~Stream() { stop(); }
    };
    ProcessTapCapture& owner;
    std::vector<CaptureApp> apps;
    CaptureSelection selected;
    std::unique_ptr<Stream> stream;
    std::vector<std::unique_ptr<Stream>> retired;
    std::vector<AudioObjectID> connected;
    std::vector<AudioObjectID> watched;
    std::atomic<bool> dirty { true };
    bool started = false, enabled = false, permissionHelp = false, failed = false, diagnostic = false;
    std::map<pid_t, std::pair<juce::String, juce::String>> identityCache;
    double rate = 48000;
    unsigned buffer = 128;
    juce::String status = juce::String::fromUTF8("取り込むアプリを選んでください");
    static OSStatus changed(AudioObjectID, UInt32, const AudioObjectPropertyAddress*, void* context)
    {
        static_cast<Impl*>(context)->dirty.store(true, std::memory_order_release); return noErr;
    }
    void unwatch()
    {
        auto a = address(kAudioProcessPropertyIsRunningOutput);
        for (auto id : watched) AudioObjectRemovePropertyListener(id, &a, changed, this);
        watched.clear();
    }
    void enumerate()
    {
        @autoreleasepool
        {
            std::map<juce::String, CaptureApp> groups;
            std::vector<pid_t> livePIDs;
            auto identityFor = [&](pid_t pid, const juce::String& bundle)
            {
                livePIDs.push_back(pid);
                if (const auto cached = identityCache.find(pid); cached != identityCache.end())
                    return Identity { cached->second.first, cached->second.second, nil, nil };
                auto identity = identify(pid, bundle);
                identityCache[pid] = { identity.id, identity.name };
                return identity;
            };
            auto add = [&](const Identity& identity) -> CaptureApp*
            {
                if (identity.id.isEmpty() || identity.id == "site.klinglab.streammixer") return nullptr;
                auto [it, inserted] = groups.try_emplace(identity.id);
                if (inserted)
                {
                    it->second.bundleID = identity.id; it->second.name = identity.name;
                    const auto previous = std::find_if(apps.begin(), apps.end(), [&](const auto& app) { return app.bundleID == identity.id; });
                    it->second.icon = previous != apps.end() ? previous->icon : image(identity.icon);
                }
                return &it->second;
            };
            for (NSRunningApplication* app in [[NSWorkspace sharedWorkspace] runningApplications])
                if ([app activationPolicy] == NSApplicationActivationPolicyRegular && [app processIdentifier] != getpid())
                    add(identityFor([app processIdentifier], {}));
            auto a = address(kAudioHardwarePropertyProcessObjectList); UInt32 size = 0;
            std::vector<AudioObjectID> ids;
            if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &a, 0, nullptr, &size) == noErr && size > 0)
            {
                ids.resize(size / sizeof(AudioObjectID));
                if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, nullptr, &size, ids.data()) != noErr) ids.clear();
                else ids.resize(size / sizeof(AudioObjectID));
            }
            if (ids != watched)
            {
                unwatch(); auto running = address(kAudioProcessPropertyIsRunningOutput);
                for (auto id : ids)
                    if (AudioObjectAddPropertyListener(id, &running, changed, this) == noErr) watched.push_back(id);
            }
            for (auto id : ids)
            {
                const auto pid = property<pid_t>(id, kAudioProcessPropertyPID);
                if (pid <= 0 || pid == getpid()) continue;
                const auto identity = identityFor(pid, stringProperty(id, kAudioProcessPropertyBundleID));
                if (auto* app = add(identity))
                {
                    app->processes.push_back(id);
                    app->hasAudio |= property<UInt32>(id, kAudioProcessPropertyIsRunningOutput) != 0;
                }
            }
            std::erase_if(identityCache, [&](const auto& entry) { return std::find(livePIDs.begin(), livePIDs.end(), entry.first) == livePIDs.end(); });
            for (const auto& id : selected.bundleIDs)
                if (groups.find(id) == groups.end()) groups.emplace(id, CaptureApp { id, id, {}, {}, false });
            std::vector<CaptureApp> next;
            for (auto& [id, app] : groups)
            {
                std::sort(app.processes.begin(), app.processes.end()); next.push_back(std::move(app));
            }
            std::sort(next.begin(), next.end(), [](const auto& a1, const auto& b) { return a1.name.compareIgnoreCase(b.name) < 0; });
            bool different = next.size() != apps.size();
            if (!different)
                for (size_t i = 0; i < next.size(); ++i)
                    if (next[i].bundleID != apps[i].bundleID || next[i].processes != apps[i].processes || next[i].hasAudio != apps[i].hasAudio || next[i].name != apps[i].name) different = true;
            apps = std::move(next);
            if (different)
            {
                captureLog("APP inventory apps=" + juce::String(apps.size()) + " processes=" + juce::String(ids.size()));
                for (const auto& app : apps) captureLog("APP " + app.name + " bundle=" + app.bundleID + " processes=" + juce::String(app.processes.size()) + " outputRunning=" + juce::String(app.hasAudio ? 1 : 0));
                if (owner.onChange) owner.onChange();
            }
        }
    }
    std::vector<AudioObjectID> targets(const CaptureSelection& selection) const
    {
        std::vector<AudioObjectID> result;
        for (const auto& app : apps)
            if (selection.bundleIDs.contains(app.bundleID)) result.insert(result.end(), app.processes.begin(), app.processes.end());
        std::sort(result.begin(), result.end()); result.erase(std::unique(result.begin(), result.end()), result.end());
        return result;
    }
    void stopStream()
    {
        owner.readable.store(nullptr);
        if (stream != nullptr)
        {
            stream->stop();
            captureLog("TAP stopped callbacks=" + juce::String(stream->callbacks.load()) + " peak=" + juce::String(stream->peak.load(), 6));
            retired.push_back(std::move(stream));
        }
        if (owner.readers.load() == 0) retired.clear();
    }
    static OSStatus io(AudioDeviceID, const AudioTimeStamp*, const AudioBufferList* input, const AudioTimeStamp*, AudioBufferList* output, const AudioTimeStamp*, void* context)
    {
        auto& s = *static_cast<Stream*>(context);
        if (output != nullptr)
            for (UInt32 b = 0; b < output->mNumberBuffers; ++b)
                if (output->mBuffers[b].mData != nullptr) std::memset(output->mBuffers[b].mData, 0, output->mBuffers[b].mDataByteSize);
        float peak = s.peak.load(std::memory_order_relaxed);
        for (unsigned f = 0; f < frames(input); ++f)
        {
            const auto l = sample(input, 0, f), r = s.channels > 1 ? sample(input, 1, f) : l;
            s.bridge.push({l, r}); peak = std::max(peak, std::max(std::abs(l), std::abs(r)));
        }
        s.peak.store(peak, std::memory_order_relaxed); s.callbacks.fetch_add(1, std::memory_order_relaxed);
        return noErr;
    }
    void reconnect(bool force = false)
    {
        if (enabled && failed) return;
        if (diagnostic)
        {
            failed = enabled && selected.bundleIDs.contains("ksm.diagnostic.tap-failure");
            status = failed ? "Diagnostic Process Tap reconnect failed" : "Diagnostic: Process Tap remains closed";
            return;
        }
        auto ids = targets(selected);
        if (!force && ids == connected) return;
        stopStream(); connected = ids;
        auto notify = [this] { if (owner.onChange) owner.onChange(); };
        if (!enabled || selected.bundleIDs.isEmpty() || ids.empty())
        {
            status = !enabled ? juce::String::fromUTF8("APP 入力は停止中です") : selected.bundleIDs.isEmpty()
                ? juce::String::fromUTF8("取り込むアプリを選んでください") : juce::String::fromUTF8("選んだアプリの起動・音声接続を待っています");
            notify(); return;
        }
        if (ids.size() > 16 || selected.bundleIDs.size() > 8)
        {
            failed = true;
            status = juce::String::fromUTF8("APP 入力を停止しました。最大8アプリ・合計16プロセスに選び直してください"); notify(); return;
        }
        @autoreleasepool
        {
            auto next = std::make_unique<Stream>();
            auto fail = [&](const char* operation, OSStatus result)
            {
                failed = true; permissionHelp = true;
                status = juce::String::fromUTF8("アプリ音声を取り込めません。システムオーディオ録音の許可を確認してください（") + juce::String(result) + juce::String::fromUTF8("）");
                captureLog(juce::String("TAP ") + operation + " failed status=" + juce::String(result)); notify();
            };
            NSMutableArray<NSNumber*>* processes = [NSMutableArray array];
            for (auto id : ids) [processes addObject:@(id)];
            CATapDescription* description = [[CATapDescription alloc] initStereoMixdownOfProcesses:processes];
            [description setPrivate:YES]; [description setName:@"Klinglab Stream Mixer Apps"];
            [description setMuteBehavior:selected.muteDirect ? CATapMutedWhenTapped : CATapUnmuted];
            auto result = AudioHardwareCreateProcessTap(description, &next->tap);
            [description release];
            if (result != noErr) { fail("create", result); return; }
            captureLog("TAP created id=" + juce::String(next->tap) + " processes=" + juce::String(ids.size()) + " muteDirect=" + juce::String(selected.muteDirect ? 1 : 0));
            const auto format = property<AudioStreamBasicDescription>(next->tap, kAudioTapPropertyFormat);
            if (format.mFormatID != kAudioFormatLinearPCM || !(format.mFormatFlags & kAudioFormatFlagIsFloat) || format.mBitsPerChannel != 32 || format.mSampleRate <= 0 || format.mChannelsPerFrame == 0)
            { fail("format", kAudioHardwareUnsupportedOperationError); return; }
            const auto uid = stringProperty(next->tap, kAudioTapPropertyUID);
            NSString* tapUID = [NSString stringWithUTF8String:uid.toRawUTF8()];
            if (uid.isEmpty() || tapUID == nil) { fail("uid", kAudioHardwareBadObjectError); return; }
            NSDictionary* aggregate = @{
                @kAudioAggregateDeviceNameKey: @"Klinglab Stream Mixer App Tap",
                @kAudioAggregateDeviceUIDKey: [[NSUUID UUID] UUIDString],
                @kAudioAggregateDeviceIsPrivateKey: @YES,
                @kAudioAggregateDeviceTapAutoStartKey: @NO,
                @kAudioAggregateDeviceTapListKey: @[@{ @kAudioSubTapUIDKey: tapUID, @kAudioSubTapDriftCompensationKey: @YES }]
            };
            result = AudioHardwareCreateAggregateDevice((__bridge CFDictionaryRef)aggregate, &next->device);
            if (result != noErr) { fail("aggregate", result); return; }
            captureLog("TAP private aggregate created id=" + juce::String(next->device) + " rate=" + juce::String(format.mSampleRate));
            auto bufferAddress = address(kAudioDevicePropertyBufferFrameSize); UInt32 desired = buffer;
            AudioObjectSetPropertyData(next->device, &bufferAddress, 0, nullptr, sizeof(desired), &desired);
            auto actualBuffer = property<UInt32>(next->device, kAudioDevicePropertyBufferFrameSize, buffer);
            next->channels = format.mChannelsPerFrame;
            next->bridge.prepare(format.mSampleRate, rate, std::max(buffer, static_cast<unsigned>(std::ceil(actualBuffer * rate / format.mSampleRate))));
            result = AudioDeviceCreateIOProcID(next->device, io, next.get(), &next->proc);
            if (result == noErr) result = AudioDeviceStart(next->device, next->proc);
            if (result != noErr) { fail("start", result); return; }
            stream = std::move(next); owner.readable.store(&stream->bridge);
            permissionHelp = false;
            status = juce::String::fromUTF8("APP 接続中：") + juce::String(selected.bundleIDs.size()) + juce::String::fromUTF8("アプリ / ") + juce::String(ids.size()) + juce::String::fromUTF8("プロセス");
            captureLog("TAP started bridge=" + juce::String(format.mSampleRate) + "->" + juce::String(rate)); notify();
        }
    }
};
ProcessTapCapture::ProcessTapCapture() : impl(std::make_unique<Impl>(*this)) {}
ProcessTapCapture::~ProcessTapCapture() { stop(); }
void ProcessTapCapture::start()
{
    if (impl->started) return;
    impl->started = true; auto a = address(kAudioHardwarePropertyProcessObjectList);
    AudioObjectAddPropertyListener(kAudioObjectSystemObject, &a, Impl::changed, impl.get());
    impl->enumerate(); impl->reconnect(true); startTimer(1000);
}
void ProcessTapCapture::startDiagnostic()
{
    impl->diagnostic = true;
}
void ProcessTapCapture::stop()
{
    stopTimer();
    if (impl->started)
    {
        auto a = address(kAudioHardwarePropertyProcessObjectList);
        AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &a, Impl::changed, impl.get()); impl->unwatch();
    }
    impl->started = false; impl->stopStream();
}
juce::Result ProcessTapCapture::setClock(double rate, unsigned buffer, bool enabled)
{
    if (std::abs(impl->rate - rate) >= 0.5 || impl->buffer != buffer || impl->enabled != enabled)
    {
        impl->rate = rate; impl->buffer = buffer; impl->enabled = enabled;
        if (impl->started || impl->diagnostic) impl->reconnect(true);
    }
    return enabled && impl->failed ? juce::Result::fail(impl->status) : juce::Result::ok();
}
const std::vector<CaptureApp>& ProcessTapCapture::apps() const noexcept { return impl->apps; }
const CaptureSelection& ProcessTapCapture::selection() const noexcept { return impl->selected; }
juce::Result ProcessTapCapture::validate(const CaptureSelection& selection) const
{
    if (selection.bundleIDs.size() > 8) return juce::Result::fail(juce::String::fromUTF8("選べるアプリは最大8個です。チェックを減らしてください。"));
    const auto count = impl->targets(selection).size();
    if (count > 16) return juce::Result::fail(juce::String::fromUTF8("合計16プロセスまで取り込めます。現在 ") + juce::String(count) + juce::String::fromUTF8("プロセスです。アプリのチェックを減らしてください。"));
    return juce::Result::ok();
}
juce::Result ProcessTapCapture::select(const CaptureSelection& selection)
{
    if (auto result = validate(selection); result.failed()) return result;
    if (impl->selected.bundleIDs == selection.bundleIDs && impl->selected.muteDirect == selection.muteDirect && !impl->failed)
        return juce::Result::ok();
    impl->selected = selection;
    impl->failed = false; impl->permissionHelp = false;
    if (impl->started) impl->enumerate();
    if (impl->started || impl->diagnostic) impl->reconnect(true);
    return impl->enabled && impl->failed ? juce::Result::fail(impl->status) : juce::Result::ok();
}
bool ProcessTapCapture::isActive() const noexcept { return impl->stream != nullptr; }
juce::String ProcessTapCapture::statusText() const
{
    if (impl->stream != nullptr && impl->stream->peak.load(std::memory_order_relaxed) <= 0.0f)
        return impl->status + juce::String::fromUTF8(" / 入力音声をまだ検出していません。音が出ている場合は録音の許可を確認してください");
    return impl->status;
}
bool ProcessTapCapture::needsPermissionHelp() const noexcept { return impl->permissionHelp; }
void ProcessTapCapture::retryPermission()
{
    impl->failed = false; impl->permissionHelp = false;
    impl->enumerate(); impl->reconnect(true);
}
void ProcessTapCapture::openPrivacySettings()
{
    NSURL* url = [NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_AudioCapture"];
    if (url != nil) [[NSWorkspace sharedWorkspace] openURL:url];
}
void ProcessTapCapture::timerCallback()
{
    const bool changed = impl->dirty.exchange(false);
    impl->enumerate();
    // Never repeatedly request permission after a failure. Retry is an explicit user action.
    if (!impl->failed) impl->reconnect();
    if (readers.load() == 0) impl->retired.clear();
    if (changed && onChange) onChange();
}
ProcessTapCapture::ReadScope::ReadScope(ProcessTapCapture& capture) noexcept : owner(capture)
{
    owner.readers.fetch_add(1); bridge = owner.readable.load();
}
ProcessTapCapture::ReadScope::~ReadScope() { owner.readers.fetch_sub(1); }
}
