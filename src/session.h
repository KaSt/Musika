#ifndef MUSIKA_SESSION_H
#define MUSIKA_SESSION_H

#include <stdbool.h>

#include "editor.h"
#include "pattern.h"
#include "samplemap.h"

typedef struct {
    TextBuffer buffer;
    Pattern compiled;
    bool has_compiled;
    bool modified;
    char file_path[512];
} MusikaSession;

void session_init(MusikaSession *session);
void session_free(MusikaSession *session);

bool session_load_file(MusikaSession *session, const char *path);
bool session_save(MusikaSession *session, const char *path);
bool session_save_current(MusikaSession *session);

bool session_compile(MusikaSession *session,
                     const SampleRegistry *default_registry,
                     const SampleRegistry *user_registry,
                     Pattern *out_pattern);

static inline bool session_has_file(const MusikaSession *session) { return session && session->file_path[0] != '\0'; }

#endif // MUSIKA_SESSION_H
