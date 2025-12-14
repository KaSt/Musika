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

Audible beep test:
```bash
./musika --beep
```

Live coding loop:
```bash
./musika
```

Commands inside the REPL:
- `:edit` – open the inline buffer (finish with a `.` line).
- `:eval` – parse the buffer and arm it as the active pattern.
- `:play` / `:stop` – start or pause transport without tearing down the audio device.
- `:panic` – silence queued audio immediately.
- `:help` – show the full list.

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
- Sharps (`#`), flats (`b`), and octave numbers belong to the note syntax itself. Key/scale helpers such as
  `major`/`minor` will surface later as separate `.key()`/`.scale()` modifiers rather than inline note tokens.

Durations use `/len` relative to a quarter note; missing `/len` defaults to `/4`. Notes always belong to the currently bound
instrument. The `tone` sample stays available for melodic lines; if a sound exposes a pitched map, Musika snaps to the nearest
base entry before adjusting playback rate. Instruments without pitched maps ignore MIDI-derived playback-rate changes so
percussive sounds stay at their recorded pitch.

Only `.note(...)` produces audible events today; other chained modifiers such as `.postgain(...)`, `.attack(...)`, or
`.release(...)` are parsed and ignored with a warning so the syntax stays forward-compatible. Legacy patterns that omit
`@sample(...)` still parse but print a deprecation warning—bind notes to a sample explicitly whenever possible.

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
