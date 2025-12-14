#include "editor.h"

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

TextBuffer text_buffer_new(void) {
    TextBuffer buf;
    buf.lines = NULL;
    buf.length = 0;
    return buf;
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

void launch_editor(TextBuffer *buffer) {
    printf("Enter your pattern lines. End with a single '.' on its own line.\n");
    text_buffer_clear(buffer);

    char line[2048];
    while (1) {
        printf("~ ");
        if (!fgets(line, sizeof(line), stdin)) {
            break;
        }
        if (strcmp(line, ".\n") == 0 || strcmp(line, ".\r\n") == 0) {
            break;
        }
        line[strcspn(line, "\n")] = '\0';
        buffer->lines = realloc(buffer->lines, sizeof(char *) * (buffer->length + 1));
        buffer->lines[buffer->length] = strdup_safe(line);
        buffer->length += 1;
    }

    printf("Buffer captured %zu lines. Use :run to play.\n", buffer->length);
}
