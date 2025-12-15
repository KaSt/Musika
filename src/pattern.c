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
    NOTE_PARSE_HIT,
} NoteParseResult;

typedef struct {
    char warned_names[8][32];
    size_t warned_count;
} ModifierWarningState;

typedef enum {
    SCALE_MODE_MAJOR,
    SCALE_MODE_MINOR,
} ScaleMode;

typedef struct {
    double duration_beats;
    double playback_rate;
    int midi_note;
    bool has_midi_note;
} NoteStep;

typedef struct {
    bool has_key;
    int key_semitone;
    bool has_scale;
    ScaleMode scale;
    bool degree_default_warned;
} MusicalContext;

static void add_step(Pattern *pattern, const PatternStep *step) {
    if (pattern->step_count >= sizeof(pattern->steps) / sizeof(pattern->steps[0])) return;
    pattern->steps[pattern->step_count] = *step;
    pattern->step_count += 1;
}

static const char *skip_spaces(const char *p) {
    while (p && *p && isspace((unsigned char)*p)) ++p;
    return p;
}

static bool equals_ci(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
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

static int parse_key_name(const char *text, bool *ok) {
    if (ok) *ok = false;
    if (!text || text[0] == '\0') return 0;
    int base = semitone_for_letter(text[0]);
    if (base < 0) return 0;
    int accidental = 0;
    char accidental_char = text[1];
    if (accidental_char == '#' || tolower((unsigned char)accidental_char) == 'b') {
        accidental = (accidental_char == '#') ? 1 : -1;
        if (text[2] != '\0') return 0;
    } else if (text[1] != '\0') {
        return 0;
    }
    if (ok) *ok = true;
    int semitone = base + accidental;
    if (semitone < 0) semitone += 12;
    if (semitone >= 12) semitone -= 12;
    return semitone;
}

static ScaleMode parse_scale_mode(const char *text, bool *ok) {
    if (ok) *ok = false;
    if (!text) return SCALE_MODE_MAJOR;
    if (equals_ci(text, "major") || equals_ci(text, "ionian")) {
        if (ok) *ok = true;
        return SCALE_MODE_MAJOR;
    }
    if (equals_ci(text, "minor") || equals_ci(text, "aeolian")) {
        if (ok) *ok = true;
        return SCALE_MODE_MINOR;
    }
    return SCALE_MODE_MAJOR;
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

static bool copy_quoted_string(const char **p, char *out, size_t out_len, bool *truncated) {
    if (!p || !*p || !out || out_len == 0) return false;
    const char *s = skip_spaces(*p);
    if (*s != '"') return false;
    s++;
    size_t idx = 0;
    while (*s && *s != '"') {
        if (idx + 1 < out_len) {
            out[idx++] = *s;
        } else if (truncated) {
            *truncated = true;
        }
        s++;
    }
    if (*s != '"') return false;
    out[idx] = '\0';
    *p = s + 1;
    return true;
}

static bool modifier_warned(const ModifierWarningState *state, const char *name) {
    if (!state || !name) return false;
    for (size_t i = 0; i < state->warned_count; ++i) {
        if (equals_ci(state->warned_names[i], name)) return true;
    }
    return false;
}

static void record_modifier_warning(ModifierWarningState *state, const char *name) {
    if (!state || !name || modifier_warned(state, name)) return;
    if (state->warned_count < (sizeof(state->warned_names) / sizeof(state->warned_names[0]))) {
        strncpy(state->warned_names[state->warned_count], name, sizeof(state->warned_names[0]) - 1);
        state->warned_names[state->warned_count][sizeof(state->warned_names[0]) - 1] = '\0';
        state->warned_count += 1;
    }
}

static NoteParseResult parse_note_token(const char *token, MusicalContext *context, NoteStep *out_step) {
    if (!token || !out_step) return NOTE_PARSE_NONE;

    char token_copy[TOKEN_BUFFER_LEN];
    strncpy(token_copy, token, sizeof(token_copy) - 1);
    token_copy[sizeof(token_copy) - 1] = '\0';

    char *duration_part = strchr(token_copy, '/');
    if (duration_part) {
        *duration_part = '\0';
        duration_part += 1;
    }

    if ((token_copy[0] == 'x' || token_copy[0] == 'X' || token_copy[0] == '1') && token_copy[1] == '\0') {
        out_step->duration_beats = parse_duration_beats(duration_part);
        out_step->playback_rate = 1.0;
        out_step->has_midi_note = false;
        out_step->midi_note = 0;
        return NOTE_PARSE_HIT;
    }

    if (token_copy[0] == '~' && token_copy[1] == '\0') {
        out_step->duration_beats = parse_duration_beats(duration_part);
        out_step->playback_rate = 1.0;
        out_step->has_midi_note = false;
        out_step->midi_note = 0;
        return NOTE_PARSE_REST;
    }

    int midi = -1;
    bool parsed = false;
    bool clamped = false;

    if ((token_copy[0] == 'd' || token_copy[0] == 'D') && isdigit((unsigned char)token_copy[1])) {
        size_t idx = 1;
        int degree = 0;
        while (isdigit((unsigned char)token_copy[idx])) {
            degree = (degree * 10) + (token_copy[idx] - '0');
            idx++;
        }
        int octave_delta = 0;
        while (token_copy[idx] == '^' || token_copy[idx] == '_') {
            octave_delta += (token_copy[idx] == '^') ? 1 : -1;
            idx++;
        }
        if (token_copy[idx] != '\0' || degree < 1 || degree > 7) {
            fprintf(stderr, "Warning: unknown note token '%s' (treated as rest)\n", token_copy);
        } else {
            int key_semitone = 0;
            ScaleMode scale_mode = SCALE_MODE_MAJOR;
            bool warn_default = true;
            if (context) {
                if (context->has_key) {
                    key_semitone = context->key_semitone;
                    warn_default = false;
                }
                if (context->has_scale) {
                    scale_mode = context->scale;
                    warn_default = warn_default && !context->has_key;
                } else {
                    warn_default = true;
                }
            }
            if (context && warn_default && !context->degree_default_warned) {
                fprintf(stderr, "Warning: degree used without .key/.scale; defaulting to C major\n");
                context->degree_default_warned = true;
            }

            static const int major_offsets[7] = {0, 2, 4, 5, 7, 9, 11};
            static const int minor_offsets[7] = {0, 2, 3, 5, 7, 8, 10};
            int offset = (scale_mode == SCALE_MODE_MINOR) ? minor_offsets[degree - 1] : major_offsets[degree - 1];
            int base_midi = (5 * 12) + key_semitone; // middle octave (C4 = 60)
            midi = base_midi + offset + (octave_delta * 12);
            if (midi < 0) midi = 0;
            if (midi > 127) midi = 127;
            parsed = true;
        }
    } else if (token_copy[0] == 'k' || token_copy[0] == 'K') {
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

static void append_note_step(Pattern *pattern,
                             const NoteStep *note_step,
                             NoteParseResult result,
                             const SampleRef *sample,
                             bool *missing_sample_warned) {
    if (!pattern || !note_step) return;
    PatternStep step = {0};
    step.duration_beats = note_step->duration_beats;
    step.playback_rate = note_step->playback_rate;
    step.midi_note = note_step->midi_note;
    step.has_midi_note = note_step->has_midi_note;

    if (result == NOTE_PARSE_OK || result == NOTE_PARSE_HIT) {
        if (sample && sample->valid) {
            step.sample = *sample;
            if (sample->sound && sample->sound->pitched_entry_count > 0 && note_step->has_midi_note) {
                size_t variant_index = sample->variant_index;
                int base_midi = BASE_MIDI_TONE;
                if (pick_pitched_variant(sample->sound, note_step->midi_note, &variant_index, &base_midi)) {
                    step.sample.variant_index = variant_index;
                    step.playback_rate = pow(2.0, ((double)note_step->midi_note - (double)base_midi) / 12.0);
                }
            } else if (sample->sound && sample->sound->pitched_entry_count == 0) {
                if (sample->sound->name && strcmp(sample->sound->name, "tone") == 0) {
                    step.playback_rate = note_step->playback_rate;
                } else {
                    step.playback_rate = 1.0;
                }
            }
        } else if (missing_sample_warned && (!*missing_sample_warned)) {
            fprintf(stderr, "Warning: note specified without a valid @sample binding (treated as rest)\n");
            *missing_sample_warned = true;
        }
    }

    add_step(pattern, &step);
}

static void apply_pitch_shift_to_step(PatternStep *step, int semitone_shift, bool *pitch_clamp_warned) {
    if (!step || semitone_shift == 0 || !step->has_midi_note) return;
    if (!step->sample.valid || !step->sample.sound) return;
    if (step->sample.sound->pitched_entry_count == 0 &&
        !(step->sample.sound->name && strcmp(step->sample.sound->name, "tone") == 0)) {
        return;
    }

    int midi = step->midi_note + semitone_shift;
    if (midi < 0) {
        midi = 0;
        if (pitch_clamp_warned && !*pitch_clamp_warned) {
            fprintf(stderr, "Warning: transposed pitch clamped to 0 (valid MIDI range 0-127)\n");
            *pitch_clamp_warned = true;
        }
    } else if (midi > 127) {
        midi = 127;
        if (pitch_clamp_warned && !*pitch_clamp_warned) {
            fprintf(stderr, "Warning: transposed pitch clamped to 127 (valid MIDI range 0-127)\n");
            *pitch_clamp_warned = true;
        }
    }

    step->midi_note = midi;

    if (step->sample.sound->pitched_entry_count > 0) {
        size_t variant_index = step->sample.variant_index;
        int base_midi = BASE_MIDI_TONE;
        if (pick_pitched_variant(step->sample.sound, midi, &variant_index, &base_midi)) {
            step->sample.variant_index = variant_index;
            step->playback_rate = pow(2.0, ((double)midi - (double)base_midi) / 12.0);
        } else {
            step->playback_rate = pow(2.0, ((double)midi - (double)BASE_MIDI_TONE) / 12.0);
        }
    } else {
        step->playback_rate = pow(2.0, ((double)midi - (double)BASE_MIDI_TONE) / 12.0);
    }
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

static SampleRef resolve_sample(const char *token,
                               const SampleRegistry *default_registry,
                               const SampleRegistry *user_registry,
                               const char *bank_name) {
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

    bool bank_matches_user = false;
    bool bank_matches_default = false;
    if (bank_name) {
        if (user_registry && user_registry->name && equals_ci(bank_name, user_registry->name)) {
            bank_matches_user = true;
        }
        if (default_registry && default_registry->name && equals_ci(bank_name, default_registry->name)) {
            bank_matches_default = true;
        }
    }

    if ((!bank_name || bank_matches_user) && user_registry) {
        sound = sample_registry_find_sound(user_registry, token_copy);
        registry = user_registry;
    }
    if (!sound && (!bank_name || bank_matches_default || !bank_matches_user) && default_registry) {
        sound = sample_registry_find_sound(default_registry, token_copy);
        registry = default_registry;
    }
    if (!sound && bank_name && !bank_matches_user && !bank_matches_default) {
        fprintf(stderr, "Warning: unknown soundbank '%s' (falling back to default registry)\n", bank_name);
        if (user_registry) {
            sound = sample_registry_find_sound(user_registry, token_copy);
            registry = user_registry;
        }
        if (!sound && default_registry) {
            sound = sample_registry_find_sound(default_registry, token_copy);
            registry = default_registry;
        }
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

static bool token_has_duration(const char *token) {
    return token && strchr(token, '/') != NULL;
}

static void emit_note_token(const char *token,
                            const SampleRef *sample,
                            MusicalContext *context,
                            Pattern *pattern,
                            bool *truncated_token_seen,
                            bool *missing_sample_warned) {
    if (!token || !pattern) return;
    char buffer[TOKEN_BUFFER_LEN];
    size_t len = strlen(token);
    if (len >= sizeof(buffer)) {
        len = sizeof(buffer) - 1;
        if (truncated_token_seen) *truncated_token_seen = true;
    }
    memcpy(buffer, token, len);
    buffer[len] = '\0';

    NoteStep step = {0};
    NoteParseResult result = parse_note_token(buffer, context, &step);
    if (result != NOTE_PARSE_NONE) {
        append_note_step(pattern, &step, result, sample, missing_sample_warned);
    }
}

static void parse_note_sequence(const char *text,
                                const SampleRef *sample,
                                MusicalContext *context,
                                Pattern *pattern,
                                bool *truncated_token_seen,
                                bool *missing_sample_warned) {
    if (!text || !pattern) return;
    const char *p = text;
    while (p && *p) {
        p = skip_spaces(p);
        if (!*p) break;

        if (*p == '<') {
            p++;
            char group_tokens[32][TOKEN_BUFFER_LEN];
            size_t group_count = 0;
            while (*p && *p != '>') {
                p = skip_spaces(p);
                if (*p == '>' || !*p) break;
                const char *start = p;
                while (*p && !isspace((unsigned char)*p) && *p != '>') ++p;
                size_t len = (size_t)(p - start);
                if (len > 0 && group_count < sizeof(group_tokens) / sizeof(group_tokens[0])) {
                    size_t copy_len = len;
                    if (copy_len >= TOKEN_BUFFER_LEN) {
                        copy_len = TOKEN_BUFFER_LEN - 1;
                        if (truncated_token_seen) *truncated_token_seen = true;
                    }
                    memcpy(group_tokens[group_count], start, copy_len);
                    group_tokens[group_count][copy_len] = '\0';
                    group_count++;
                } else if (len >= TOKEN_BUFFER_LEN && truncated_token_seen) {
                    *truncated_token_seen = true;
                }
            }
            if (*p == '>') {
                p++;
            }

            char group_duration[32] = {0};
            const char *after_group = p;
            if (*after_group == '/') {
                after_group++;
                const char *start = after_group;
                while (*after_group && !isspace((unsigned char)*after_group)) ++after_group;
                size_t len = (size_t)(after_group - start);
                if (len > 0) {
                    size_t copy_len = len < sizeof(group_duration) - 1 ? len : sizeof(group_duration) - 1;
                    memcpy(group_duration, start, copy_len);
                    group_duration[copy_len] = '\0';
                    if (len >= sizeof(group_duration) && truncated_token_seen) *truncated_token_seen = true;
                }
                p = after_group;
            }

            for (size_t i = 0; i < group_count; ++i) {
                const char *source = group_tokens[i];
                char combined[TOKEN_BUFFER_LEN];
                if (group_duration[0] != '\0' && !token_has_duration(source)) {
                    if (snprintf(combined, sizeof(combined), "%s/%s", source, group_duration) >= (int)sizeof(combined)) {
                        if (truncated_token_seen) *truncated_token_seen = true;
                        combined[sizeof(combined) - 1] = '\0';
                    }
                    emit_note_token(combined, sample, context, pattern, truncated_token_seen, missing_sample_warned);
                } else {
                    emit_note_token(source, sample, context, pattern, truncated_token_seen, missing_sample_warned);
                }
            }
            continue;
        }

        const char *start = p;
        while (*p && !isspace((unsigned char)*p)) ++p;
        size_t len = (size_t)(p - start);
        if (len > 0) {
            char token[TOKEN_BUFFER_LEN];
            size_t copy_len = len < sizeof(token) - 1 ? len : sizeof(token) - 1;
            if (len >= sizeof(token) && truncated_token_seen) *truncated_token_seen = true;
            memcpy(token, start, copy_len);
            token[copy_len] = '\0';
            emit_note_token(token, sample, context, pattern, truncated_token_seen, missing_sample_warned);
        }
    }
}

static void parse_modifier_chain(const char *text,
                                 const SampleRef *sample,
                                 Pattern *pattern,
                                 bool *truncated_token_seen,
                                 bool *missing_sample_warned,
                                 ModifierWarningState *modifier_warnings,
                                 MusicalContext *musical_context,
                                 bool *pitch_clamp_warned) {
    size_t start_index = pattern ? pattern->step_count : 0;
    int semitone_shift = 0;
    const char *p = text;
    while (p && *p) {
        p = skip_spaces(p);
        if (*p != '.') break;
        p++;
        const char *name_start = p;
        while (*p && (isalnum((unsigned char)*p) || *p == '_')) ++p;
        size_t name_len = (size_t)(p - name_start);
        if (name_len == 0) {
            fprintf(stderr, "Warning: expected modifier name after '.'\n");
            break;
        }
        char name[32];
        size_t copy_len = name_len < sizeof(name) - 1 ? name_len : sizeof(name) - 1;
        if (name_len >= sizeof(name) && truncated_token_seen) *truncated_token_seen = true;
        memcpy(name, name_start, copy_len);
        name[copy_len] = '\0';

        p = skip_spaces(p);
        if (*p != '(') {
            fprintf(stderr, "Warning: expected '(' after modifier '%s'\n", name);
            break;
        }
        p++;
        const char *arg_start = p;
        int depth = 1;
        while (*p && depth > 0) {
            if (*p == '(') depth++;
            else if (*p == ')') depth--;
            if (depth > 0) ++p;
        }
        if (depth != 0) {
            fprintf(stderr, "Warning: unterminated modifier '%s'\n", name);
            break;
        }
        size_t arg_len = (size_t)(p - arg_start);
        char arg_buf[512];
        size_t arg_copy_len = arg_len < sizeof(arg_buf) - 1 ? arg_len : sizeof(arg_buf) - 1;
        if (arg_len >= sizeof(arg_buf) && truncated_token_seen) *truncated_token_seen = true;
        memcpy(arg_buf, arg_start, arg_copy_len);
        arg_buf[arg_copy_len] = '\0';

        const char *after_paren = p + 1;
        if (equals_ci(name, "note")) {
            const char *arg_ptr = arg_buf;
            char note_text[512];
            if (!copy_quoted_string(&arg_ptr, note_text, sizeof(note_text), truncated_token_seen)) {
                fprintf(stderr, "Warning: .note() expects a quoted string\n");
            } else {
                parse_note_sequence(note_text, sample, musical_context, pattern, truncated_token_seen, missing_sample_warned);
            }
        } else if (equals_ci(name, "octave")) {
            const char *arg_ptr = skip_spaces(arg_buf);
            char *end = NULL;
            long delta = strtol(arg_ptr, &end, 10);
            const char *rest = skip_spaces(end);
            if (end == arg_ptr || *rest != '\0') {
                fprintf(stderr, "Warning: .octave() expects a numeric argument\n");
            } else {
                semitone_shift += (int)(delta * 12);
            }
        } else if (equals_ci(name, "transpose")) {
            const char *arg_ptr = skip_spaces(arg_buf);
            char *end = NULL;
            long delta = strtol(arg_ptr, &end, 10);
            const char *rest = skip_spaces(end);
            if (end == arg_ptr || *rest != '\0') {
                fprintf(stderr, "Warning: .transpose() expects a numeric argument\n");
            } else {
                semitone_shift += (int)delta;
            }
        } else if (equals_ci(name, "key")) {
            const char *arg_ptr = arg_buf;
            char key_text[32];
            if (!copy_quoted_string(&arg_ptr, key_text, sizeof(key_text), truncated_token_seen)) {
                fprintf(stderr, "Warning: .key() expects a quoted key name like \"C#\"\n");
            } else {
                bool ok = false;
                int semitone = parse_key_name(key_text, &ok);
                if (!ok) {
                    fprintf(stderr, "Warning: unknown key '%s' (ignored)\n", key_text);
                } else if (musical_context) {
                    musical_context->key_semitone = semitone;
                    musical_context->has_key = true;
                }
            }
        } else if (equals_ci(name, "scale")) {
            const char *arg_ptr = arg_buf;
            char scale_text[32];
            if (!copy_quoted_string(&arg_ptr, scale_text, sizeof(scale_text), truncated_token_seen)) {
                fprintf(stderr, "Warning: .scale() expects a quoted scale name like \"major\"\n");
            } else {
                bool ok = false;
                ScaleMode mode = parse_scale_mode(scale_text, &ok);
                if (!ok) {
                    fprintf(stderr, "Warning: unknown scale '%s' (ignored)\n", scale_text);
                } else if (musical_context) {
                    musical_context->scale = mode;
                    musical_context->has_scale = true;
                }
            }
        } else {
            if (!modifier_warned(modifier_warnings, name)) {
                fprintf(stderr, "Warning: modifier '%s' is not implemented yet (ignored)\n", name);
                record_modifier_warning(modifier_warnings, name);
            }
        }

        p = after_paren;
    }

    if (semitone_shift != 0 && pattern) {
        size_t end_index = pattern->step_count;
        for (size_t i = start_index; i < end_index; ++i) {
            apply_pitch_shift_to_step(&pattern->steps[i], semitone_shift, pitch_clamp_warned);
        }
    }
}

static bool parse_sample_invocation(const char *line,
                                    const SampleRegistry *default_registry,
                                    const SampleRegistry *user_registry,
                                    SampleRef *out_sample,
                                    const char **out_rest,
                                    bool *truncated_token_seen) {
    if (!line) return false;
    const char *p = skip_spaces(line);
    const char *keyword = "@sample";
    size_t keyword_len = strlen(keyword);
    if (strncmp(p, keyword, keyword_len) != 0) return false;
    p += keyword_len;
    p = skip_spaces(p);
    if (*p != '(') {
        fprintf(stderr, "Warning: @sample must be followed by '('\n");
        return false;
    }
    p++;

    char sample_token[TOKEN_BUFFER_LEN];
    if (!copy_quoted_string(&p, sample_token, sizeof(sample_token), truncated_token_seen)) {
        fprintf(stderr, "Warning: @sample(...) requires a quoted sound name\n");
        return false;
    }

    char bank_arg[TOKEN_BUFFER_LEN] = {0};
    p = skip_spaces(p);
    if (*p == ',') {
        p++;
        p = skip_spaces(p);
        const char *ident_start = p;
        while (*p && isalpha((unsigned char)*p)) ++p;
        size_t ident_len = (size_t)(p - ident_start);
        char ident[32];
        size_t ident_copy = ident_len < sizeof(ident) - 1 ? ident_len : sizeof(ident) - 1;
        memcpy(ident, ident_start, ident_copy);
        ident[ident_copy] = '\0';
        p = skip_spaces(p);
        if (*p == '=') {
            p++;
        }
        if (equals_ci(ident, "bank")) {
            if (!copy_quoted_string(&p, bank_arg, sizeof(bank_arg), truncated_token_seen)) {
                fprintf(stderr, "Warning: @sample bank parameter must be quoted\n");
            }
        }
        p = skip_spaces(p);
    }

    if (*p != ')') {
        fprintf(stderr, "Warning: @sample missing closing ')'\n");
        return false;
    }
    if (out_rest) *out_rest = p + 1;

    char token_copy[TOKEN_BUFFER_LEN];
    strncpy(token_copy, sample_token, sizeof(token_copy) - 1);
    token_copy[sizeof(token_copy) - 1] = '\0';
    if (strlen(sample_token) >= sizeof(token_copy) && truncated_token_seen) *truncated_token_seen = true;

    char inline_bank[TOKEN_BUFFER_LEN] = {0};
    char sound_token[TOKEN_BUFFER_LEN] = {0};
    size_t variant_index = 0;
    bool has_variant = false;

    int colon_count = 0;
    for (const char *c = token_copy; *c; ++c) {
        if (*c == ':') colon_count++;
    }

    if (colon_count == 0) {
        strncpy(sound_token, token_copy, sizeof(sound_token) - 1);
        sound_token[sizeof(sound_token) - 1] = '\0';
    } else if (colon_count == 1) {
        char *first = strchr(token_copy, ':');
        *first = '\0';
        char *second = first + 1;
        bool treat_as_bank = false;
        if (!bank_arg[0]) {
            if ((user_registry && user_registry->name && equals_ci(user_registry->name, token_copy)) ||
                (default_registry && default_registry->name && equals_ci(default_registry->name, token_copy))) {
                treat_as_bank = true;
            }
        }
        if (treat_as_bank) {
            strncpy(inline_bank, token_copy, sizeof(inline_bank) - 1);
            inline_bank[sizeof(inline_bank) - 1] = '\0';
            strncpy(sound_token, second, sizeof(sound_token) - 1);
            sound_token[sizeof(sound_token) - 1] = '\0';
        } else {
            strncpy(sound_token, token_copy, sizeof(sound_token) - 1);
            sound_token[sizeof(sound_token) - 1] = '\0';
            has_variant = parse_variant_index(second, &variant_index);
            if (!has_variant) {
                strncpy(sound_token, sample_token, sizeof(sound_token) - 1);
                sound_token[sizeof(sound_token) - 1] = '\0';
            }
        }
    } else {
        char *first = strchr(token_copy, ':');
        char *second = first ? strchr(first + 1, ':') : NULL;
        if (first && second) {
            *first = '\0';
            *second = '\0';
            strncpy(inline_bank, token_copy, sizeof(inline_bank) - 1);
            inline_bank[sizeof(inline_bank) - 1] = '\0';
            strncpy(sound_token, first + 1, sizeof(sound_token) - 1);
            sound_token[sizeof(sound_token) - 1] = '\0';
            has_variant = parse_variant_index(second + 1, &variant_index);
        }
    }

    const char *bank_to_use = bank_arg[0] ? bank_arg : (inline_bank[0] ? inline_bank : NULL);
    if (!sound_token[0]) {
        fprintf(stderr, "Warning: unable to parse sound name in @sample()\n");
        return true;
    }

    SampleRef ref = resolve_sample(sound_token, default_registry, user_registry, bank_to_use);
    if (ref.valid && has_variant && ref.sound && ref.sound->variant_count > 0) {
        if (variant_index >= ref.sound->variant_count) {
            variant_index = variant_index % ref.sound->variant_count;
        }
        ref.variant_index = variant_index;
    }

    if (out_sample) *out_sample = ref;
    return true;
}

bool pattern_from_lines(char **lines, size_t line_count, const SampleRegistry *default_registry, const SampleRegistry *user_registry, Pattern *out_pattern) {
    if (!out_pattern) return false;
    memset(out_pattern, 0, sizeof(*out_pattern));
    bool truncated_token_seen = false;
    bool tone_checked = false;
    SampleRef tone_ref = {0};
    bool missing_sample_warned = false;
    bool deprecated_notes = false;
    SampleRef current_sample = {0};
    bool have_current_sample = false;
    ModifierWarningState modifier_warnings = {0};
    bool pitch_clamp_warned = false;
    MusicalContext musical_context = {0};
    musical_context.scale = SCALE_MODE_MAJOR;

    for (size_t i = 0; i < line_count; ++i) {
        char *line = lines[i];
        if (!line) continue;
        const char *trimmed = skip_spaces(line);
        if (!trimmed || trimmed[0] == '\0') continue;

        SampleRef parsed_sample = {0};
        const char *rest = NULL;
        if (parse_sample_invocation(trimmed, default_registry, user_registry, &parsed_sample, &rest, &truncated_token_seen)) {
            current_sample = parsed_sample;
            have_current_sample = true;
            musical_context = (MusicalContext){0};
            musical_context.scale = SCALE_MODE_MAJOR;
            parse_modifier_chain(rest, &current_sample, out_pattern, &truncated_token_seen, &missing_sample_warned, &modifier_warnings, &musical_context, &pitch_clamp_warned);
            continue;
        }

        if (have_current_sample && trimmed[0] == '.') {
            parse_modifier_chain(trimmed, &current_sample, out_pattern, &truncated_token_seen, &missing_sample_warned, &modifier_warnings, &musical_context, &pitch_clamp_warned);
            continue;
        }

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
            NoteParseResult note_result = parse_note_token(token, NULL, &note_step);
            if (note_result != NOTE_PARSE_NONE) {
                if (note_result == NOTE_PARSE_OK) {
                    deprecated_notes = true;
                    if (!tone_checked) {
                        tone_ref = resolve_sample("tone", default_registry, user_registry, NULL);
                        tone_checked = true;
                        if (!tone_ref.valid) {
                            fprintf(stderr, "Warning: default 'tone' sample unavailable (notes become rests)\n");
                        }
                    }
                    append_note_step(out_pattern, &note_step, note_result, tone_ref.valid ? &tone_ref : NULL, &missing_sample_warned);
                } else {
                    append_note_step(out_pattern, &note_step, note_result, NULL, &missing_sample_warned);
                }
                continue;
            }

            SampleRef ref = resolve_sample(token, default_registry, user_registry, NULL);
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

    if (deprecated_notes) {
        fprintf(stderr, "Warning: implicit note syntax is deprecated; please use @sample(...).note(...) instead.\n");
    }

    return out_pattern->step_count > 0;
}
