#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "config.h"
#include "editor.h"
#include "pattern.h"
#include "samplemap.h"
#include "transport.h"
#include "../audio/audio.h"

static void banner(void) {
    printf("Musika: live-coded terminal groove\n");
    printf("Type :help for commands.\n\n");
}

static void print_help(void) {
    printf("Commands:\n");
    printf("  :help           Show this help text.\n");
    printf("  :config         Display resolved configuration.\n");
    printf("  :samples <src>  Load a Strudel sample map into the user registry.\n");
    printf("  :list-sounds [scope] Show sounds from default and user registries.\n");
    printf("  :edit           Open the inline text editor to craft a pattern.\n");
    printf("  :eval           Evaluate the current buffer into the live transport.\n");
    printf("  :play           Start playback of the active pattern.\n");
    printf("  :stop           Pause playback without clearing the pattern.\n");
    printf("  :panic          Stop playback and clear queued audio.\n");
    printf("  :clear          Clear the buffer.\n");
    printf("  :quit           Exit Musika.\n\n");
    printf("Pattern hints:\n");
    printf("  Use a space-separated sequence of sample names: kick kick\n");
    printf("  Alias 'bd' also triggers the generated kick sample.\n");
    printf("  Use sound:variant to pick a specific variant (wraps if out of range).\n");
    printf("  :list-sounds [default|user|all] controls which registry is shown.\n");
}

static void show_config(const MusikaConfig *config) {
    printf("Audio backend : %s\n", config->audio_backend);
    printf("Sample packs  :\n");
    for (size_t i = 0; i < config->sample_repo_count; ++i) {
        printf("  - %s\n", config->sample_repos[i]);
    }
    printf("Tempo         : %.2f bpm\n", config->tempo_bpm);
}

static int run_beep_mode(void) {
    AudioEngine engine;
    AudioSample tone;
    if (!audio_sample_generate_sine(&tone, 2.5, 48000, 440.0)) {
        fprintf(stderr, "Failed to generate tone.\n");
        return 1;
    }
    if (!audio_engine_init(&engine, 48000, 2)) {
        fprintf(stderr, "Audio init failed.\n");
        audio_sample_free(&tone);
        return 1;
    }

    if (!audio_engine_queue(&engine, &tone, engine.sample_rate / 10)) { // start shortly after boot
        fprintf(stderr, "Beep scheduling failed; exiting beep mode.\n");
        audio_engine_shutdown(&engine);
        audio_sample_free(&tone);
        return 1;
    }

    sleep(3);

    audio_engine_shutdown(&engine);
    audio_sample_free(&tone);
    return 0;
}

static void handle_list_sounds(const SampleRegistry *default_registry, const SampleRegistry *user_registry, const char *arg) {
    const char *filter = NULL;
    if (arg && arg[0] != '\0') {
        filter = arg;
    }
    sample_registry_print_merged(default_registry, user_registry, filter, stdout);
}

