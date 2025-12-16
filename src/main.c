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
#include "midi_dump.h"
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

static int run_midi_dump_mode(MusikaRuntime *rt, const char *input_path, const char *output_path) {
    if (!rt || !input_path || !output_path) return 1;
    MusikaSession session;
    session_init(&session);
    if (!session_load_file(&session, input_path)) {
        fprintf(stderr, "Failed to open %s\n", input_path);
        session_free(&session);
        return 1;
    }

    Pattern compiled;
    if (!session_compile(&session, &rt->default_registry, &rt->user_registry, &compiled)) {
        fprintf(stderr, "Failed to compile %s\n", input_path);
        session_free(&session);
        return 1;
    }

    bool ok = midi_dump_pattern(&compiled, rt->config.tempo_bpm, output_path);
    session_free(&session);
    if (!ok) {
        fprintf(stderr, "Failed to write MIDI dump to %s\n", output_path);
        return 1;
    }
    printf("Wrote MIDI dump to %s\n", output_path);
    return 0;
}

static void print_usage(const char *prog) {
    printf("Usage: %s [--edit] [--play <file>] [--check <file>] [--midi-dump <path>] [file]\n", prog);
    printf("       %s --version\n", prog);
}

int main(int argc, char **argv) {
    const char *positional_file = NULL;
    const char *play_file = NULL;
    const char *check_file = NULL;
    const char *midi_dump_path = NULL;
    bool force_edit = false;
    bool show_version = false;

    bool error = false;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--play") == 0 && i + 1 < argc) {
            play_file = argv[++i];
        } else if (strcmp(argv[i], "--edit") == 0) {
            force_edit = true;
        } else if (strcmp(argv[i], "--check") == 0 && i + 1 < argc) {
            check_file = argv[++i];
        } else if (strcmp(argv[i], "--midi-dump") == 0 && i + 1 < argc) {
            midi_dump_path = argv[++i];
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
            if (positional_file) {
                fprintf(stderr, "Multiple input files specified (%s and %s)\n", positional_file, argv[i]);
                error = true;
            } else {
                positional_file = argv[i];
            }
        }
    }

    if (error) return 1;

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

    int rc = 0;

    typedef enum {
        MODE_NONE,
        MODE_PLAY,
        MODE_EDIT,
        MODE_CHECK,
        MODE_MIDI_DUMP,
    } MusikaMode;

    MusikaMode mode = MODE_NONE;
    const char *input_path = NULL;

    if (check_file) {
        mode = MODE_CHECK;
        input_path = check_file;
    }

    if (midi_dump_path) {
        if (mode != MODE_NONE) {
            fprintf(stderr, "--midi-dump cannot be combined with other modes.\n");
            musika_runtime_free(&runtime);
            return 1;
        }
        mode = MODE_MIDI_DUMP;
    }

    if (force_edit) {
        if (mode != MODE_NONE) {
            fprintf(stderr, "--edit conflicts with other modes.\n");
            musika_runtime_free(&runtime);
            return 1;
        }
        mode = MODE_EDIT;
    }

    if (play_file) {
        if (mode != MODE_NONE && mode != MODE_PLAY) {
            fprintf(stderr, "--play conflicts with other modes.\n");
            musika_runtime_free(&runtime);
            return 1;
        }
        mode = MODE_PLAY;
        input_path = play_file;
    }

    if (mode == MODE_NONE) {
        if (positional_file) {
            mode = MODE_PLAY;
            input_path = positional_file;
        } else {
            mode = MODE_EDIT;
        }
    }

    if (positional_file && mode == MODE_CHECK) {
        fprintf(stderr, "--check already specifies an input file; remove the extra positional argument.\n");
        musika_runtime_free(&runtime);
        return 1;
    }

    if (mode == MODE_PLAY && positional_file && play_file && strcmp(positional_file, play_file) != 0) {
        fprintf(stderr, "Both --play and positional file provided; choose one.\n");
        musika_runtime_free(&runtime);
        return 1;
    }

    if (mode == MODE_MIDI_DUMP && !input_path && positional_file) {
        input_path = positional_file;
    }

    if ((mode == MODE_PLAY || mode == MODE_CHECK || mode == MODE_MIDI_DUMP) && !input_path) {
        fprintf(stderr, "A file path is required for this mode.\n");
        musika_runtime_free(&runtime);
        return 1;
    }

    switch (mode) {
        case MODE_CHECK:
            rc = run_check_mode(&runtime, input_path);
            break;
        case MODE_MIDI_DUMP:
            if (!positional_file && play_file) {
                input_path = play_file;
            } else if (!input_path) {
                input_path = positional_file;
            }
            if (!input_path) {
                fprintf(stderr, "--midi-dump requires an input file path.\n");
                rc = 1;
                break;
            }
            rc = run_midi_dump_mode(&runtime, input_path, midi_dump_path);
            break;
        case MODE_EDIT:
            rc = run_editor_mode(&runtime, positional_file);
            break;
        case MODE_PLAY:
            rc = run_play_mode(&runtime, input_path);
            break;
        default:
            rc = 1;
            break;
    }

    musika_runtime_free(&runtime);
    return rc;
}
