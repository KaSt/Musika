#ifndef MUSIKA_ENGINE_H
#define MUSIKA_ENGINE_H

#include "config.h"
#include <stddef.h>

typedef struct {
    double start_time;
    double duration;
    char sample[64];
    double gain;
} Event;

typedef struct {
    const MusikaConfig *config;
    double beat_seconds;
} EngineContext;

EngineContext engine_context_new(const MusikaConfig *config);
void engine_context_free(EngineContext *ctx);

void render_script(EngineContext *ctx, char **lines, size_t line_count);

#endif
