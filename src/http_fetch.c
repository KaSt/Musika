#include "http_fetch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Minimal fetch helper that shells out to curl to avoid adding heavy dependencies.
bool http_fetch_to_buffer(const char *url, char **out_buffer, size_t *out_len) {
    if (!url || !out_buffer) return false;
    *out_buffer = NULL;
    if (out_len) *out_len = 0;

    char cmd[1024];
    if (snprintf(cmd, sizeof(cmd), "curl -L -s %s", url) >= (int)sizeof(cmd)) {
        return false;
    }

    FILE *pipe = popen(cmd, "r");
    if (!pipe) return false;

    size_t capacity = 4096;
    size_t len = 0;
    char *buffer = malloc(capacity);
    if (!buffer) {
        pclose(pipe);
        return false;
    }

    size_t nread;
    while ((nread = fread(buffer + len, 1, capacity - len, pipe)) > 0) {
        len += nread;
        if (len == capacity) {
            capacity *= 2;
            char *next = realloc(buffer, capacity);
            if (!next) {
                free(buffer);
                pclose(pipe);
                return false;
            }
            buffer = next;
        }
    }
    pclose(pipe);

    if (len == 0) {
        free(buffer);
        return false;
    }
    if (len == capacity) {
        char *next = realloc(buffer, capacity + 1);
        if (!next) {
            free(buffer);
            return false;
        }
        buffer = next;
    }
    buffer[len] = '\0';
    *out_buffer = buffer;
    if (out_len) *out_len = len;
    return true;
}

