#ifndef MUSIKA_TRANSPORT_H
#define MUSIKA_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>

#include "../audio/audio.h"
#include "pattern.h"

typedef struct {
    AudioEngine *audio;
    AudioSample *samples;
    size_t sample_count;
    double seconds_per_beat;

    Pattern patterns[2];
    _Atomic size_t active_pattern;

    _Atomic bool running;
    _Atomic bool playing;

    double next_event_time;
    size_t next_step;

    struct {
        char key[256];
        AudioSample sample;
        bool loaded;
    } sample_cache[128];
    size_t sample_cache_count;

    pthread_t thread;
} Transport;

bool transport_start(Transport *transport, AudioEngine *audio, AudioSample *samples, size_t sample_count, double bpm);
void transport_stop(Transport *transport);
void transport_set_pattern(Transport *transport, const Pattern *pattern);
void transport_play(Transport *transport);
void transport_pause(Transport *transport);
void transport_panic(Transport *transport);

#endif // MUSIKA_TRANSPORT_H
