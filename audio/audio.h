#ifndef MUSIKA_AUDIO_H
#define MUSIKA_AUDIO_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../third_party/miniaudio/miniaudio.h"

typedef struct {
    float *data;
    uint32_t frame_count;
    uint32_t channels;
    uint32_t sample_rate;
} AudioSample;

typedef struct {
    const AudioSample *sample;
    uint64_t start_frame;
    double playback_rate;
} ScheduledEvent;

typedef struct {
    const AudioSample *sample;
    uint64_t start_frame;
    double playback_rate;
} ActiveVoice;

typedef struct {
    ma_context context;
    ma_device device;
    uint32_t sample_rate;
    uint32_t channels;

    _Atomic uint64_t frame_cursor;
    ScheduledEvent event_queue[1024];
    _Atomic size_t event_head;
    _Atomic size_t event_tail;

    ActiveVoice voices[64];
    size_t voice_count;
    _Atomic bool panic;
} AudioEngine;

bool audio_engine_init(AudioEngine *engine, uint32_t sample_rate, uint32_t channels);
void audio_engine_shutdown(AudioEngine *engine);

bool audio_engine_queue(AudioEngine *engine, const AudioSample *sample, uint64_t start_frame);
bool audio_engine_queue_rate(AudioEngine *engine, const AudioSample *sample, uint64_t start_frame, double playback_rate);
double audio_engine_time_seconds(const AudioEngine *engine);
void audio_engine_panic(AudioEngine *engine);

bool audio_sample_from_wav(const char *path, AudioSample *out_sample);
bool audio_sample_generate_sine(AudioSample *out_sample, double seconds, uint32_t sample_rate, double frequency);
void audio_sample_free(AudioSample *sample);

#endif // MUSIKA_AUDIO_H
