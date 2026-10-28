/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "ksm_shm.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stopping;

static void stop_signal(int signal_number)
{
    (void)signal_number;
    stopping = 1;
}

static uint64_t monotonic_ns(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}

static int read_stream(long seconds)
{
    int fd = shm_open(KSM_SHM_NAME, O_RDONLY, 0);
    if (fd < 0) { perror("shm_open reader"); return 1; }
    struct stat info;
    if (fstat(fd, &info) != 0 || info.st_size < (off_t)KSM_SHM_BYTES) { close(fd); return 1; }
    const struct ksm_shm_header *header = mmap(NULL, KSM_SHM_BYTES, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    if (header == MAP_FAILED) { perror("mmap reader"); return 1; }
    const float *ring = (const float *)((const uint8_t *)header + KSM_SHM_HEADER_BYTES);
    uint64_t deadline = monotonic_ns() + (uint64_t)seconds * UINT64_C(1000000000);
    uint64_t session = 0, previous = 0, received = 0;
    float peak = 0;
    while (monotonic_ns() < deadline) {
        uint64_t next_session = ksm_load_u64(&header->sessionId);
        uint64_t position = ksm_load_u64(&header->writePos);
        if (next_session && ksm_load_u32(&header->magic) == KSM_SHM_MAGIC &&
            ksm_load_u32(&header->version) == KSM_SHM_VERSION &&
            ksm_sample_rate_valid(ksm_load_u32(&header->sampleRate)) &&
            ksm_load_u32(&header->channels) == KSM_SHM_CHANNELS &&
            ksm_load_u32(&header->capacityFrames) == KSM_SHM_CAPACITY_FRAMES) {
            if (next_session != session || position < previous) {
                session = next_session; previous = position;
                printf("READER session=%" PRIu64 " magic=0x%08" PRIx32 " rate=%u channels=%u capacity=%u writePos=%" PRIu64 "\n",
                       session, ksm_load_u32(&header->magic), ksm_load_u32(&header->sampleRate),
                       ksm_load_u32(&header->channels), ksm_load_u32(&header->capacityFrames), position);
                fflush(stdout);
            } else if (position > previous) {
                uint64_t begin = previous;
                if (position - begin > KSM_SHM_CAPACITY_FRAMES) begin = position - KSM_SHM_CAPACITY_FRAMES;
                float block_peak = 0;
                for (uint64_t f = begin; f < position; ++f) {
                    size_t index = (f & (KSM_SHM_CAPACITY_FRAMES - 1u)) * KSM_SHM_CHANNELS;
                    for (size_t c = 0; c < KSM_SHM_CHANNELS; ++c) {
                        float value = fabsf(ksm_load_sample(&ring[index + c]));
                        if (value > block_peak) block_peak = value;
                    }
                }
                uint64_t after = ksm_load_u64(&header->writePos);
                if (ksm_load_u64(&header->sessionId) == session && after >= position && after - begin < KSM_SHM_CAPACITY_FRAMES) {
                    received += position - begin;
                    if (block_peak > peak) peak = block_peak;
                }
                previous = position;
            }
        }
        struct timespec pause = {.tv_sec = 0, .tv_nsec = 10000000};
        nanosleep(&pause, NULL);
    }
    printf("READER frames=%" PRIu64 " peak=%.6f lastWritePos=%" PRIu64 "\n", received, peak, previous);
    munmap((void *)header, KSM_SHM_BYTES);
    return received > 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
    int reading = argc == 3 && strcmp(argv[1], "--read") == 0;
    const char *duration = reading ? argv[2] : argc == 2 ? argv[1] : "";
    char *end = NULL;
    long seconds = strtol(duration, &end, 10);
    if ((!reading && argc != 2) || !duration[0] || !end || *end || seconds < 1 || seconds > 86400) {
        fprintf(stderr, "Usage: ksm-shm-test [--read] SECONDS (1..86400)\n");
        return 2;
    }
    if (reading) return read_stream(seconds);
    /* Retained lock file avoids competing diagnostic writers. No unlink: a
     * replacement lock inode would let a second writer bypass the lock. */
    char lock_path[128];
    snprintf(lock_path, sizeof(lock_path), "/tmp/klinglab-stream-mixer-writer-%lu.lock", (unsigned long)getuid());
    int lock_fd = open(lock_path, O_CREAT | O_RDWR | O_NOFOLLOW, 0600);
    struct flock lock = {.l_type = F_WRLCK, .l_whence = SEEK_SET};
    if (lock_fd < 0 || fcntl(lock_fd, F_SETLK, &lock) != 0) {
        fprintf(stderr, "Cannot acquire diagnostic writer lock\n");
        if (lock_fd >= 0)
            close(lock_fd);
        return 1;
    }
    int created = 1;
    int fd = shm_open(KSM_SHM_NAME, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0 && errno == EEXIST) {
        created = 0;
        fd = shm_open(KSM_SHM_NAME, O_RDWR, 0600);
    }
    if (fd < 0) {
        perror("shm_open");
        close(lock_fd);
        return 1;
    }
    struct stat status;
    if ((created && ftruncate(fd, (off_t)KSM_SHM_BYTES) != 0) || fstat(fd, &status) != 0 ||
        status.st_size < (off_t)KSM_SHM_BYTES) {
        fprintf(stderr, "Shared memory has an invalid size; refusing to resize an existing object\n");
        close(fd);
        close(lock_fd);
        return 1;
    }
    struct ksm_shm_header *header = mmap(NULL, KSM_SHM_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (header == MAP_FAILED) {
        perror("mmap");
        close(lock_fd);
        return 1;
    }
    if (created)
        memset(header, 0, KSM_SHM_BYTES);
    else if (ksm_load_u32(&header->magic) == 0 && ksm_load_u32(&header->version) == 0 &&
             ksm_load_u64(&header->sessionId) == 0 && ksm_load_u64(&header->writePos) == 0) {
        /* An interrupted first initialization can leave a zero-filled object. */
    } else if (ksm_load_u32(&header->magic) != KSM_SHM_MAGIC ||
               ksm_load_u32(&header->version) != KSM_SHM_VERSION) {
        fprintf(stderr, "Shared memory has an unknown format; refusing to overwrite\n");
        munmap(header, KSM_SHM_BYTES);
        close(lock_fd);
        return 1;
    } else {
        uint64_t before = ksm_load_u64(&header->writePos);
        struct timespec pause = {.tv_sec = 0, .tv_nsec = 100000000};
        nanosleep(&pause, NULL);
        if (ksm_load_u64(&header->writePos) != before) {
            fprintf(stderr, "A shared memory writer is active; refusing to overwrite\n");
            munmap(header, KSM_SHM_BYTES);
            close(lock_fd);
            return 1;
        }
    }
    uint64_t session = 0;
    while (!session)
        arc4random_buf(&session, sizeof(session));
    ksm_store_u64(&header->sessionId, 0);
    ksm_store_u64(&header->writePos, 0);
    ksm_store_u32(&header->version, KSM_SHM_VERSION);
    ksm_store_u32(&header->sampleRate, 48000);
    ksm_store_u32(&header->channels, KSM_SHM_CHANNELS);
    ksm_store_u32(&header->capacityFrames, KSM_SHM_CAPACITY_FRAMES);
    ksm_store_u32(&header->_pad, 0);
    ksm_store_u64(&header->sessionId, session);
    ksm_store_u32(&header->magic, KSM_SHM_MAGIC);
    float *ring = (float *)((uint8_t *)header + KSM_SHM_HEADER_BYTES);
    struct sigaction action = {0};
    action.sa_handler = stop_signal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    printf("440 Hz stereo, 48000 Hz, amplitude 0.1, seconds=%ld session=%" PRIu64 "\n", seconds, session);
    fflush(stdout);
    uint64_t start = monotonic_ns();
    uint64_t position = 0;
    uint64_t total = (uint64_t)seconds * 48000u;
    while (!stopping && position < total) {
        /* If another application takes ownership, only this tool stops. */
        if (ksm_load_u64(&header->sessionId) != session) {
            fprintf(stderr, "Shared memory ownership changed; stopping diagnostic writer\n");
            break;
        }
        uint64_t next = position + 480u;
        for (uint64_t frame = position; frame < next; ++frame) {
            float sample = 0.1f * (float)sin(6.2831853071795864769 * 440.0 * (double)frame / 48000.0);
            size_t index = (frame & (KSM_SHM_CAPACITY_FRAMES - 1u)) * KSM_SHM_CHANNELS;
            ksm_store_sample(&ring[index], sample);
            ksm_store_sample(&ring[index + 1], sample);
        }
        position = next;
        ksm_store_u64(&header->writePos, position);
        uint64_t deadline = start + position * UINT64_C(1000000000) / 48000u;
        uint64_t now = monotonic_ns();
        while (!stopping && deadline > now) {
            uint64_t wait = deadline - now;
            struct timespec pause = {.tv_sec = (time_t)(wait / UINT64_C(1000000000)),
                                     .tv_nsec = (long)(wait % UINT64_C(1000000000))};
            nanosleep(&pause, NULL);
            now = monotonic_ns();
        }
    }
    printf("Stopped: frames=%" PRIu64 "; shared memory retained for reconnect\n", position);
    munmap(header, KSM_SHM_BYTES);
    close(lock_fd);
    return 0;
}
