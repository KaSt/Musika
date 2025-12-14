#include "audio.h"

#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static size_t next_index(size_t idx) {
    return (idx + 1) % 1024;
}

static void audio_callback(ma_device *device, void *output, const void *input, ma_uint32 frame_count) {
    (void)input;
    AudioEngine *engine = (AudioEngine *)device->config.pUserData;
    float *out = (float *)output;
    const uint32_t channels = engine->channels;
    memset(out, 0, sizeof(float) * frame_count * channels);

    if (atomic_exchange(&engine->panic, false)) {
        engine->voice_count = 0;
        atomic_store(&engine->event_tail, atomic_load(&engine->event_head));
    }

    uint64_t frame_cursor = atomic_load(&engine->frame_cursor);

    for (ma_uint32 frame = 0; frame < frame_count; ++frame) {
        uint64_t global_frame = frame_cursor + frame;

        size_t tail = atomic_load(&engine->event_tail);
        size_t head = atomic_load(&engine->event_head);
        while (tail != head) {
            ScheduledEvent *ev = &engine->event_queue[tail];
            if (ev->start_frame > global_frame) break;

            if (engine->voice_count < (sizeof(engine->voices) / sizeof(engine->voices[0]))) {
                ActiveVoice *voice = &engine->voices[engine->voice_count++];
                voice->sample = ev->sample;
                voice->start_frame = ev->start_frame;
                voice->position = 0;
            }

            tail = next_index(tail);
            atomic_store(&engine->event_tail, tail);
            head = atomic_load(&engine->event_head);
        }

        for (size_t v = 0; v < engine->voice_count;) {
            ActiveVoice *voice = &engine->voices[v];
            if (!voice->sample) {
                engine->voices[v] = engine->voices[--engine->voice_count];
                continue;
            }

            uint64_t offset = 0;
            if (global_frame >= voice->start_frame) {
                offset = global_frame - voice->start_frame;
            } else {
                ++v;
                continue;
            }

            if (offset >= voice->sample->frame_count) {
                engine->voices[v] = engine->voices[--engine->voice_count];
                continue;
            }

            for (uint32_t ch = 0; ch < channels; ++ch) {
                uint32_t sample_ch = ch % voice->sample->channels;
                float s = voice->sample->data[offset * voice->sample->channels + sample_ch];
                out[frame * channels + ch] += s;
            }
            ++v;
        }
    }

    atomic_store(&engine->frame_cursor, frame_cursor + frame_count);
}

bool audio_engine_init(AudioEngine *engine, uint32_t sample_rate, uint32_t channels) {
    memset(engine, 0, sizeof(*engine));
    engine->sample_rate = sample_rate;
    engine->channels = channels;
    atomic_store(&engine->frame_cursor, 0);
    atomic_store(&engine->event_head, 0);
    atomic_store(&engine->event_tail, 0);
    atomic_store(&engine->panic, false);

    ma_device_config cfg = ma_device_config_init(channels, sample_rate);
    cfg.dataCallback = audio_callback;
    cfg.pUserData = engine;

    if (ma_context_init(NULL, 0, &engine->context) != 0) {
        fprintf(stderr, "Failed to init audio context\n");
        return false;
    }
    if (ma_device_init(&engine->context, &cfg, &engine->device) != 0) {
        fprintf(stderr, "Failed to init audio device\n");
        ma_context_uninit(&engine->context);
        return false;
    }
    engine->sample_rate = engine->device.config.sampleRate;
    engine->channels = engine->device.config.channels;
    if (ma_device_start(&engine->device) != 0) {
        fprintf(stderr, "Failed to start audio device\n");
        ma_device_uninit(&engine->device);
        ma_context_uninit(&engine->context);
        return false;
    }
    return true;
}

void audio_engine_shutdown(AudioEngine *engine) {
    ma_device_uninit(&engine->device);
    ma_context_uninit(&engine->context);
}

bool audio_engine_queue(AudioEngine *engine, const AudioSample *sample, uint64_t start_frame) {
    size_t head = atomic_load(&engine->event_head);
    size_t next = next_index(head);
    size_t tail = atomic_load(&engine->event_tail);
    if (next == tail) {
        return false; // queue full
    }
    engine->event_queue[head].sample = sample;
    engine->event_queue[head].start_frame = start_frame;
    atomic_store(&engine->event_head, next);
    return true;
}

double audio_engine_time_seconds(const AudioEngine *engine) {
    uint64_t frames = atomic_load(&engine->frame_cursor);
    if (engine->sample_rate == 0) return 0.0;
    return (double)frames / (double)engine->sample_rate;
}

void audio_engine_panic(AudioEngine *engine) {
    atomic_store(&engine->panic, true);
}

bool audio_sample_generate_sine(AudioSample *out_sample, double seconds, uint32_t sample_rate, double frequency) {
    if (!out_sample) return false;
    uint32_t frames = (uint32_t)(seconds * (double)sample_rate);
    float *data = (float *)malloc(sizeof(float) * frames);
    if (!data) return false;
    for (uint32_t i = 0; i < frames; ++i) {
        double t = (double)i / (double)sample_rate;
        data[i] = (float)sin(2.0 * M_PI * frequency * t) * 0.4f;
    }
    out_sample->data = data;
    out_sample->frame_count = frames;
    out_sample->channels = 1;
    out_sample->sample_rate = sample_rate;
    return true;
}

static uint32_t read_le32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool audio_sample_from_wav(const char *path, AudioSample *out_sample) {
    if (!out_sample) return false;
    memset(out_sample, 0, sizeof(*out_sample));

    FILE *f = fopen(path, "rb");
    if (!f) return false;

    unsigned char header[44];
    if (fread(header, 1, sizeof(header), f) != sizeof(header)) {
        fclose(f);
        return false;
    }

    if (memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0) {
        fclose(f);
        return false;
    }

    uint16_t audio_format = header[20] | (header[21] << 8);
    uint16_t channels = header[22] | (header[23] << 8);
    uint32_t sample_rate = read_le32(header + 24);
    uint16_t bits_per_sample = header[34] | (header[35] << 8);
    uint32_t data_chunk_size = read_le32(header + 40);

    if (audio_format != 1 || bits_per_sample != 16) {
        fclose(f);
        return false;
    }

    const size_t max_wav_bytes = 50 * 1024 * 1024;
    if (data_chunk_size > max_wav_bytes) {
        fprintf(stderr, "WAV data chunk too large (%u bytes). Refusing to load.\n", data_chunk_size);
        fclose(f);
        return false;
    }

    size_t frames = data_chunk_size / (channels * (bits_per_sample / 8));
    int16_t *pcm16 = (int16_t *)malloc(data_chunk_size);
    if (!pcm16) {
        fclose(f);
        return false;
    }
    size_t read = fread(pcm16, 1, data_chunk_size, f);
    fclose(f);
    if (read != data_chunk_size) {
        free(pcm16);
        return false;
    }

    float *data = (float *)malloc(sizeof(float) * frames * channels);
    if (!data) {
        free(pcm16);
        return false;
    }
    for (size_t i = 0; i < frames * channels; ++i) {
        data[i] = (float)pcm16[i] / 32768.0f;
    }

    free(pcm16);

    out_sample->data = data;
    out_sample->frame_count = (uint32_t)frames;
    out_sample->channels = channels;
    out_sample->sample_rate = sample_rate;
    return true;
}

void audio_sample_free(AudioSample *sample) {
    if (!sample) return;
    free(sample->data);
    sample->data = NULL;
    sample->frame_count = 0;
    sample->channels = 0;
    sample->sample_rate = 0;
}
