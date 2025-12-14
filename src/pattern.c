#include "pattern.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

enum { TOKEN_BUFFER_LEN = 64 };

static int map_token_to_sample(const char *token) {
    if (strcmp(token, "kick") == 0 || strcmp(token, "bd") == 0 || strcmp(token, "bass") == 0) {
        return 0;
    }
    return -1;
}

static void add_step(Pattern *pattern, int sample_id, double duration) {
    if (pattern->step_count >= sizeof(pattern->steps) / sizeof(pattern->steps[0])) return;
    pattern->steps[pattern->step_count].sample_id = sample_id;
    pattern->steps[pattern->step_count].duration_beats = duration;
    pattern->step_count += 1;
}

bool pattern_from_lines(char **lines, size_t line_count, Pattern *out_pattern) {
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
            int sample = map_token_to_sample(token);
            if (sample < 0) {
                fprintf(stderr, "Warning: unknown token '%s' treated as rest\n", token);
            }
            add_step(out_pattern, sample, 1.0);
        }
    }

    if (truncated_token_seen) {
        fprintf(stderr, "Warning: pattern tokens longer than %zu characters were truncated.\n", (size_t)(TOKEN_BUFFER_LEN - 1));
    }

    return out_pattern->step_count > 0;
}
