#include "tui.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

typedef enum {
    MODE_NORMAL = 0,
    MODE_INSERT,
    MODE_COMMAND
} EditorMode;

typedef struct {
    EditorContext *ctx;
    EditorMode mode;
    size_t row;
    size_t col;
    char command[256];
    size_t command_len;
    char message[256];
    bool running;
    bool pending_delete;
} EditorState;

static struct termios orig_termios;

static void disable_raw_mode(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

static bool enable_raw_mode(void) {
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) return false;
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_iflag &= ~(IXON | ICRNL);
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) return false;
    atexit(disable_raw_mode);
    return true;
}

static void set_message(EditorState *state, const char *msg) {
    if (!state) return;
    snprintf(state->message, sizeof(state->message), "%s", msg ? msg : "");
}

static void clamp_cursor(EditorState *state) {
    if (!state) return;
    TextBuffer *buf = &state->ctx->session->buffer;
    if (state->row >= buf->length) {
        state->row = buf->length ? buf->length - 1 : 0;
    }
    size_t len = text_buffer_line_length(buf, state->row);
    if (state->col > len) state->col = len;
}

static void draw_status(EditorState *state, size_t rows) {
    const char *mode_name = state->mode == MODE_INSERT ? "INSERT" : (state->mode == MODE_COMMAND ? "COMMAND" : "NORMAL");
    const char *file = session_has_file(state->ctx->session) ? state->ctx->session->file_path : "[new]";
    printf("\x1b[%zu;1H", rows);
    printf("[%s] %s | bpm %.2f", mode_name, file, state->ctx->config ? state->ctx->config->tempo_bpm : 0.0);
    if (state->ctx->transport) {
        printf(" | %s", "ready");
    }
    printf("\x1b[K\n");
    if (state->mode == MODE_COMMAND) {
        printf(":%s\x1b[K\n", state->command);
    } else if (state->message[0]) {
        printf("%s\x1b[K\n", state->message);
    } else {
        printf("\x1b[K\n");
    }
}

static void render(EditorState *state) {
    struct winsize ws;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
    size_t rows = ws.ws_row > 2 ? ws.ws_row - 2 : 2;
    printf("\x1b[2J\x1b[H");
    TextBuffer *buf = &state->ctx->session->buffer;
    for (size_t i = 0; i < buf->length && i < rows; ++i) {
        const char *line = buf->lines[i] ? buf->lines[i] : "";
        printf("%s\x1b[K\n", line);
    }
    draw_status(state, rows);
    printf("\x1b[%zu;%zuH", state->row + 1, state->col + 1);
    fflush(stdout);
}

static void insert_character(EditorState *state, char c) {
    text_buffer_insert_char(&state->ctx->session->buffer, state->row, state->col, c);
    state->ctx->session->modified = true;
    state->col += 1;
}

static void insert_newline(EditorState *state) {
    text_buffer_insert_newline(&state->ctx->session->buffer, state->row, state->col);
    state->ctx->session->modified = true;
    state->row += 1;
    state->col = 0;
}

static void delete_under_cursor(EditorState *state) {
    TextBuffer *buf = &state->ctx->session->buffer;
    size_t len = text_buffer_line_length(buf, state->row);
    if (len == 0 || state->col >= len) return;
    text_buffer_delete_char(buf, state->row, state->col);
    state->ctx->session->modified = true;
}

static void backspace(EditorState *state) {
    TextBuffer *buf = &state->ctx->session->buffer;
    if (state->col > 0) {
        state->col -= 1;
        text_buffer_delete_char(buf, state->row, state->col);
        state->ctx->session->modified = true;
        return;
    }
    if (state->row == 0) return;
    size_t prev_len = text_buffer_line_length(buf, state->row - 1);
    char *current_line = buf->lines[state->row];
    size_t current_len = current_line ? strlen(current_line) : 0;
    buf->lines[state->row - 1] = realloc(buf->lines[state->row - 1], prev_len + current_len + 1);
    memcpy(buf->lines[state->row - 1] + prev_len, current_line ? current_line : "", current_len + 1);
    text_buffer_delete_line(buf, state->row);
    state->row -= 1;
    state->col = prev_len;
    state->ctx->session->modified = true;
}

static void delete_line(EditorState *state) {
    text_buffer_delete_line(&state->ctx->session->buffer, state->row);
    clamp_cursor(state);
    state->ctx->session->modified = true;
}

