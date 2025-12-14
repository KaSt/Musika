#ifndef MUSIKA_CONFIG_H
#define MUSIKA_CONFIG_H

#include <stddef.h>

typedef struct {
    char *audio_backend;
    char **sample_repos;
    size_t sample_repo_count;
    double tempo_bpm;
} MusikaConfig;

void load_config(const char *path, MusikaConfig *config);
void free_config(MusikaConfig *config);

#endif
