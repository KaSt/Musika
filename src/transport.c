#include "transport.h"

#include <math.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>

static void sleep_ms(int ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static void *transport_thread(void *user) {
    Transport *t = (Transport *)user;
    while (atomic_load(&t->running)) {
        if (!atomic_load(&t->playing)) {
            sleep_ms(10);
            continue;
        }

        Pattern *pattern = &t->patterns[atomic_load(&t->active_pattern) % 2];
        if (pattern->step_count == 0) {
            sleep_ms(10);
            continue;
        }

        double now = audio_engine_time_seconds(t->audio);
        double horizon = now + 0.2;
        if (t->next_event_time < now) {
            t->next_event_time = now;
        }

        while (t->next_event_time <= horizon) {
            PatternStep *step = &pattern->steps[t->next_step];
            if (step->sample.valid && t->sample_count > 0) {
                uint64_t start_frame = (uint64_t)(t->next_event_time * (double)t->audio->sample_rate);
                audio_engine_queue(t->audio, &t->samples[0], start_frame);
            }
            t->next_event_time += step->duration_beats * t->seconds_per_beat;
            t->next_step = (t->next_step + 1) % pattern->step_count;
        }

        sleep_ms(10);
    }
    return NULL;
}

bool transport_start(Transport *transport, AudioEngine *audio, AudioSample *samples, size_t sample_count, double bpm) {
    memset(transport, 0, sizeof(*transport));
    transport->audio = audio;
    transport->samples = samples;
    transport->sample_count = sample_count;
    transport->seconds_per_beat = 60.0 / bpm;
    atomic_store(&transport->active_pattern, 0);
    atomic_store(&transport->running, true);
    atomic_store(&transport->playing, false);
    transport->next_event_time = 0.0;
    transport->next_step = 0;

    if (pthread_create(&transport->thread, NULL, transport_thread, transport) != 0) {
        atomic_store(&transport->running, false);
        return false;
    }
    return true;
}

void transport_stop(Transport *transport) {
    if (!transport) return;
    atomic_store(&transport->running, false);
    pthread_join(transport->thread, NULL);
}

void transport_set_pattern(Transport *transport, const Pattern *pattern) {
    if (!transport || !pattern) return;
    size_t next = (atomic_load(&transport->active_pattern) + 1) % 2;
    transport->patterns[next] = *pattern;
    atomic_store(&transport->active_pattern, next);
    transport->next_step = 0;
    transport->next_event_time = audio_engine_time_seconds(transport->audio);
}

void transport_play(Transport *transport) {
    if (!transport) return;
    transport->next_event_time = audio_engine_time_seconds(transport->audio);
    transport->next_step = 0;
    atomic_store(&transport->playing, true);
}

void transport_pause(Transport *transport) {
    if (!transport) return;
    atomic_store(&transport->playing, false);
}

void transport_panic(Transport *transport) {
    if (!transport) return;
    audio_engine_panic(transport->audio);
    transport->next_event_time = audio_engine_time_seconds(transport->audio);
    transport->next_step = 0;
}