static void handle_command(EditorState *state) {
    state->command[state->command_len] = '\0';
    if (strcmp(state->command, "q") == 0) {
        if (state->ctx->session->modified) {
            set_message(state, "Unsaved changes (use :q!)");
        } else {
            state->running = false;
        }
    } else if (strcmp(state->command, "q!") == 0) {
        state->running = false;
    } else if (strncmp(state->command, "wq", 2) == 0) {
        const char *path = NULL;
        if (state->command[2] == ' ' && state->command[3] != '\0') {
            path = state->command + 3;
        }
        if (path) {
            if (!session_save(state->ctx->session, path)) {
                set_message(state, "Failed to save file");
                state->mode = MODE_NORMAL;
                return;
            }
        } else if (!session_save_current(state->ctx->session)) {
            set_message(state, "No file name. Use :w <path>");
            state->mode = MODE_NORMAL;
            return;
        }
        state->running = false;
    } else if (strncmp(state->command, "w", 1) == 0) {
        const char *path = NULL;
        if (state->command[1] == ' ' && state->command[2] != '\0') {
            path = state->command + 2;
        }
        if (path) {
            if (session_save(state->ctx->session, path)) {
                set_message(state, "Written");
            } else {
                set_message(state, "Failed to save file");
            }
        } else if (session_save_current(state->ctx->session)) {
            set_message(state, "Written");
        } else {
            set_message(state, "No file name. Use :w <path>");
        }
    } else if (strcmp(state->command, "play") == 0) {
        Pattern compiled;
        if (!session_compile(state->ctx->session, state->ctx->default_registry, state->ctx->user_registry, &compiled)) {
            set_message(state, "Failed to compile pattern");
        } else {
            transport_set_pattern(state->ctx->transport, &compiled);
            transport_play(state->ctx->transport);
            set_message(state, "Playing");
        }
    } else if (strcmp(state->command, "stop") == 0) {
        transport_pause(state->ctx->transport);
        set_message(state, "Stopped");
    } else {
        set_message(state, "Unknown command");
    }
    state->mode = MODE_NORMAL;
    state->pending_delete = false;
}

static void process_normal(EditorState *state, char c) {
    if (c == 27) {
        state->mode = MODE_NORMAL;
        state->pending_delete = false;
        return;
    }
    if (c == 'i') {
        state->mode = MODE_INSERT;
        state->pending_delete = false;
    } else if (c == ':') {
        state->mode = MODE_COMMAND;
        state->command_len = 0;
        state->command[0] = '\0';
        state->pending_delete = false;
    } else if (c == 'h') {
        if (state->col > 0) state->col -= 1;
        state->pending_delete = false;
    } else if (c == 'l') {
        size_t len = text_buffer_line_length(&state->ctx->session->buffer, state->row);
        if (state->col < len) state->col += 1;
        state->pending_delete = false;
    } else if (c == 'k') {
        if (state->row > 0) state->row -= 1;
        clamp_cursor(state);
        state->pending_delete = false;
    } else if (c == 'j') {
        if (state->row + 1 < state->ctx->session->buffer.length) state->row += 1;
        clamp_cursor(state);
        state->pending_delete = false;
    } else if (c == 'x') {
        delete_under_cursor(state);
        state->pending_delete = false;
    } else if (c == 'd') {
        if (state->pending_delete) {
            delete_line(state);
            state->pending_delete = false;
        } else {
            state->pending_delete = true;
        }
    } else {
        state->pending_delete = false;
    }
}

static void process_insert(EditorState *state, char c) {
    if (c == 27) { // ESC
        state->mode = MODE_NORMAL;
        return;
    }
    if (c == 127 || c == 8) {
        backspace(state);
        return;
    }
    if (c == '\r' || c == '\n') {
        insert_newline(state);
        return;
    }
    if (isprint((unsigned char)c) || c == '\t') {
        insert_character(state, c);
    }
}

static void process_command(EditorState *state, char c) {
    if (c == 27) { // ESC
        state->mode = MODE_NORMAL;
        return;
    }
    if (c == '\r' || c == '\n') {
        handle_command(state);
        return;
    }
    if ((c == 127 || c == 8) && state->command_len > 0) {
        state->command_len -= 1;
        state->command[state->command_len] = '\0';
        return;
    }
    if (isprint((unsigned char)c) && state->command_len + 1 < sizeof(state->command)) {
        state->command[state->command_len++] = c;
        state->command[state->command_len] = '\0';
    }
}

int run_editor(EditorContext *ctx) {
    if (!ctx || !ctx->session) return 1;
    if (!enable_raw_mode()) {
        fprintf(stderr, "Failed to enter raw mode.\n");
        return 1;
    }

    EditorState state = {0};
    state.ctx = ctx;
    state.mode = MODE_NORMAL;
    state.running = true;
    set_message(&state, "Entering editor. ESC for NORMAL, i for INSERT.");

    while (state.running) {
        clamp_cursor(&state);
        render(&state);
        char c = 0;
        if (read(STDIN_FILENO, &c, 1) <= 0) {
            break;
        }
        switch (state.mode) {
            case MODE_INSERT:
                process_insert(&state, c);
                break;
            case MODE_COMMAND:
                process_command(&state, c);
                break;
            default:
                process_normal(&state, c);
                break;
        }
    }

    disable_raw_mode();
    printf("\x1b[2J\x1b[H");
    fflush(stdout);
    return 0;
}
