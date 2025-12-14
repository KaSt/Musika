#include "transport.h"

#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "cache.h"
#include "http_fetch.h"
static void sleep_ms(int ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static bool file_exists(const char *path) {
    struct stat st;
    return path && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static bool build_variant_url(const SampleRef *ref, char *out, size_t out_len) {
    if (!ref || !ref->sound || ref->variant_index >= ref->sound->variant_count || !out || out_len == 0) return false;
    const char *variant = ref->sound->variants ? ref->sound->variants[ref->variant_index] : NULL;
    if (!variant || variant[0] == '\0') return false;

    if (strncmp(variant, "http://", 7) == 0 || strncmp(variant, "https://", 8) == 0 || strncmp(variant, "file://", 7) == 0 || variant[0] == '/' || variant[0] == '.') {
        return snprintf(out, out_len, "%s", variant) < (int)out_len;
    }

    const char *base = (ref->registry && ref->registry->base) ? ref->registry->base : NULL;
    if (base && base[0]) {
        size_t base_len = strlen(base);
        bool needs_slash = base_len > 0 && base[base_len - 1] != '/' && variant[0] != '/';
        if (needs_slash) {
            return snprintf(out, out_len, "%s/%s", base, variant) < (int)out_len;
        }
        return snprintf(out, out_len, "%s%s", base, variant) < (int)out_len;
    }

    return snprintf(out, out_len, "%s", variant) < (int)out_len;
}

static bool is_remote_url(const char *url) {
    return url && (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0);
}

static const AudioSample *load_builtin_tone(Transport *t) {
    if (!t) return NULL;
    const char *key = "builtin:tone";
    for (size_t i = 0; i < t->sample_cache_count; ++i) {
        if (t->sample_cache[i].loaded && strcmp(t->sample_cache[i].key, key) == 0) {
            return &t->sample_cache[i].sample;
        }
    }

    if (t->sample_cache_count >= sizeof(t->sample_cache) / sizeof(t->sample_cache[0])) {
        return NULL;
    }

    AudioSample sample;
    if (!audio_sample_generate_sine(&sample, 1.5, t->audio ? t->audio->sample_rate : 48000, 440.0)) {
        return NULL;
    }

    size_t slot = t->sample_cache_count++;
    t->sample_cache[slot].loaded = true;
    snprintf(t->sample_cache[slot].key, sizeof(t->sample_cache[slot].key), "%s", key);
    t->sample_cache[slot].sample = sample;
    return &t->sample_cache[slot].sample;
}

static bool ensure_cached_sample(const char *url, char *out_path, size_t out_len) {
    if (!url || !out_path || out_len == 0) return false;
    if (!cache_path_for_key_with_ext(url, ".wav", out_path, out_len)) return false;
    if (file_exists(out_path)) return true;

    char *buffer = NULL;
    size_t len = 0;
    if (!http_fetch_to_buffer(url, &buffer, &len)) return false;
    bool ok = cache_write(out_path, buffer, len);
    free(buffer);
    return ok;
}

static const AudioSample *load_sample_for_ref(Transport *t, const SampleRef *ref) {
    if (!t || !ref || !ref->valid) return NULL;
    if (!ref->sound || ref->variant_index >= ref->sound->variant_count) return NULL;

    if (ref->sound->name && strcmp(ref->sound->name, "tone") == 0) {
        const AudioSample *tone = load_builtin_tone(t);
        if (tone) return tone;
    }

    const char *registry_name = (ref->registry && ref->registry->name) ? ref->registry->name : "default";
    char cache_key[256];
    if (snprintf(cache_key, sizeof(cache_key), "%s:%s:%zu", registry_name, ref->sound->name, ref->variant_index) >= (int)sizeof(cache_key)) {
        return NULL;
    }

    for (size_t i = 0; i < t->sample_cache_count; ++i) {
        if (t->sample_cache[i].loaded && strcmp(t->sample_cache[i].key, cache_key) == 0) {
            return &t->sample_cache[i].sample;
        }
    }

    if (t->sample_cache_count >= sizeof(t->sample_cache) / sizeof(t->sample_cache[0])) {
        return NULL;
    }

    char url[512];
    if (!build_variant_url(ref, url, sizeof(url))) {
        return (t->sample_count > 0) ? &t->samples[0] : NULL;
    }

    char path[512];
    bool remote = is_remote_url(url);
    if (remote) {
        if (!ensure_cached_sample(url, path, sizeof(path))) {
            return (t->sample_count > 0) ? &t->samples[0] : NULL;
        }
    } else {
        if (snprintf(path, sizeof(path), "%s", url) >= (int)sizeof(path) || !file_exists(path)) {
            return (t->sample_count > 0) ? &t->samples[0] : NULL;
        }
    }

    AudioSample sample;
    if (!audio_sample_from_wav(path, &sample)) {
        return (t->sample_count > 0) ? &t->samples[0] : NULL;
    }

    size_t slot = t->sample_cache_count++;
    t->sample_cache[slot].loaded = true;
    snprintf(t->sample_cache[slot].key, sizeof(t->sample_cache[slot].key), "%s", cache_key);
    t->sample_cache[slot].sample = sample;
    return &t->sample_cache[slot].sample;
}

static void free_cached_samples(Transport *t) {
    if (!t) return;
    for (size_t i = 0; i < t->sample_cache_count; ++i) {
        if (t->sample_cache[i].loaded) {
            audio_sample_free(&t->sample_cache[i].sample);
            t->sample_cache[i].loaded = false;
        }
    }
    t->sample_cache_count = 0;
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
            if (step->sample.valid) {
                const AudioSample *sample = load_sample_for_ref(t, &step->sample);
                if (sample) {
                    uint64_t start_frame = (uint64_t)(t->next_event_time * (double)t->audio->sample_rate);
                    audio_engine_queue_rate(t->audio, sample, start_frame, step->playback_rate > 0.0 ? step->playback_rate : 1.0);
                }
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
    transport->sample_cache_count = 0;

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
    free_cached_samples(transport);
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
