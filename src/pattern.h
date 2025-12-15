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

typedef enum {
    TIME_TRANSFORM_NONE = 0,
    TIME_TRANSFORM_FAST,
    TIME_TRANSFORM_SLOW,
} TimeTransformType;

typedef struct {
    int id;
    double base_time_scale;
    bool has_every;
    int every_interval;
    TimeTransformType every_type;
    int every_factor;
} PatternChain;

typedef struct {
    SampleRef sample;
    double duration_beats;
    double playback_rate;
    int midi_note;
    bool has_midi_note;
    bool advance_time;
    int chain_id;
    double time_scale;
} PatternStep;

typedef struct {
    PatternStep steps[128];
    size_t step_count;

    PatternChain chains[16];
    size_t chain_count;
} Pattern;

bool pattern_from_lines(char **lines, size_t line_count, const SampleRegistry *default_registry, const SampleRegistry *user_registry, Pattern *out_pattern);

#endif // MUSIKA_PATTERN_H
