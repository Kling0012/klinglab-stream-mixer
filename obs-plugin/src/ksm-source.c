/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <obs-module.h>
#include <util/platform.h>

#include "ksm_shm.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

OBS_DECLARE_MODULE()

#define KSM_SOURCE_ID "klinglab-stream-mixer-source"
#define NS_PER_TICK UINT64_C(10000000)
#define NS_PER_SECOND UINT64_C(1000000000)
#define MAX_BLOCK_FRAMES 960u

struct ksm_source {
    obs_source_t *source;
    pthread_t thread;
    bool stop;
    const struct ksm_shm_header *header;
    const float *ring;
    uint64_t session;
    uint64_t read_pos;
    uint64_t last_write_pos;
    uint32_t rate;
    uint64_t audio_frames;
    uint64_t silent_frames;
    uint64_t resyncs;
    bool streaming;
    float block[MAX_BLOCK_FRAMES * KSM_SHM_CHANNELS];
};

static bool header_valid(const struct ksm_shm_header *header)
{
    return ksm_load_u32(&header->magic) == KSM_SHM_MAGIC &&
           ksm_load_u32(&header->version) == KSM_SHM_VERSION &&
           ksm_sample_rate_valid(ksm_load_u32(&header->sampleRate)) &&
           ksm_load_u32(&header->channels) == KSM_SHM_CHANNELS &&
           ksm_load_u32(&header->capacityFrames) == KSM_SHM_CAPACITY_FRAMES;
}

static void disconnect_shm(struct ksm_source *state)
{
    if (state->header)
        munmap((void *)state->header, KSM_SHM_BYTES);
    state->header = NULL;
    state->ring = NULL;
    state->session = 0;
    state->streaming = false;
}

static void resync(struct ksm_source *state, uint64_t session, uint64_t position)
{
    state->session = session;
    state->rate = ksm_load_u32(&state->header->sampleRate);
    state->read_pos = position;
    state->last_write_pos = position;
    state->resyncs++;
    blog(LOG_INFO, "[KSM] resync session=%" PRIu64 " writePos=%" PRIu64 " rate=%u",
         session, position, state->rate);
}

/* Also probe while connected: an unlinked/recreated object has a new mapping,
 * while existing readers may still hold the old one. Never modify the writer. */
