#include "session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void session_init(MusikaSession *session) {
    if (!session) return;
    memset(session, 0, sizeof(*session));
    text_buffer_init(&session->buffer);
    session->modified = false;
}

void session_free(MusikaSession *session) {
    if (!session) return;
    text_buffer_free(&session->buffer);
}

bool session_load_file(MusikaSession *session, const char *path) {
    if (!session || !path) return false;
    if (!text_buffer_load_file(&session->buffer, path)) {
        return false;
    }
    strncpy(session->file_path, path, sizeof(session->file_path) - 1);
    session->file_path[sizeof(session->file_path) - 1] = '\0';
    session->modified = false;
    session->has_compiled = false;
    return true;
}

bool session_save(MusikaSession *session, const char *path) {
    if (!session || !path) return false;
    if (!text_buffer_save_to_path(&session->buffer, path)) return false;
    strncpy(session->file_path, path, sizeof(session->file_path) - 1);
    session->file_path[sizeof(session->file_path) - 1] = '\0';
    session->modified = false;
    return true;
}

bool session_save_current(MusikaSession *session) {
    if (!session || session->file_path[0] == '\0') return false;
    return session_save(session, session->file_path);
}

bool session_compile(MusikaSession *session,
                     const SampleRegistry *default_registry,
                     const SampleRegistry *user_registry,
                     Pattern *out_pattern) {
    if (!session || !out_pattern) return false;
    if (!pattern_from_lines(session->buffer.lines, session->buffer.length, default_registry, user_registry, out_pattern)) {
        return false;
    }
    session->compiled = *out_pattern;
    session->has_compiled = true;
    return true;
}
