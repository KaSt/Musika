#include "pattern.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { TOKEN_BUFFER_LEN = 64 };

enum { BASE_MIDI_TONE = 69 };

typedef enum {
    NOTE_PARSE_NONE,
    NOTE_PARSE_OK,
    NOTE_PARSE_REST,
} NoteParseResult;

typedef struct {
    double duration_beats;
    double playback_rate;
    int midi_note;
    bool has_midi_note;
} NoteStep;

static void add_step(Pattern *pattern, const PatternStep *step) {
    if (pattern->step_count >= sizeof(pattern->steps) / sizeof(pattern->steps[0])) return;
    pattern->steps[pattern->step_count] = *step;
    pattern->step_count += 1;
}

static int semitone_for_letter(char c) {
    switch (tolower((unsigned char)c)) {
        case 'c': return 0;
        case 'd': return 2;
        case 'e': return 4;
        case 'f': return 5;
        case 'g': return 7;
        case 'a': return 9;
        case 'b': return 11;
        default: return -1;
    }
}

static bool pick_pitched_variant(const SampleSound *sound, int midi_note, size_t *variant_index, int *base_midi) {
    if (!sound || sound->pitched_entry_count == 0 || !sound->pitched_midi) return false;
    size_t best_idx = 0;
    int best_midi = sound->pitched_midi[0];
    int best_diff = abs(midi_note - best_midi);
    for (size_t i = 1; i < sound->pitched_entry_count; ++i) {
        int midi = sound->pitched_midi[i];
        int diff = abs(midi_note - midi);
        if (diff < best_diff) {
            best_diff = diff;
            best_idx = i;
            best_midi = midi;
        }
    }
    if (variant_index) *variant_index = best_idx;
    if (base_midi) *base_midi = best_midi;
    return true;
}

static double parse_duration_beats(const char *text) {
    const int default_divisor = 4;
    if (!text || text[0] == '\0') return 4.0 / (double)default_divisor;
    char *end = NULL;
    long denom = strtol(text, &end, 10);
    if (end == text || denom <= 0) {
        fprintf(stderr, "Warning: invalid duration '/%s' (defaulting to /%d)\n", text, default_divisor);
        return 4.0 / (double)default_divisor;
    }
    return 4.0 / (double)denom;
}

static NoteParseResult parse_note_token(const char *token, NoteStep *out_step) {
    if (!token || !out_step) return NOTE_PARSE_NONE;

    char token_copy[TOKEN_BUFFER_LEN];
    strncpy(token_copy, token, sizeof(token_copy) - 1);
    token_copy[sizeof(token_copy) - 1] = '\0';

    char *duration_part = strchr(token_copy, '/');
    if (duration_part) {
        *duration_part = '\0';
        duration_part += 1;
    }

    int midi = -1;
    bool parsed = false;
    bool clamped = false;

    if (token_copy[0] == 'k' || token_copy[0] == 'K') {
        char *end = NULL;
        long key_num = strtol(token_copy + 1, &end, 10);
        if (end == token_copy + 1 || (*end != '\0' && !isspace((unsigned char)*end))) {
            fprintf(stderr, "Warning: unknown note token '%s' (treated as rest)\n", token_copy);
        } else {
            if (key_num < 1) {
                key_num = 1;
                clamped = true;
            } else if (key_num > 88) {
                key_num = 88;
                clamped = true;
            }
            if (clamped) {
                fprintf(stderr, "Warning: piano key clamped to %ld for token '%s'\n", key_num, token_copy);
            }
            midi = (int)(20 + key_num);
            parsed = true;
        }
    } else if (isdigit((unsigned char)token_copy[0]) ||
               ((token_copy[0] == '-' || token_copy[0] == '+') && isdigit((unsigned char)token_copy[1]))) {
        char *end = NULL;
        long midi_num = strtol(token_copy, &end, 10);
        if (end == token_copy || (*end != '\0' && !isspace((unsigned char)*end))) {
            fprintf(stderr, "Warning: unknown note token '%s' (treated as rest)\n", token_copy);
        } else {
            if (midi_num < 0) {
                midi_num = 0;
                clamped = true;
            } else if (midi_num > 127) {
                midi_num = 127;
                clamped = true;
            }
            if (clamped) {
                fprintf(stderr, "Warning: MIDI note clamped to %ld for token '%s'\n", midi_num, token_copy);
            }
            midi = (int)midi_num;
            parsed = true;
        }
    } else {
        int base = semitone_for_letter(token_copy[0]);
        if (base < 0) {
            return NOTE_PARSE_NONE;
        }

        size_t idx = 1;
        int accidental = 0;
        char accidental_char = token_copy[idx];
        if (accidental_char == '#' || tolower((unsigned char)accidental_char) == 'b') {
            accidental = (accidental_char == '#') ? 1 : -1;
            idx++;
        }

        if (!isdigit((unsigned char)token_copy[idx])) {
            fprintf(stderr, "Warning: unknown note token '%s' (treated as rest)\n", token_copy);
        } else {
            char *end = NULL;
            long octave = strtol(&token_copy[idx], &end, 10);
            if (end == &token_copy[idx] || (*end != '\0' && !isspace((unsigned char)*end))) {
                fprintf(stderr, "Warning: unknown note token '%s' (treated as rest)\n", token_copy);
            } else {
                if (octave < 0) {
                    octave = 0;
                    clamped = true;
                } else if (octave > 8) {
                    octave = 8;
                    clamped = true;
                }
                if (clamped) {
                    fprintf(stderr, "Warning: octave clamped to %ld for token '%s'\n", octave, token_copy);
                }

                midi = (int)((octave + 1) * 12 + base + accidental);
                parsed = true;
            }
        }
    }

    out_step->duration_beats = parse_duration_beats(duration_part);
    out_step->playback_rate = 1.0;
    out_step->has_midi_note = parsed;
    out_step->midi_note = midi;
    if (!parsed) {
        return NOTE_PARSE_REST;
    }

    double rate = pow(2.0, ((double)midi - (double)BASE_MIDI_TONE) / 12.0);
    out_step->playback_rate = rate;
    return NOTE_PARSE_OK;
}