static void reconnect_shm(struct ksm_source *state)
{
    int fd = shm_open(KSM_SHM_NAME, O_RDONLY, 0);
    if (fd < 0)
        return;
    struct stat status;
    if (fstat(fd, &status) != 0 || status.st_size < (off_t)KSM_SHM_BYTES) {
        close(fd);
        return;
    }
    const struct ksm_shm_header *header = mmap(NULL, KSM_SHM_BYTES, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    if (header == MAP_FAILED)
        return;
    uint64_t session = ksm_load_u64(&header->sessionId);
    if (!header_valid(header) || !session ||
        (state->header && session == state->session)) {
        munmap((void *)header, KSM_SHM_BYTES);
        return;
    }
    disconnect_shm(state);
    state->header = header;
    state->ring = (const float *)((const uint8_t *)header + KSM_SHM_HEADER_BYTES);
    resync(state, session, ksm_load_u64(&header->writePos));
    blog(LOG_INFO, "[KSM] connected to %s (read-only)", KSM_SHM_NAME);
}

static bool read_audio(struct ksm_source *state, uint32_t frames)
{
    const struct ksm_shm_header *header = state->header;
    if (!header)
        return false;
    if (!header_valid(header)) {
        disconnect_shm(state);
        return false;
    }
    uint64_t session = ksm_load_u64(&header->sessionId);
    uint64_t written = ksm_load_u64(&header->writePos);
    if (!session)
        return false;
    if (session != state->session || written < state->last_write_pos ||
        ksm_load_u32(&header->sampleRate) != state->rate) {
        resync(state, session, written);
        return false;
    }
    state->last_write_pos = written;
    if (written < state->read_pos || written - state->read_pos > state->rate / 10u) {
        resync(state, session, written);
        return false;
    }
    if (written - state->read_pos < frames)
        return false;

    for (uint32_t frame = 0; frame < frames; ++frame) {
        size_t index = ((state->read_pos + frame) & (KSM_SHM_CAPACITY_FRAMES - 1u)) * KSM_SHM_CHANNELS;
        for (uint32_t channel = 0; channel < KSM_SHM_CHANNELS; ++channel) {
            float sample = ksm_load_sample(&state->ring[index + channel]);
            state->block[frame * KSM_SHM_CHANNELS + channel] = isfinite(sample) ? sample : 0.0f;
        }
    }
    /* Drop a block if the writer restarted or lapped us during the copy. */
    uint64_t after = ksm_load_u64(&header->writePos);
    if (ksm_load_u64(&header->sessionId) != session || after < written ||
        after - state->read_pos >= KSM_SHM_CAPACITY_FRAMES)
        return false;
    state->read_pos += frames;
    return true;
}

static void *audio_thread(void *opaque)
{
    struct ksm_source *state = opaque;
    uint64_t next_tick = os_gettime_ns();
    uint64_t next_connect = 0;
    uint64_t timestamp = next_tick;
    while (!__atomic_load_n(&state->stop, __ATOMIC_ACQUIRE)) {
        uint64_t now = os_gettime_ns();
        if (now >= next_connect) {
            reconnect_shm(state);
            next_connect = now + NS_PER_SECOND;
        }
        uint32_t rate = state->rate;
        uint32_t frames = rate / 100u;
        bool received = read_audio(state, frames);
        /* A format change is applied on the next tick, with matching silence. */
        if (!received)
            memset(state->block, 0, frames * KSM_SHM_CHANNELS * sizeof(float));
        if (received) {
            state->audio_frames += frames;
            if (!state->streaming)
                blog(LOG_INFO, "[KSM] receiving audio (%u Hz, stereo float32)", rate);
        } else {
            state->silent_frames += frames;
            if (state->streaming)
                blog(LOG_INFO, "[KSM] waiting for audio; output is silent");
        }
        state->streaming = received;
        struct obs_source_audio audio = {0};
        audio.data[0] = (const uint8_t *)state->block;
        audio.frames = frames;
        audio.speakers = SPEAKERS_STEREO;
        audio.format = AUDIO_FORMAT_FLOAT;
        audio.samples_per_sec = rate;
        audio.timestamp = timestamp;
        obs_source_output_audio(state->source, &audio);
        timestamp += NS_PER_TICK;
        next_tick += NS_PER_TICK;
        now = os_gettime_ns();
        if (now > next_tick + NS_PER_TICK) {
            /* Never burst old audio after sleep or a delayed scheduler tick. */
            next_tick = now;
            timestamp = now;
            if (state->header)
                state->read_pos = ksm_load_u64(&state->header->writePos);
        }
        now = os_gettime_ns();
        if (next_tick > now) {
            uint64_t wait = next_tick - now;
            struct timespec duration = {.tv_sec = 0, .tv_nsec = (long)wait};
            while (nanosleep(&duration, &duration) != 0 && errno == EINTR) {}
        }
    }
    disconnect_shm(state);
    return NULL;
}

static const char *source_name(void *unused)
{
    (void)unused;
    return "Klinglab Stream Mixer";
}

static void *source_create(obs_data_t *settings, obs_source_t *source)
{
    (void)settings;
    struct ksm_source *state = calloc(1, sizeof(*state));
    if (!state)
        return NULL;
    state->source = source;
    state->rate = 48000;
    if (pthread_create(&state->thread, NULL, audio_thread, state) != 0) {
        blog(LOG_WARNING, "[KSM] audio thread unavailable; source remains silent");
        free(state);
        return NULL;
    }
    blog(LOG_INFO, "[KSM] source created; waiting for shared memory");
    return state;
}

static void source_destroy(void *opaque)
{
    struct ksm_source *state = opaque;
    if (!state)
        return;
    __atomic_store_n(&state->stop, true, __ATOMIC_RELEASE);
    pthread_join(state->thread, NULL);
    blog(LOG_INFO, "[KSM] source stopped: audioFrames=%" PRIu64 " silentFrames=%" PRIu64 " resyncs=%" PRIu64,
         state->audio_frames, state->silent_frames, state->resyncs);
    free(state);
}

bool obs_module_load(void)
{
    struct obs_source_info info = {0};
    info.id = KSM_SOURCE_ID;
    info.type = OBS_SOURCE_TYPE_INPUT;
    info.output_flags = OBS_SOURCE_AUDIO | OBS_SOURCE_CAP_DONT_SHOW_PROPERTIES;
    info.get_name = source_name;
    info.create = source_create;
    info.destroy = source_destroy;
    info.icon_type = OBS_ICON_TYPE_AUDIO_OUTPUT;
    obs_register_source(&info);
    bool registered = obs_get_source_output_flags(KSM_SOURCE_ID) == info.output_flags;
    blog(registered ? LOG_INFO : LOG_WARNING, "[KSM] source type %s: %s",
         registered ? "registered" : "registration failed", KSM_SOURCE_ID);
    return registered;
}

const char *obs_module_name(void)
{
    return "Klinglab Stream Mixer";
}

const char *obs_module_description(void)
{
    return "Stereo audio from Klinglab Stream Mixer shared memory";
}

OBS_MODULE_AUTHOR("Klinglab")
