# Musika

Music made code, or code made music.

Musika is a tiny terminal-first live coding playground inspired by TidalCycles and Strudel. It ships as a single C program that
opens a real audio device (via an embedded miniaudio-compatible backend), plays a generated kick sample, and hot-reloads simple
patterns while audio keeps running.

## Features

- Inline text editor inside the terminal to sketch multi-line patterns.
- Minimal pattern language using space-separated tokens (`kick`, `bd`) that map to the generated kick sample.
- Melody-friendly note tokens (`c4`, `d#5/8`) that pitch-shift a built-in tone sample without changing syntax elsewhere
  (simple nearest-neighbor resampling; no interpolation yet).
- Configurable tempo, audio backend name, and remote sample packs through `config.json` (audio runs locally, no downloads).
- Continuous transport thread that schedules events in deterministic time slices for steady playback.

## Building

```bash
make
```

This produces the `musika` binary. On Linux, the embedded backend dynamically loads ALSA (`libasound.so.2`) at runtime; on
macOS it uses AudioQueue. No additional build-time dependencies are required.

## Running

Generate the kick sample (only needed once per checkout or after cleaning `assets/`):
```bash
./scripts/fetch_kick.sh
```

Startup modes and precedence (conflicts print an error):

- **Play a file immediately:** `./musika song.musika` or `./musika --play song.musika` loads, compiles, and starts playback. `Ctrl+C` stops.
- **Open the fullscreen editor:** `./musika` with no args opens an empty buffer. `./musika --edit [file]` opens the editor with an optional file. `--edit` cannot be mixed with `--play`/`--check`/`--midi-dump`.
- **Compile check only:** `./musika --check file.musika` parses the file and exits 0/1. Additional positional files are rejected.
- **MIDI event dump (test mode):** `./musika --midi-dump out.json file.musika` compiles the pattern and writes deterministic note_on/note_off events to JSON (beats + seconds). No audio device is opened.
- **Version:** `./musika --version`

### Editor basics

- Modes: `ESC` for **NORMAL**, `i` for **INSERT**. Normal mode uses `h/j/k/l` to move.
- Commands: `:` enters the command prompt. Supported commands:
  - `:play` / `:stop` – compile + start playback or pause.
  - `:w` – save to the current filename; `:w <path>` saves as.
  - `:q` – quit (blocked if modified), `:q!` – force quit, `:wq` – save and quit.
- Editing: typed characters insert in INSERT mode; `Backspace` deletes; `Enter` inserts a newline; `dd` deletes a line.

Write patterns by first binding an instrument, then chaining notes and modifiers:

```
@sample("bd").note("x x x x")
@sample("tone").note("<c4 e4 g4>/8")
@sample("piano", bank="default").note("k49 k52 k56/2").postgain(0.7)
```

`@sample("name[:variant]")` declares the sound to play and can optionally target a soundbank (use `bank="user"` or an inline
form such as `@sample("mybank:piano:1")`). If no bank is provided, Musika tries the user registry first and then falls back
to the default registry. Unknown banks fall back to the default registry with a warning.

The transport still schedules ~200ms ahead of the audio callback so tempo-stable playback continues while you edit.

`.note("...")` accepts several note input styles within the quoted string:

- Note names: `c4`, `d#4`, `eb3` (octaves 0–8).
- MIDI note numbers: `60`, `62/8`, `69/2`.
- Piano key numbers: `k1`–`k88` (`k49` = A4/440 Hz, midi = key + 20). Out-of-range keys clamp with a warning.
- Grouped patterns such as `<c4 e4 g4>/8`, where the trailing duration applies to each grouped note.
- Percussive hits: `x` (or `1`) for an unpitched trigger on the bound sample.
- Rests: `~` yields silence for the given duration (defaults to `/4`).
- Sharps (`#`), flats (`b`), and octave numbers belong to the note syntax itself.

Chord reminder: wrap space-separated notes in `< >`. A duration after the closing `>` applies to each note in the group, and `~` inside a chord is simply ignored (no voice, no timing shift).

Examples of pitch helpers on a melodic chain:

```
@sample("tone").note("c4 e4 g4").octave(1)
@sample("tone").note("c4 e4 g4").transpose(-3)
```

### Time transforms

Accelerate or slow down a chain without touching the note text:

