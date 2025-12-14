#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "engine.h"
#include "editor.h"

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
    printf("  :run            Run the current buffer.\n");
    printf("  :clear          Clear the buffer.\n");
    printf("  :quit           Exit Musika.\n\n");
    printf("Pattern hints:\n");
    printf("  Use a space-separated sequence of sample names: bd sn hh\n");
    printf("  Apply tempo scaling with fast(N) or slow(N): fast(2) bd sn\n");
    printf("  Stack patterns with [a b] to interleave voices.\n");
}

static void show_config(const MusikaConfig *config) {
    printf("Audio backend : %s\n", config->audio_backend);
    printf("Sample packs  :\n");
    for (size_t i = 0; i < config->sample_repo_count; ++i) {
        printf("  - %s\n", config->sample_repos[i]);
    }
    printf("Tempo         : %.2f bpm\n", config->tempo_bpm);
}

static void run_loop(MusikaConfig *config) {
    TextBuffer buffer = text_buffer_new();
    char line[2048];

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
        } else if (strcmp(line, ":run") == 0) {
            if (buffer.length == 0) {
                printf("Buffer is empty. Use :edit to add a pattern.\n");
                continue;
            }
            printf("\n== Performing pattern ==\n");
            EngineContext ctx = engine_context_new(*config);
            render_script(&ctx, buffer.lines, buffer.length);
            engine_context_free(&ctx);
            printf("== End ==\n\n");
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

    text_buffer_free(&buffer);
}

int main(void) {
    MusikaConfig config;
    const char *config_path = "config.json";
    load_config(config_path, &config);
    run_loop(&config);
    free_config(&config);
    return 0;
}
