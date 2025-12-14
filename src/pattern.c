#include "pattern.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { TOKEN_BUFFER_LEN = 64 };

static void add_step(Pattern *pattern, const SampleRef *sample, double duration) {
    if (pattern->step_count >= sizeof(pattern->steps) / sizeof(pattern->steps[0])) return;
    pattern->steps[pattern->step_count].sample = *sample;
    pattern->steps[pattern->step_count].duration_beats = duration;
    pattern->step_count += 1;
}

static bool parse_variant_index(const char *text, size_t *out_index) {
    if (!text || !out_index) return false;
    if (text[0] == '\0') {
        *out_index = 0;
        return true;
    }
    for (const char *p = text; *p; ++p) {
        if (!isdigit((unsigned char)*p)) {
            return false;
        }
    }
    *out_index = (size_t)strtoul(text, NULL, 10);
    return true;
}

static SampleRef resolve_sample(const char *token, const SampleRegistry *default_registry, const SampleRegistry *user_registry) {
    SampleRef ref = {0};
    ref.valid = false;
    if (!token) return ref;
    bool has_registry = (default_registry != NULL) || (user_registry != NULL);

    char token_copy[TOKEN_BUFFER_LEN];
    strncpy(token_copy, token, sizeof(token_copy) - 1);
    token_copy[sizeof(token_copy) - 1] = '\0';

    char *variant_sep = strchr(token_copy, ':');
    size_t variant_index = 0;
    if (variant_sep) {
        *variant_sep = '\0';
        if (!parse_variant_index(variant_sep + 1, &variant_index)) {
            fprintf(stderr, "Warning: invalid variant index '%s' for sound '%s' (treated as rest)\n", variant_sep + 1, token_copy);
            return ref;
        }
    }

    const SampleSound *sound = NULL;
    const SampleRegistry *registry = NULL;
    if (user_registry) {
        sound = sample_registry_find_sound(user_registry, token_copy);
        registry = user_registry;
    }
    if (!sound && default_registry) {
        sound = sample_registry_find_sound(default_registry, token_copy);
        registry = default_registry;
    }

    if (!sound || sound->variant_count == 0 || !has_registry) {
        fprintf(stderr, "Unknown sound '%s' (treated as rest)\n", token_copy);
        return ref;
    }

    if (sound->variant_count > 0) {
        if (variant_index >= sound->variant_count) {
            variant_index = variant_index % sound->variant_count;
        }
    }

    ref.registry = registry;
    ref.sound = sound;
    ref.variant_index = variant_index;
    ref.valid = true;
    return ref;
}

bool pattern_from_lines(char **lines, size_t line_count, const SampleRegistry *default_registry, const SampleRegistry *user_registry, Pattern *out_pattern) {
    if (!out_pattern) return false;
    memset(out_pattern, 0, sizeof(*out_pattern));
    bool truncated_token_seen = false;

    for (size_t i = 0; i < line_count; ++i) {
        char *line = lines[i];
        if (!line) continue;
        size_t len = strlen(line);
        size_t idx = 0;
        while (idx < len) {
            while (idx < len && isspace((unsigned char)line[idx])) idx++;
            if (idx >= len) break;
            size_t start = idx;
            while (idx < len && !isspace((unsigned char)line[idx])) idx++;
            size_t tok_len = idx - start;
            if (tok_len == 0) continue;
            size_t copy_len = tok_len;
            char token[TOKEN_BUFFER_LEN];
            if (copy_len >= sizeof(token)) {
                copy_len = sizeof(token) - 1;
                truncated_token_seen = true;
            }
            memcpy(token, line + start, copy_len);
            token[copy_len] = '\0';
            SampleRef ref = resolve_sample(token, default_registry, user_registry);
            // Duration is intentionally fixed to one beat per token; extend parsing only when new syntax is introduced.
            add_step(out_pattern, &ref, 1.0);
        }
    }

    if (truncated_token_seen) {
        fprintf(stderr, "Warning: pattern tokens longer than %zu characters were truncated.\n", (size_t)(TOKEN_BUFFER_LEN - 1));
    }

    return out_pattern->step_count > 0;
}
