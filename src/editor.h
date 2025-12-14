#ifndef MUSIKA_EDITOR_H
#define MUSIKA_EDITOR_H

#include <stddef.h>

typedef struct {
    char **lines;
    size_t length;
} TextBuffer;

TextBuffer text_buffer_new(void);
void text_buffer_clear(TextBuffer *buffer);
void text_buffer_free(TextBuffer *buffer);
void launch_editor(TextBuffer *buffer);

#endif
