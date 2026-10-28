#include "SharedMemorySender.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <juce_core/juce_core.h>

namespace ksm
{
SharedMemorySender::SharedMemorySender() = default;
SharedMemorySender::~SharedMemorySender() { close(); }
bool SharedMemorySender::open(uint32_t rate)
{
    close();
    auto failed = [this](const char* operation)
    {
        const auto error = errno;
        juce::Logger::writeToLog(juce::String("SHM ") + operation + " failed errno=" + juce::String(error) + " " + juce::String::fromUTF8(std::strerror(error)));
        close();
        return false;
    };
    descriptor = shm_open(KSM_SHM_NAME, O_CREAT | O_RDWR, 0600);
    if (descriptor < 0) return failed("shm_open");
    struct stat info {};
    if (fstat(descriptor, &info) != 0) return failed("fstat");
    if (info.st_size == 0)
    {
        if (ftruncate(descriptor, static_cast<off_t>(KSM_SHM_BYTES)) != 0) return failed("ftruncate");
    }
    else if (info.st_size < static_cast<off_t>(KSM_SHM_BYTES))
    {
        juce::Logger::writeToLog("SHM incompatible mapping size; existing object left intact");
        close();
        return false;
    }
    auto* mapping = mmap(nullptr, KSM_SHM_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, descriptor, 0);
    if (mapping == MAP_FAILED) return failed("mmap");
    header = static_cast<struct ksm_shm_header*>(mapping);
    samples = reinterpret_cast<float*>(static_cast<char*>(mapping) + KSM_SHM_HEADER_BYTES);
    // A new session is published on every reopen, including clock/rate changes.
    uint64_t next = 0;
    do { arc4random_buf(&next, sizeof(next)); } while (next == 0 || next == session);
    session = next;
    ksm_store_u64(&header->sessionId, 0);
    position = 0;
    ksm_store_u64(&header->writePos, 0);
    ksm_store_u32(&header->version, KSM_SHM_VERSION);
    ksm_store_u32(&header->sampleRate, rate);
    ksm_store_u32(&header->channels, KSM_SHM_CHANNELS);
    ksm_store_u32(&header->capacityFrames, KSM_SHM_CAPACITY_FRAMES);
    ksm_store_u32(&header->_pad, 0);
    ksm_store_u64(&header->sessionId, session);
    ksm_store_u32(&header->magic, KSM_SHM_MAGIC);
    return true;
}
void SharedMemorySender::close() noexcept
{
    if (header != nullptr)
    {
        ksm_store_u64(&header->sessionId, 0);
        munmap(header, KSM_SHM_BYTES);
    }
    if (descriptor >= 0) ::close(descriptor);
    header = nullptr; samples = nullptr; descriptor = -1;
    // Keep the named mapping so already-connected readers can observe the next session.
}
void SharedMemorySender::push(float left, float right) noexcept
{
    if (header == nullptr) return;
    const auto offset = (position & (KSM_SHM_CAPACITY_FRAMES - 1)) * 2;
    ksm_store_sample(&samples[offset], left); ksm_store_sample(&samples[offset + 1], right);
    ksm_store_u64(&header->writePos, ++position);
}
}
