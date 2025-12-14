#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *strdup_safe(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *out = malloc(len + 1);
    if (out) {
        memcpy(out, s, len + 1);
    }
    return out;
}

static void set_defaults(MusikaConfig *config) {
    config->audio_backend = strdup_safe("simulated");
    config->tempo_bpm = 120.0;
    config->sample_repo_count = 3;
    config->sample_repos = calloc(config->sample_repo_count, sizeof(char *));
    config->sample_repos[0] = strdup_safe("https://github.com/tyleretters/strudel-samples");
    config->sample_repos[1] = strdup_safe("https://github.com/tidalcycles/Dirt-Samples");
    config->sample_repos[2] = strdup_safe("https://github.com/lukaprincic/strudel-sample-pack");
}

static void reset_config(MusikaConfig *config) {
    config->audio_backend = NULL;
    config->sample_repos = NULL;
    config->sample_repo_count = 0;
    config->tempo_bpm = 120.0;
}

static void parse_line(MusikaConfig *config, const char *line) {
    if (strstr(line, "audioBackend") != NULL) {
        const char *value = strchr(line, ':');
        if (value) {
            value = value + 1;
            while (*value == ' ' || *value == '"') value++;
            const char *end = strpbrk(value, "\",\n");
            if (end) {
                size_t len = (size_t)(end - value);
                char *buf = malloc(len + 1);
                memcpy(buf, value, len);
                buf[len] = '\0';
                free(config->audio_backend);
                config->audio_backend = buf;
            }
        }
    } else if (strstr(line, "tempo") != NULL) {
        const char *value = strchr(line, ':');
        if (value) {
            config->tempo_bpm = atof(value + 1);
        }
    } else if (strstr(line, "sampleRepos") != NULL) {
        // handled in subsequent lines, here we just ensure allocation if missing
        return;
    } else if (strstr(line, "http") != NULL) {
        const char *start = strchr(line, '"');
        const char *end = strrchr(line, '"');
        if (start && end && end > start) {
            size_t len = (size_t)(end - start - 1);
            char *url = malloc(len + 1);
            memcpy(url, start + 1, len);
            url[len] = '\0';
            config->sample_repos = realloc(config->sample_repos, sizeof(char *) * (config->sample_repo_count + 1));
            config->sample_repos[config->sample_repo_count] = url;
            config->sample_repo_count += 1;
        }
    }
}

void load_config(const char *path, MusikaConfig *config) {
    reset_config(config);
    set_defaults(config);

    FILE *f = fopen(path, "r");
    if (!f) {
        return;
    }

    // reset repos to read from file
    for (size_t i = 0; i < config->sample_repo_count; ++i) {
        free(config->sample_repos[i]);
    }
    free(config->sample_repos);
    config->sample_repos = NULL;
    config->sample_repo_count = 0;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        parse_line(config, line);
    }
    fclose(f);

    if (!config->audio_backend) {
        config->audio_backend = strdup_safe("simulated");
    }
    if (config->sample_repo_count == 0) {
        set_defaults(config);
    }
}

void free_config(MusikaConfig *config) {
    free(config->audio_backend);
    for (size_t i = 0; i < config->sample_repo_count; ++i) {
        free(config->sample_repos[i]);
    }
    free(config->sample_repos);
    reset_config(config);
}
