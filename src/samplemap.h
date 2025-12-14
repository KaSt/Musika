#ifndef MUSIKA_SAMPLEMAP_H
#define MUSIKA_SAMPLEMAP_H

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct {
    char *name;
    char **variants;
    size_t variant_count;
    char *pitched_map_json;
    size_t pitched_entry_count;
} SampleSound;

typedef struct {
    char *name;
    char *base;
    SampleSound *sounds;
    size_t sound_count;
} SampleRegistry;

bool sample_registry_load_default(SampleRegistry *registry);
bool sample_registry_load_from_source(SampleRegistry *registry, const char *source, const char *name, bool refresh, char *cache_path, size_t cache_path_len, bool *out_cached, char *error, size_t error_len);
void sample_registry_free(SampleRegistry *registry);
void sample_registry_print(const SampleRegistry *registry, FILE *out);
void sample_registry_print_merged(const SampleRegistry *default_registry, const SampleRegistry *user_registry, const char *filter, FILE *out);
const SampleSound *sample_registry_find_sound(const SampleRegistry *registry, const char *name);


#endif // MUSIKA_SAMPLEMAP_H
