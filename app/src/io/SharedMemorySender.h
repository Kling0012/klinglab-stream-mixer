#pragma once

#include "../../../../obs-plugin/include/ksm_shm.h"
#include <cstdint>

namespace ksm
{
class SharedMemorySender
{
public:
    SharedMemorySender();
    ~SharedMemorySender();
    bool open(uint32_t sampleRate);
    void close() noexcept;
    void push(float left, float right) noexcept;
    bool isConnected() const noexcept { return header != nullptr; }
private:
    struct ksm_shm_header* header = nullptr;
    float* samples = nullptr;
    uint64_t session = 0;
    uint64_t position = 0;
    int descriptor = -1;
};
}
