#ifndef MUSIKA_EDITOR_H
#define MUSIKA_EDITOR_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char **lines;
    size_t length;
} TextBuffer;

void text_buffer_init(TextBuffer *buffer);
void text_buffer_clear(TextBuffer *buffer);
void text_buffer_free(TextBuffer *buffer);

void text_buffer_ensure_line(TextBuffer *buffer, size_t line);
size_t text_buffer_line_length(const TextBuffer *buffer, size_t line);

bool text_buffer_load_file(TextBuffer *buffer, const char *path);
bool text_buffer_save_to_path(const TextBuffer *buffer, const char *path);

void text_buffer_insert_char(TextBuffer *buffer, size_t row, size_t col, char c);
void text_buffer_insert_newline(TextBuffer *buffer, size_t row, size_t col);
void text_buffer_delete_char(TextBuffer *buffer, size_t row, size_t col);
void text_buffer_delete_line(TextBuffer *buffer, size_t row);

#endif
