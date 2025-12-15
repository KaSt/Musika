#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "config.h"
#include "pattern.h"
#include "samplemap.h"
#include "session.h"
#include "transport.h"
#include "tui.h"
#include "../audio/audio.h"

#define MUSIKA_VERSION "0.2.0"

typedef struct {
    MusikaConfig config;
    SampleRegistry default_registry;
    SampleRegistry user_registry;
    AudioEngine engine;
    AudioSample kick;
    Transport transport;
    bool audio_ready;
    bool transport_ready;
    bool registries_ready;
} MusikaRuntime;

static volatile sig_atomic_t stop_requested = 0;

static void handle_sigint(int signo) {
    (void)signo;
    stop_requested = 1;
}

static void register_sigint_handler(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigint;
    sigaction(SIGINT, &sa, NULL);
}

static void musika_runtime_init(MusikaRuntime *rt) {
    memset(rt, 0, sizeof(*rt));
}

static bool musika_runtime_load(MusikaRuntime *rt) {
    if (!rt) return false;
    const char *config_path = "config.json";
    load_config(config_path, &rt->config);
    if (!sample_registry_load_default(&rt->default_registry)) {
        fprintf(stderr, "Failed to load default sample map.\n");
        return false;
    }
    memset(&rt->user_registry, 0, sizeof(rt->user_registry));
    rt->registries_ready = true;
    return true;
}

static bool musika_runtime_start_audio(MusikaRuntime *rt) {
    if (!rt) return false;
    if (rt->audio_ready && rt->transport_ready) return true;
    if (!audio_sample_from_wav("assets/kick.wav", &rt->kick)) {
        fprintf(stderr, "Failed to load kick sample. Run ./scripts/fetch_kick.sh to generate assets/kick.wav.\n");
        return false;
    }
    if (!audio_engine_init(&rt->engine, 48000, 2)) {
        fprintf(stderr, "Audio initialization failed.\n");
        audio_sample_free(&rt->kick);
        return false;
    }
    if (!transport_start(&rt->transport, &rt->engine, &rt->kick, 1, rt->config.tempo_bpm)) {
        fprintf(stderr, "Transport initialization failed.\n");
        audio_engine_shutdown(&rt->engine);
        audio_sample_free(&rt->kick);
        return false;
    }
    rt->audio_ready = true;
    rt->transport_ready = true;
    return true;
}

static void musika_runtime_stop_audio(MusikaRuntime *rt) {
    if (!rt) return;
    if (rt->transport_ready) {
        transport_stop(&rt->transport);
    }
    if (rt->audio_ready) {
        audio_engine_shutdown(&rt->engine);
    }
    if (rt->kick.data) {
        audio_sample_free(&rt->kick);
    }
    rt->transport_ready = false;
    rt->audio_ready = false;
}

static void musika_runtime_free(MusikaRuntime *rt) {
    musika_runtime_stop_audio(rt);
    if (rt->registries_ready) {
        sample_registry_free(&rt->user_registry);
        sample_registry_free(&rt->default_registry);
    }
    free_config(&rt->config);
}

static int run_check_mode(MusikaRuntime *rt, const char *path) {
    MusikaSession session;
    session_init(&session);
    if (!session_load_file(&session, path)) {
        fprintf(stderr, "Failed to read %s\n", path);
        session_free(&session);
        return 1;
    }
    Pattern compiled;
    bool ok = session_compile(&session, &rt->default_registry, &rt->user_registry, &compiled);
    session_free(&session);
    if (ok) {
        printf("%s: OK\n", path);
        return 0;
    }
    printf("%s: FAILED\n", path);
    return 1;
}

static int run_play_mode(MusikaRuntime *rt, const char *path) {
    if (!musika_runtime_start_audio(rt)) {
        return 1;
    }
    MusikaSession session;
    session_init(&session);
    if (!session_load_file(&session, path)) {
        fprintf(stderr, "Failed to read %s\n", path);
        session_free(&session);
        return 1;
    }
    Pattern compiled;
    if (!session_compile(&session, &rt->default_registry, &rt->user_registry, &compiled)) {
        fprintf(stderr, "Failed to compile %s\n", path);
        session_free(&session);
        return 1;
    }
    transport_set_pattern(&rt->transport, &compiled);
    transport_play(&rt->transport);
    printf("Playing %s @ %.2f bpm. Press Ctrl+C to stop.\n", path, rt->config.tempo_bpm);
    register_sigint_handler();
    struct timespec ts = {0, 100000000};
    while (!stop_requested) {
        nanosleep(&ts, NULL);
    }
    stop_requested = 0;
    transport_pause(&rt->transport);
    session_free(&session);
    return 0;
}

static int run_editor_mode(MusikaRuntime *rt, const char *path) {
    if (!musika_runtime_start_audio(rt)) {
        return 1;
    }
    MusikaSession session;
    session_init(&session);
    if (path) {
        if (!session_load_file(&session, path)) {
            fprintf(stderr, "Failed to open %s. Starting with empty buffer.\n", path);
        }
    }
    EditorContext ctx = {
        .session = &session,
        .transport = &rt->transport,
        .config = &rt->config,
        .default_registry = &rt->default_registry,
        .user_registry = &rt->user_registry,
    };
    int rc = run_editor(&ctx);
    session_free(&session);
    return rc;
}

static void print_usage(const char *prog) {
    printf("Usage: %s [--edit] [--play <file>] [--check <file>] [file]\n", prog);
    printf("       %s --version\n", prog);
}

int main(int argc, char **argv) {
    const char *positional_file = NULL;
    const char *play_file = NULL;
    const char *check_file = NULL;
    bool force_edit = false;
    bool show_version = false;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--play") == 0 && i + 1 < argc) {
            play_file = argv[++i];
        } else if (strcmp(argv[i], "--edit") == 0) {
            force_edit = true;
        } else if (strcmp(argv[i], "--check") == 0 && i + 1 < argc) {
            check_file = argv[++i];
        } else if (strcmp(argv[i], "--version") == 0) {
            show_version = true;
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        } else {
            positional_file = argv[i];
        }
    }

    if (show_version) {
        printf("Musika %s\n", MUSIKA_VERSION);
        return 0;
    }

    MusikaRuntime runtime;
    musika_runtime_init(&runtime);
    if (!musika_runtime_load(&runtime)) {
        musika_runtime_free(&runtime);
        return 1;
    }

    if (check_file) {
        int rc = run_check_mode(&runtime, check_file);
        musika_runtime_free(&runtime);
        return rc;
    }

    if (play_file && positional_file) {
        fprintf(stderr, "Both positional file and --play provided; using --play.\n");
    }

    int rc = 0;
    if (force_edit || (!play_file && !positional_file)) {
        const char *to_open = force_edit ? (positional_file ? positional_file : play_file) : NULL;
        rc = run_editor_mode(&runtime, to_open);
    } else if (play_file) {
        rc = run_play_mode(&runtime, play_file);
    } else if (positional_file) {
        rc = run_play_mode(&runtime, positional_file);
    }

    musika_runtime_free(&runtime);
    return rc;
}