```
@sample("bd").note("x x x x").fast(2)   # double-speed
@sample("bd").note("x x x x").slow(2)   # half-speed
```

Use `.every(n, "fast k")` or `.every(n, "slow k")` to apply a temporary tempo change on specific cycles:

```
@sample("hh").note("x x x x").every(4, "fast 2")
```

Transforms stay scoped to the chain they are called on; other chains continue at their own timing.

#### Cycle semantics

- A cycle is one full wrap through the compiled pattern step list (when scheduling wraps from the last step back to step 0).
- `.every(n, ...)` uses this global pattern-cycle counter; it does not reset per chain and it is not aligned to musical bars.
- Example: `.every(4, "fast 2")` activates on cycles 4, 8, 12, etc., across the entire pattern.

### Key/Scale + Degrees

Bind a key and scale to the current `@sample` chain, then write degrees Strudel-style:

```
@sample("tone").key("Eb").scale("minor").note("d1 d4 d5^ d1_")
```

- `.key("C")`, `.key("Eb")`, `.key("F#")` set the tonic (letter + optional sharp/flat, no octave needed).
- `.scale("major")` / `.scale("minor")` select the interval map (aliases: `ionian`, `aeolian`).
- Degree tokens inside `.note()` use `d1`–`d7`, optional octave nudges (`d5^`, `d2_`), and durations (`d3/8`).
- Degrees resolve relative to the active key/scale, then `.octave()` / `.transpose()` apply afterward.
- Missing key/scale defaults to C major with a one-time warning.

Durations use `/len` relative to a quarter note; missing `/len` defaults to `/4`. Notes always belong to the currently bound
instrument. The `tone` sample stays available for melodic lines; if a sound exposes a pitched map, Musika snaps to the nearest
base entry before adjusting playback rate. Instruments without pitched maps ignore MIDI-derived playback-rate changes so
percussive sounds stay at their recorded pitch.

Only `.note(...)` produces audible events today; other chained modifiers such as `.postgain(...)`, `.attack(...)`, or
`.release(...)` are parsed and ignored with a warning so the syntax stays forward-compatible. Legacy patterns that omit
`@sample(...)` still parse but print a deprecation warning—bind notes to a sample explicitly whenever possible.

Playback articulation: pitched notes keep a short release tail (legato-lite) so repeated notes or overlapping chords can decay naturally instead of cutting off the moment a new note starts. There is no sustain pedal or extra API—this is a built-in smoothing pass for back-to-back melodic events.

### Manual regression checklist

- `dd` deletes only the current line in NORMAL mode (cursor stays on a valid line/col).
- `./musika <file>` starts playback immediately; `Ctrl+C` stops.
- `./musika --edit <file>` opens the fullscreen editor preloaded with that file.
- `:w` writes to the current file, `:q` blocks if the buffer is modified, and `:q!` exits without saving.
- `./musika --midi-dump out.json <file>` writes deterministic note_on/note_off events without mutating the buffer.

Manual verification (quick sanity checks):

- Load a user pack and resolve variants: `:samples github:dxinteractive/strudel-samples@main` then evaluate `bd bd:3`.
- Default variants from the built-in map: evaluate `bd bd:3 bd:7`.
- Percussion hits and rests: evaluate `@sample("bd").note("x ~ x ~")` and confirm `~` stays silent while hits trigger.

## Configuring samples

The default `config.json` lists a few public repositories that host drum and synth samples from the Strudel/TidalCycles community.
The built-in kick is generated locally via `./scripts/fetch_kick.sh` to avoid storing binaries in the repository. Remote packs are
optional; the core experience runs entirely offline once the kick file exists. Musika currently requires `libcurl` for loading
remote sample maps. The melodic `tone` sample is generated locally (referenced as `builtin:tone` in the default map) when first used
so simple melodies work without downloads.

## Keeping the repository binary-free

Generated audio assets live under `assets/` (ignored by Git). To verify the tree stays free of committed binaries, run:

```
./scripts/check_binary_files.sh            # working tree only
./scripts/check_binary_files.sh --history  # working tree + all historical blobs
```

The helper scans tracked files (and optionally history) and exits non-zero if it detects likely binary content so you can clean up before pushing updates. Use the history mode when repairing old commits before publishing the branch.
