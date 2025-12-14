#ifndef MUSIKA_PATTERN_H
#define MUSIKA_PATTERN_H

#include <stddef.h>
#include <stdbool.h>

#include "samplemap.h"

typedef struct {
    const SampleRegistry *registry;
    const SampleSound *sound;
    size_t variant_index;
    bool valid;
} SampleRef;

typedef struct {
    SampleRef sample;
    double duration_beats;
} PatternStep;

typedef struct {
    PatternStep steps[128];
    size_t step_count;
} Pattern;

bool pattern_from_lines(char **lines, size_t line_count, const SampleRegistry *default_registry, const SampleRegistry *user_registry, Pattern *out_pattern);

#endif // MUSIKA_PATTERN_H
