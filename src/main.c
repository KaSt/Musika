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
    printf("  :samples        List sample packs from configuration.\n");
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

static void run_loop(MusikaConfig *config) {
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
        } else if (strcmp(line, ":samples") == 0) {
            for (size_t i = 0; i < config->sample_repo_count; ++i) {
                printf("[%zu] %s\n", i + 1, config->sample_repos[i]);
            }
        } else if (strcmp(line, ":edit") == 0) {
            launch_editor(&buffer);
        } else if (strcmp(line, ":eval") == 0) {
            if (buffer.length == 0) {
                printf("Buffer is empty. Use :edit to add a pattern.\n");
                continue;
            }
            if (pattern_from_lines(buffer.lines, buffer.length, &pattern)) {
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
    SampleRegistry registry;
    const char *config_path = "config.json";
    load_config(config_path, &config);

    if (!sample_registry_load_default(&registry)) {
        fprintf(stderr, "Failed to load default sample map.\n");
        free_config(&config);
        return 1;
    }

    if (argc > 1 && strcmp(argv[1], "--list-sounds") == 0) {
        sample_registry_print(&registry, stdout);
        sample_registry_free(&registry);
        free_config(&config);
        return 0;
    }

    if (argc > 1 && strcmp(argv[1], "--beep") == 0) {
        int rc = run_beep_mode();
        sample_registry_free(&registry);
        free_config(&config);
        return rc;
    }

    run_loop(&config);
    sample_registry_free(&registry);
    free_config(&config);
    return 0;
}
