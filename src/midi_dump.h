#ifndef MUSIKA_MIDI_DUMP_H
#define MUSIKA_MIDI_DUMP_H

#include <stdbool.h>

#include "pattern.h"

typedef struct {
    double time_beats;
    double time_seconds;
    int midi_note;
    int velocity;
    int channel;
    bool note_on;
} MidiLikeEvent;

// Emit a JSON array of note_on/note_off events for a compiled pattern. The event
// times are derived from the pattern timing model (including per-chain every/fast/slow)
// at the provided tempo (BPM).
bool midi_dump_pattern(const Pattern *pattern, double bpm, const char *path);

#endif // MUSIKA_MIDI_DUMP_H

