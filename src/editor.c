#define _POSIX_C_SOURCE 200809L

#include "editor.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

static char *strdup_safe(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *out = malloc(len + 1);
    if (out) {
        memcpy(out, s, len + 1);
    }
    return out;
}

static char *strdup_range(const char *s, size_t len) {
    char *out = malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, s, len);
    out[len] = '\0';
    return out;
}

void text_buffer_init(TextBuffer *buffer) {
    if (!buffer) return;
    buffer->lines = NULL;
    buffer->length = 0;
    text_buffer_ensure_line(buffer, 0);
}

void text_buffer_clear(TextBuffer *buffer) {
    if (!buffer) return;
    for (size_t i = 0; i < buffer->length; ++i) {
        free(buffer->lines[i]);
    }
    free(buffer->lines);
    buffer->lines = NULL;
    buffer->length = 0;
}

void text_buffer_free(TextBuffer *buffer) {
    text_buffer_clear(buffer);
}

void text_buffer_ensure_line(TextBuffer *buffer, size_t line) {
    if (!buffer) return;
    if (buffer->length > line) return;
    size_t new_length = line + 1;
    char **new_lines = realloc(buffer->lines, sizeof(char *) * new_length);
    if (!new_lines) return;
    buffer->lines = new_lines;
    for (size_t i = buffer->length; i < new_length; ++i) {
        buffer->lines[i] = strdup_safe("");
    }
    buffer->length = new_length;
}

size_t text_buffer_line_length(const TextBuffer *buffer, size_t line) {
    if (!buffer || line >= buffer->length || !buffer->lines[line]) return 0;
    return strlen(buffer->lines[line]);
}

bool text_buffer_load_file(TextBuffer *buffer, const char *path) {
    if (!buffer || !path) return false;
    FILE *f = fopen(path, "r");
    if (!f) return false;

    text_buffer_clear(buffer);
    char *line = NULL;
    size_t cap = 0;
    ssize_t read = 0;
    while ((read = getline(&line, &cap, f)) != -1) {
        while (read > 0 && (line[read - 1] == '\n' || line[read - 1] == '\r')) {
            line[--read] = '\0';
        }
        text_buffer_ensure_line(buffer, buffer->length);
        size_t idx = buffer->length - 1;
        free(buffer->lines[idx]);
        buffer->lines[idx] = strdup_safe(line);
    }
    free(line);
    fclose(f);

    if (buffer->length == 0) {
        text_buffer_ensure_line(buffer, 0);
    }
    return true;
}

bool text_buffer_save_to_path(const TextBuffer *buffer, const char *path) {
    if (!buffer || !path) return false;
    FILE *f = fopen(path, "w");
    if (!f) return false;

    for (size_t i = 0; i < buffer->length; ++i) {
        const char *s = buffer->lines[i] ? buffer->lines[i] : "";
        fputs(s, f);
        if (i + 1 < buffer->length) fputc('\n', f);
    }

    fclose(f);
    return true;
}

void text_buffer_insert_char(TextBuffer *buffer, size_t row, size_t col, char c) {
    if (!buffer) return;
    text_buffer_ensure_line(buffer, row);
    char *line = buffer->lines[row];
    if (!line) {
        buffer->lines[row] = strdup_range(&c, 1);
        return;
    }
    size_t len = strlen(line);
    if (col > len) col = len;
    char *new_line = realloc(line, len + 2);
    if (!new_line) return;
    memmove(new_line + col + 1, new_line + col, len - col + 1);
    new_line[col] = c;
    buffer->lines[row] = new_line;
}

void text_buffer_insert_newline(TextBuffer *buffer, size_t row, size_t col) {
    if (!buffer) return;
    text_buffer_ensure_line(buffer, row);
    char *line = buffer->lines[row];
    size_t len = line ? strlen(line) : 0;
    if (col > len) col = len;

    char *head = line ? strdup_range(line, col) : strdup_safe("");
    char *tail = line ? strdup_safe(line + col) : strdup_safe("");
    if (!head || !tail) {
        free(head);
        free(tail);
        return;
    }

    free(buffer->lines[row]);
    buffer->lines[row] = head;

    char **new_lines = realloc(buffer->lines, sizeof(char *) * (buffer->length + 1));
    if (!new_lines) {
        free(tail);
        return;
    }
    buffer->lines = new_lines;
    memmove(&buffer->lines[row + 2], &buffer->lines[row + 1], sizeof(char *) * (buffer->length - (row + 1)));
    buffer->lines[row + 1] = tail;
    buffer->length += 1;
}

void text_buffer_delete_char(TextBuffer *buffer, size_t row, size_t col) {
    if (!buffer || row >= buffer->length) return;
    char *line = buffer->lines[row];
    if (!line) return;
    size_t len = strlen(line);
    if (len == 0 || col >= len) return;
    memmove(line + col, line + col + 1, len - col);
}

void text_buffer_delete_line(TextBuffer *buffer, size_t row) {
    if (!buffer || row >= buffer->length) return;
    free(buffer->lines[row]);
    memmove(&buffer->lines[row], &buffer->lines[row + 1], sizeof(char *) * (buffer->length - row - 1));
    buffer->length -= 1;
    if (buffer->length == 0) {
        text_buffer_ensure_line(buffer, 0);
    }
}