static bool parse_variant_index(const char *text, size_t *out_index) {
    if (!text || !out_index) return false;
    if (text[0] == '\0') {
        *out_index = 0;
        return true;
    }
    for (const char *p = text; *p; ++p) {
        if (!isdigit((unsigned char)*p)) {
            return false;
        }
    }
    *out_index = (size_t)strtoul(text, NULL, 10);
    return true;
}

static SampleRef resolve_sample(const char *token, const SampleRegistry *default_registry, const SampleRegistry *user_registry) {
    SampleRef ref = {0};
    ref.valid = false;
    if (!token) return ref;
    bool has_registry = (default_registry != NULL) || (user_registry != NULL);

    char token_copy[TOKEN_BUFFER_LEN];
    strncpy(token_copy, token, sizeof(token_copy) - 1);
    token_copy[sizeof(token_copy) - 1] = '\0';

    if (token_copy[0] == '~' && token_copy[1] == '\0') {
        return ref;
    }

    char *variant_sep = strchr(token_copy, ':');
    size_t variant_index = 0;
    if (variant_sep) {
        *variant_sep = '\0';
        if (!parse_variant_index(variant_sep + 1, &variant_index)) {
            fprintf(stderr, "Warning: invalid variant index '%s' for sound '%s' (treated as rest)\n", variant_sep + 1, token_copy);
            return ref;
        }
    }

    const SampleSound *sound = NULL;
    const SampleRegistry *registry = NULL;
    if (user_registry) {
        sound = sample_registry_find_sound(user_registry, token_copy);
        registry = user_registry;
    }
    if (!sound && default_registry) {
        sound = sample_registry_find_sound(default_registry, token_copy);
        registry = default_registry;
    }

    if (!sound || sound->variant_count == 0 || !has_registry) {
        fprintf(stderr, "Unknown sound '%s' (treated as rest)\n", token_copy);
        return ref;
    }

    if (sound->variant_count > 0) {
        if (variant_index >= sound->variant_count) {
            variant_index = variant_index % sound->variant_count;
        }
    }

    ref.registry = registry;
    ref.sound = sound;
    ref.variant_index = variant_index;
    ref.valid = true;
    return ref;
}

bool pattern_from_lines(char **lines, size_t line_count, const SampleRegistry *default_registry, const SampleRegistry *user_registry, Pattern *out_pattern) {
    if (!out_pattern) return false;
    memset(out_pattern, 0, sizeof(*out_pattern));
    bool truncated_token_seen = false;
    bool tone_checked = false;
    SampleRef tone_ref = {0};

    for (size_t i = 0; i < line_count; ++i) {
        char *line = lines[i];
        if (!line) continue;
        size_t len = strlen(line);
        size_t idx = 0;
        while (idx < len) {
            while (idx < len && isspace((unsigned char)line[idx])) idx++;
            if (idx >= len) break;
            size_t start = idx;
            while (idx < len && !isspace((unsigned char)line[idx])) idx++;
            size_t tok_len = idx - start;
            if (tok_len == 0) continue;
            size_t copy_len = tok_len;
            char token[TOKEN_BUFFER_LEN];
            if (copy_len >= sizeof(token)) {
                copy_len = sizeof(token) - 1;
                truncated_token_seen = true;
            }
            memcpy(token, line + start, copy_len);
            token[copy_len] = '\0';

            NoteStep note_step = {0};
            NoteParseResult note_result = parse_note_token(token, &note_step);
            if (note_result != NOTE_PARSE_NONE) {
                PatternStep step = {0};
                step.duration_beats = note_step.duration_beats;
                step.playback_rate = note_step.playback_rate;
                step.midi_note = note_step.midi_note;
                step.has_midi_note = note_step.has_midi_note;

                if (note_result == NOTE_PARSE_OK) {
                    if (!tone_checked) {
                        tone_ref = resolve_sample("tone", default_registry, user_registry);
                        tone_checked = true;
                        if (!tone_ref.valid) {
                            fprintf(stderr, "Warning: default 'tone' sample unavailable (notes become rests)\n");
                        }
                    }
                    if (tone_ref.valid) {
                        step.sample = tone_ref;
                        if (tone_ref.sound && tone_ref.sound->pitched_entry_count > 0 && note_step.has_midi_note) {
                            size_t variant_index = tone_ref.variant_index;
                            int base_midi = BASE_MIDI_TONE;
                            if (pick_pitched_variant(tone_ref.sound, note_step.midi_note, &variant_index, &base_midi)) {
                                step.sample.variant_index = variant_index;
                                step.playback_rate = pow(2.0, ((double)note_step.midi_note - (double)base_midi) / 12.0);
                            }
                        }
                    }
                }
                add_step(out_pattern, &step);
                continue;
            }

            SampleRef ref = resolve_sample(token, default_registry, user_registry);
            PatternStep step = {0};
            step.sample = ref;
            step.duration_beats = 1.0;
            step.playback_rate = 1.0;
            step.has_midi_note = false;
            step.midi_note = 0;
            add_step(out_pattern, &step);
        }
    }

    if (truncated_token_seen) {
        fprintf(stderr, "Warning: pattern tokens longer than %zu characters were truncated.\n", (size_t)(TOKEN_BUFFER_LEN - 1));
    }

    return out_pattern->step_count > 0;
}
