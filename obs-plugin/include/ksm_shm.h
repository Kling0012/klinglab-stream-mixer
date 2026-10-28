/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef KSM_SHM_H
#define KSM_SHM_H

#include <stddef.h>
#include <stdint.h>

#define KSM_SHM_NAME "/klinglab-stream-mixer-v1"
/* Little-endian bytes K S M 1 on both supported macOS architectures. */
#define KSM_SHM_MAGIC UINT32_C(0x314d534b)
#define KSM_SHM_VERSION UINT32_C(1)
#define KSM_SHM_HEADER_BYTES 4096u
#define KSM_SHM_CHANNELS 2u
#define KSM_SHM_CAPACITY_FRAMES 131072u
#define KSM_SHM_BYTES (KSM_SHM_HEADER_BYTES + KSM_SHM_CAPACITY_FRAMES * KSM_SHM_CHANNELS * sizeof(float))

/* Single writer, any number of readers. Do not pack this structure.
 * Keep the object at KSM_SHM_BYTES for its entire lifetime: never truncate a
 * mapped object. Reuse it on restart with a new sessionId. Do not unlink it
 * while a writer is active. Consumers open O_RDONLY and map PROT_READ.
 *
 * C and C++ share plain, naturally aligned fields and Clang/GCC __atomic
 * operations, rather than depending on the ABI of either language's atomic
 * types. Every concurrent access must use these helpers (including samples).
 * A writer invalidates sessionId with a release store of zero, resets writePos
 * and format, then publishes a fresh nonzero sessionId with a release store.
 * It writes samples before publishing writePos with a release store. Readers
 * acquire both counters and recheck them after copying a block. Format fields
 * are immutable during one session. A new object publishes magic last.
 */
struct ksm_shm_header {
    uint32_t magic;          /* KSM_SHM_MAGIC; zero until initialization finishes. */
    uint32_t version;        /* Protocol version; other layouts need a new name. */
    uint32_t sampleRate;     /* Frames/second: 44100, 48000, or 96000. */
    uint32_t channels;       /* Always 2 (left, right). */
    uint32_t capacityFrames; /* Always 131072; ring index = position & (capacity-1). */
    uint32_t _pad;           /* Reserved, zero; aligns the 64-bit counters. */
    uint64_t writePos;       /* Atomic count of fully published frames, not samples. */
    uint64_t sessionId;      /* Atomic random nonzero identifier for each writer run. */
    uint8_t reserved[KSM_SHM_HEADER_BYTES - 40u]; /* Reserved, zero on creation. */
};
/* Ring follows at byte 4096: float32 L,R,L,R...; not part of the header. */

#if defined(__cplusplus)
#define KSM_STATIC_ASSERT static_assert
#else
#define KSM_STATIC_ASSERT _Static_assert
#endif
KSM_STATIC_ASSERT(sizeof(struct ksm_shm_header) == KSM_SHM_HEADER_BYTES, "header size");
KSM_STATIC_ASSERT(offsetof(struct ksm_shm_header, writePos) == 24, "writePos offset");
KSM_STATIC_ASSERT(offsetof(struct ksm_shm_header, sessionId) == 32, "sessionId offset");
KSM_STATIC_ASSERT(sizeof(float) == 4, "float32 required");
KSM_STATIC_ASSERT(__atomic_always_lock_free(8, 0), "lock-free uint64 required");
KSM_STATIC_ASSERT(__atomic_always_lock_free(4, 0), "lock-free samples required");
#undef KSM_STATIC_ASSERT

static inline uint32_t ksm_load_u32(const uint32_t *value)
{
    return __atomic_load_n(value, __ATOMIC_ACQUIRE);
}
static inline uint64_t ksm_load_u64(const uint64_t *value)
{
    return __atomic_load_n(value, __ATOMIC_ACQUIRE);
}
static inline void ksm_store_u32(uint32_t *value, uint32_t next)
{
    __atomic_store_n(value, next, __ATOMIC_RELEASE);
}
static inline void ksm_store_u64(uint64_t *value, uint64_t next)
{
    __atomic_store_n(value, next, __ATOMIC_RELEASE);
}
static inline float ksm_load_sample(const float *value)
{
    float result;
    __atomic_load(value, &result, __ATOMIC_RELAXED);
    return result;
}
static inline void ksm_store_sample(float *value, float next)
{
    __atomic_store(value, &next, __ATOMIC_RELAXED);
}
static inline int ksm_sample_rate_valid(uint32_t rate)
{
    return rate == 44100 || rate == 48000 || rate == 96000;
}

#endif
