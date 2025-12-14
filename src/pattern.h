#ifndef MUSIKA_PATTERN_H
#define MUSIKA_PATTERN_H

#include <stddef.h>
#include <stdbool.h>

typedef struct {
    int sample_id;
    double duration_beats;
} PatternStep;

typedef struct {
    PatternStep steps[128];
    size_t step_count;
} Pattern;

bool pattern_from_lines(char **lines, size_t line_count, Pattern *out_pattern);

#endif // MUSIKA_PATTERN_H