static void run_loop(MusikaConfig *config, SampleRegistry *default_registry, SampleRegistry *user_registry) {
    TextBuffer buffer = text_buffer_new();
    char line[2048];
    AudioEngine engine;
    AudioSample samples[1];
    Transport transport;
    Pattern pattern;

    if (!audio_sample_from_wav("assets/kick.wav", &samples[0])) {
        fprintf(stderr, "Failed to load kick sample. Run ./scripts/fetch_kick.sh to generate assets/kick.wav.\n");
        text_buffer_free(&buffer);
        return;
    }
    if (!audio_engine_init(&engine, 48000, 2)) {
        fprintf(stderr, "Audio initialization failed.\n");
        audio_sample_free(&samples[0]);
        text_buffer_free(&buffer);
        return;
    }
    if (!transport_start(&transport, &engine, samples, 1, config->tempo_bpm)) {
        fprintf(stderr, "Transport initialization failed.\n");
        audio_engine_shutdown(&engine);
        audio_sample_free(&samples[0]);
        text_buffer_free(&buffer);
        return;
    }

    banner();
    while (1) {
        printf("> ");
        if (!fgets(line, sizeof(line), stdin)) {
            break;
        }

        line[strcspn(line, "\n")] = '\0';
        if (strcmp(line, ":help") == 0) {
            print_help();
        } else if (strcmp(line, ":config") == 0) {
            show_config(config);
        } else if (strncmp(line, ":samples", 8) == 0) {
            char *arg = line + 8;
            while (*arg && isspace((unsigned char)*arg)) arg++;
            bool refresh = false;
            if (strncmp(arg, "--refresh", 9) == 0) {
                refresh = true;
                arg += 9;
                while (*arg && isspace((unsigned char)*arg)) arg++;
            }
            if (*arg == '\0') {
                printf("Usage: :samples [--refresh] <source>\n");
            } else {
                char cache_path[512];
                char error[256];
                bool from_cache = false;
                char resolved_url[512];
                if (sample_registry_load_from_source(user_registry, arg, "user", refresh, cache_path, sizeof(cache_path), &from_cache, resolved_url, sizeof(resolved_url), error, sizeof(error))) {
                    printf("Resolved to: %s\n", resolved_url);
                    if (refresh || !from_cache) {
                        printf("Fetched and cached: %s\n", cache_path);
                    } else if (from_cache) {
                        printf("Loaded from cache: %s\n", cache_path);
                    }
                    printf("Loaded %zu sounds into user registry from %s\n", user_registry->sound_count, arg);
                } else {
                    printf("Failed to load samples: %s\n", error[0] ? error : "unknown error");
                }
            }
        } else if (strncmp(line, ":list-sounds", 12) == 0) {
            char *arg = line + 12;
            while (*arg && isspace((unsigned char)*arg)) arg++;
            handle_list_sounds(default_registry, user_registry, *arg ? arg : "all");
        } else if (strcmp(line, ":edit") == 0) {
            launch_editor(&buffer);
        } else if (strcmp(line, ":eval") == 0) {
            if (buffer.length == 0) {
                printf("Buffer is empty. Use :edit to add a pattern.\n");
                continue;
            }
            if (pattern_from_lines(buffer.lines, buffer.length, default_registry, user_registry, &pattern)) {
                transport_set_pattern(&transport, &pattern);
                printf("Pattern loaded into transport. Use :play to hear it.\n");
            } else {
                printf("Pattern is empty; nothing to evaluate.\n");
            }
        } else if (strcmp(line, ":play") == 0) {
            transport_play(&transport);
            printf("Playback started.\n");
        } else if (strcmp(line, ":stop") == 0) {
            transport_pause(&transport);
            printf("Playback paused.\n");
        } else if (strcmp(line, ":panic") == 0) {
            transport_panic(&transport);
            printf("Transport and queues cleared.\n");
        } else if (strcmp(line, ":clear") == 0) {
            text_buffer_clear(&buffer);
            printf("Buffer cleared.\n");
        } else if (strcmp(line, ":quit") == 0 || strcmp(line, ":q") == 0) {
            break;
        } else if (line[0] == '\0') {
            continue;
        } else {
            printf("Unknown command. Type :help for options.\n");
        }
    }

    transport_stop(&transport);
    audio_engine_shutdown(&engine);
    audio_sample_free(&samples[0]);
    text_buffer_free(&buffer);
}

int main(int argc, char **argv) {
    MusikaConfig config;
    SampleRegistry default_registry;
    SampleRegistry user_registry;
    memset(&user_registry, 0, sizeof(user_registry));
    const char *config_path = "config.json";
    load_config(config_path, &config);

    if (!sample_registry_load_default(&default_registry)) {
        fprintf(stderr, "Failed to load default sample map.\n");
        free_config(&config);
        return 1;
    }

    bool list_sounds = false;
    const char *list_filter = "all";
    const char *user_source = NULL;
    bool refresh_samples = false;
    bool beep_mode = false;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--list-sounds") == 0) {
            list_sounds = true;
        } else if (strcmp(argv[i], "--registry") == 0 && i + 1 < argc) {
            list_filter = argv[++i];
        } else if (strcmp(argv[i], "--samples") == 0 && i + 1 < argc) {
            user_source = argv[++i];
        } else if (strcmp(argv[i], "--refresh-samples") == 0) {
            refresh_samples = true;
        } else if (strcmp(argv[i], "--beep") == 0) {
            beep_mode = true;
        }
    }

    if (beep_mode) {
        int rc = run_beep_mode();
        sample_registry_free(&default_registry);
        free_config(&config);
        return rc;
    }

    if (user_source) {
        char cache_path[512];
        char error[256];
        char resolved_url[512];
        bool from_cache = false;
        if (!sample_registry_load_from_source(&user_registry, user_source, "user", refresh_samples, cache_path, sizeof(cache_path), &from_cache, resolved_url, sizeof(resolved_url), error, sizeof(error))) {
            fprintf(stderr, "Failed to load user samples: %s\n", error[0] ? error : "unknown error");
        } else {
            printf("Resolved to: %s\n", resolved_url);
            if (refresh_samples || !from_cache) {
                printf("Fetched and cached: %s\n", cache_path);
            } else if (from_cache) {
                printf("Loaded from cache: %s\n", cache_path);
            }
            printf("Loaded %zu sounds into user registry from %s\n", user_registry.sound_count, user_source);
        }
    }

    if (list_sounds) {
        sample_registry_print_merged(&default_registry, &user_registry, list_filter, stdout);
        sample_registry_free(&user_registry);
        sample_registry_free(&default_registry);
        free_config(&config);
        return 0;
    }

    run_loop(&config, &default_registry, &user_registry);
    sample_registry_free(&user_registry);
    sample_registry_free(&default_registry);
    free_config(&config);
    return 0;
}
