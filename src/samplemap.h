#ifndef MUSIKA_SAMPLEMAP_H
#define MUSIKA_SAMPLEMAP_H

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct {
    char *name;
    char **variants;
    size_t variant_count;
} SampleSound;

typedef struct {
    char *name;
    char *base;
    SampleSound *sounds;
    size_t sound_count;
} SampleRegistry;

bool sample_registry_load_default(SampleRegistry *registry);
void sample_registry_free(SampleRegistry *registry);
void sample_registry_print(const SampleRegistry *registry, FILE *out);

#endif // MUSIKA_SAMPLEMAP_H
