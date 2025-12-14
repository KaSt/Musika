#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *strdup_range(const char *start, size_t len) {
    char *out = malloc(len + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, start, len);
    out[len] = '\0';
    return out;
}

static char *strdup_safe(const char *s) {
    if (!s) return NULL;
    return strdup_range(s, strlen(s));
}

static void clear_sample_repos(MusikaConfig *config) {
    if (!config || !config->sample_repos) return;
    for (size_t i = 0; i < config->sample_repo_count; ++i) {
        free(config->sample_repos[i]);
    }
    free(config->sample_repos);
    config->sample_repos = NULL;
    config->sample_repo_count = 0;
}

static void append_repo(MusikaConfig *config, const char *start, size_t len) {
    if (!config || !start || len == 0) return;
    char *repo = strdup_range(start, len);
    if (!repo) return;
    char **next = realloc(config->sample_repos, sizeof(char *) * (config->sample_repo_count + 1));
    if (!next) {
        free(repo);
        return;
    }
    config->sample_repos = next;
    config->sample_repos[config->sample_repo_count] = repo;
    config->sample_repo_count += 1;
}

static void set_defaults(MusikaConfig *config) {
    config->audio_backend = strdup_safe("miniaudio");
    config->tempo_bpm = 120.0;
    config->sample_repo_count = 0;
    config->sample_repos = NULL;
    append_repo(config, "https://github.com/tyleretters/strudel-samples", strlen("https://github.com/tyleretters/strudel-samples"));
    append_repo(config, "https://github.com/tidalcycles/Dirt-Samples", strlen("https://github.com/tidalcycles/Dirt-Samples"));
    append_repo(config, "https://github.com/lukaprincic/strudel-sample-pack", strlen("https://github.com/lukaprincic/strudel-sample-pack"));
}

static void reset_config(MusikaConfig *config) {
    config->audio_backend = NULL;
    config->sample_repos = NULL;
    config->sample_repo_count = 0;
    config->tempo_bpm = 120.0;
}

static char *load_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long len = ftell(f);
    if (len < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    char *buf = malloc((size_t)len + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t read_len = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[read_len] = '\0';
    if (out_len) *out_len = read_len;
    return buf;
}

static const char *skip_ws(const char *p) {
    while (p && *p && isspace((unsigned char)*p)) {
        ++p;
    }
    return p;
}

static int parse_audio_backend(const char *json, MusikaConfig *config) {
    const char *key = "\"audioBackend\"";
    const char *pos = strstr(json, key);
    if (!pos) return 0;
    pos = strchr(pos, ':');
    if (!pos) return 0;
    pos = skip_ws(pos + 1);
    if (!pos || *pos != '"') return 0;
    const char *value_start = pos + 1;
    const char *value_end = strchr(value_start, '"');
    if (!value_end) return 0;
    free(config->audio_backend);
    config->audio_backend = strdup_range(value_start, (size_t)(value_end - value_start));
    return 1;
}

static int parse_tempo(const char *json, MusikaConfig *config) {
    const char *key = "\"tempo\"";
    const char *pos = strstr(json, key);
    if (!pos) return 0;
    pos = strchr(pos, ':');
    if (!pos) return 0;
    pos = skip_ws(pos + 1);
    if (!pos) return 0;
    char *endptr = NULL;
    double tempo = strtod(pos, &endptr);
    if (pos == endptr) return 0;
    if (tempo > 0.0) {
        config->tempo_bpm = tempo;
    }
    return 1;
}

static int parse_sample_repos(const char *json, MusikaConfig *config) {
    const char *key = "\"sampleRepos\"";
    const char *pos = strstr(json, key);
    if (!pos) return 0;
    pos = strchr(pos, ':');
    if (!pos) return 1; // key present but malformed
    pos = skip_ws(pos + 1);
    if (!pos || *pos != '[') return 1;
    ++pos; // move past '['

    clear_sample_repos(config);

    while (*pos && *pos != ']') {
        pos = skip_ws(pos);
        if (*pos == '"') {
            const char *value_start = pos + 1;
            const char *value_end = strchr(value_start, '"');
            if (!value_end) break;
            append_repo(config, value_start, (size_t)(value_end - value_start));
            pos = value_end + 1;
        } else {
            pos++;
        }
        while (*pos && *pos != '"' && *pos != ']') {
            if (*pos == ',') {
                pos++;
                break;
            }
            pos++;
        }
    }

    return 1;
}

void load_config(const char *path, MusikaConfig *config) {
    reset_config(config);
    set_defaults(config);

    size_t len = 0;
    char *json = load_file(path, &len);
    if (!json || len == 0) {
        free(json);
        return;
    }

    parse_audio_backend(json, config);
    parse_tempo(json, config);
    parse_sample_repos(json, config);

    if (!config->audio_backend) {
        config->audio_backend = strdup_safe("simulated");
    }
    if (config->sample_repo_count == 0) {
        set_defaults(config);
    }

    free(json);
}

void free_config(MusikaConfig *config) {
    free(config->audio_backend);
    clear_sample_repos(config);
    reset_config(config);
}
