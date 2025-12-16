#include "midi_dump.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static double chain_time_scale(const Pattern *pattern, const PatternStep *step, uint64_t cycle_number) {
    double scale = (step && step->time_scale > 0.0) ? step->time_scale : 1.0;
    if (!pattern) return scale;
    const PatternChain *chain = NULL;
    if (step && step->chain_id >= 0) {
        for (size_t i = 0; i < pattern->chain_count; ++i) {
            if (pattern->chains[i].id == step->chain_id) {
                chain = &pattern->chains[i];
                break;
            }
        }
    }

    if (!chain) return scale;

    if (chain->base_time_scale > 0.0) {
        scale *= chain->base_time_scale;
    }

    if (chain->has_every && chain->every_interval > 0 && chain->every_factor > 0) {
        if (cycle_number > 0 && (cycle_number % (uint64_t)chain->every_interval) == 0) {
            if (chain->every_type == TIME_TRANSFORM_FAST) {
                scale /= (double)chain->every_factor;
            } else if (chain->every_type == TIME_TRANSFORM_SLOW) {
                scale *= (double)chain->every_factor;
            }
        }
    }

    return scale;
}

static bool write_event(FILE *f, const MidiLikeEvent *event, bool first) {
    if (!f || !event) return false;
    if (!first) {
        if (fputs(",\n", f) < 0) return false;
    }
    return fprintf(f,
                   "  {\"time_beats\":%.6f,\"time_seconds\":%.6f,\"type\":\"%s\",\"note\":%d,\"velocity\":%d,\"channel\":%d}",
                   event->time_beats,
                   event->time_seconds,
                   event->note_on ? "note_on" : "note_off",
                   event->midi_note,
                   event->velocity,
                   event->channel) > 0;
}

bool midi_dump_pattern(const Pattern *pattern, double bpm, const char *path) {
    if (!pattern || !path || bpm <= 0.0) return false;
    FILE *f = fopen(path, "w");
    if (!f) return false;

    double seconds_per_beat = 60.0 / bpm;
    double next_event_time = 0.0;
    uint64_t cycle_count = 0;
    bool first = true;

    fputs("[\n", f);

    for (size_t i = 0; i < pattern->step_count; ++i) {
        const PatternStep *step = &pattern->steps[i];
        uint64_t cycle_number = cycle_count + 1;
        double scaled_duration_beats = step->duration_beats * chain_time_scale(pattern, step, cycle_number);
        double scaled_duration_seconds = scaled_duration_beats * seconds_per_beat;

        if (step->has_midi_note) {
            MidiLikeEvent on = {
                .time_beats = next_event_time,
                .time_seconds = next_event_time * seconds_per_beat,
                .midi_note = step->midi_note,
                .velocity = 100,
                .channel = 0,
                .note_on = true,
            };
            MidiLikeEvent off = on;
            off.time_beats = next_event_time + scaled_duration_beats;
            off.time_seconds = on.time_seconds + scaled_duration_seconds;
            off.note_on = false;

            write_event(f, &on, first);
            first = false;
            write_event(f, &off, first);
            first = false;
        }

        if (step->advance_time) {
            next_event_time += scaled_duration_beats;
        }

        if (i + 1 == pattern->step_count) {
            cycle_count += 1;
        }
    }

    fputs("\n]\n", f);
    fclose(f);
    return true;
}

