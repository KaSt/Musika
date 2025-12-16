#ifndef MUSIKA_TUI_H
#define MUSIKA_TUI_H

#include "session.h"
#include "transport.h"
#include "config.h"

typedef struct {
    MusikaSession *session;
    Transport *transport;
    const MusikaConfig *config;
    const SampleRegistry *default_registry;
    const SampleRegistry *user_registry;
} EditorContext;

int run_editor(EditorContext *ctx);

#endif // MUSIKA_TUI_H
