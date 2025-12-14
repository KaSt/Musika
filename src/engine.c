#include "engine.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static double clamp(double v, double lo, double hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

EngineContext engine_context_new(const MusikaConfig *config) {
    EngineContext ctx;
    ctx.config = config;
    ctx.beat_seconds = config ? 60.0 / config->tempo_bpm : 0.5;
    return ctx;
}

void engine_context_free(EngineContext *ctx) {
    (void)ctx;
}

static int next_token(const char *line, size_t *idx, char *out, size_t out_len) {
    size_t i = *idx;
    while (line[i] == ' ') i++;
    if (line[i] == '\0') {
        return 0;
    }
    size_t start = i;
    while (line[i] && line[i] != ' ' && line[i] != '\n') {
        i++;
    }
    size_t len = i - start;
    if (len >= out_len) len = out_len - 1;
    memcpy(out, line + start, len);
    out[len] = '\0';
    *idx = i;
    return 1;
}

static void emit_event(const EngineContext *ctx, double beat_position, double duration, const char *sample) {
    Event e;
    e.start_time = beat_position * ctx->beat_seconds;
    e.duration = duration * ctx->beat_seconds;
    e.gain = 1.0;
    strncpy(e.sample, sample, sizeof(e.sample) - 1);
    e.sample[sizeof(e.sample) - 1] = '\0';
    printf("[%.2fs] %-8s (%.2fs)\n", e.start_time, e.sample, e.duration);
}

static void interpret_phrase(const EngineContext *ctx, const char *phrase) {
    size_t idx = 0;
    double beat = 0.0;
    double beat_width = 1.0;
    char token[128];

    while (next_token(phrase, &idx, token, sizeof(token))) {
        if (strncmp(token, "fast(", 5) == 0) {
            double factor = atof(token + 5);
            if (factor > 0) {
                beat_width = clamp(beat_width / factor, 0.0625, 4.0);
            }
        } else if (strncmp(token, "slow(", 5) == 0) {
            double factor = atof(token + 5);
            if (factor > 0) {
                beat_width = clamp(beat_width * factor, 0.0625, 8.0);
            }
        } else if (token[0] == '[') {
            // stacked sub pattern: [a b c]
            char inner[256];
            size_t len = strlen(token);
            if (token[len - 1] == ']') {
                memcpy(inner, token + 1, len - 2);
                inner[len - 2] = '\0';
            } else {
                strcpy(inner, token + 1);
            }
            char *voice = strtok(inner, "/");
            double local_width = beat_width / 2.0;
            while (voice) {
                emit_event(ctx, beat, local_width, voice);
                beat += local_width;
                voice = strtok(NULL, "/");
            }
        } else {
            emit_event(ctx, beat, beat_width, token);
            beat += beat_width;
        }
    }
}

void render_script(EngineContext *ctx, char **lines, size_t line_count) {
    for (size_t i = 0; i < line_count; ++i) {
        const char *line = lines[i];
        if (!line || line[0] == '\0') continue;
        printf("Track %zu | %s\n", i + 1, line);
        interpret_phrase(ctx, line);
    }
}
