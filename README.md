# Musika

Music made code, or code made music.

Musika is a tiny terminal-first live coding playground inspired by TidalCycles and Strudel. It ships as a single C program that
opens a real audio device (via an embedded miniaudio-compatible backend), plays a generated kick sample, and hot-reloads simple
patterns while audio keeps running.

## Features

- Inline text editor inside the terminal to sketch multi-line patterns.
- Minimal pattern language using space-separated tokens (`kick`, `bd`) that map to the generated kick sample.
- Melody-friendly note tokens (`c4`, `d#5/8`) that pitch-shift a built-in tone sample without changing syntax elsewhere.
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

Write patterns such as `kick kick kick kick` to place quarter notes at the configured tempo. The transport schedules 200ms
windows ahead of the audio callback so tempo-stable playback continues while you edit.

Note tokens:

- `c4`, `d#4`, `eb3` name pitches (case-insensitive) with octaves 0–8.
- Append `/len` for durations in beats relative to a quarter note: `c4/8` (eighth), `c4/2` (half). Missing `/len` defaults to `/4`.
- Notes use the built-in `tone` sample (base A4/440 Hz) and set playback rate automatically. Unknown or out-of-range notes
  print a warning and become rests.

Pattern resolution:

- Musika resolves sounds from the user registry first, then the default registry. Unknown sounds print a warning
  ("Unknown sound '<name>' (treated as rest)") and are treated as rests.
- Use `sound:variant` to pick a specific variant; non-numeric indexes are rejected with a warning. Variant indexes wrap
  modulo the available variants for a sound.

Manual verification (quick sanity checks):

- Load a user pack and resolve variants: `:samples github:dxinteractive/strudel-samples@main` then evaluate `bd bd:3`.
- Default variants from the built-in map: evaluate `bd bd:3 bd:7`.
- Rest handling: evaluate `bd ~ bd` and confirm there is no warning for `~`.

## Configuring samples

The default `config.json` lists a few public repositories that host drum and synth samples from the Strudel/TidalCycles community.
The built-in kick is generated locally via `./scripts/fetch_kick.sh` to avoid storing binaries in the repository. Remote packs are
optional; the core experience runs entirely offline once the kick file exists. Musika currently requires `libcurl` for loading
remote sample maps. The melodic `tone` sample is generated locally when first used so simple melodies work without downloads.

## Keeping the repository binary-free

Generated audio assets live under `assets/` (ignored by Git). To verify the tree stays free of committed binaries, run:

```
./scripts/check_binary_files.sh            # working tree only
./scripts/check_binary_files.sh --history  # working tree + all historical blobs
```

The helper scans tracked files (and optionally history) and exits non-zero if it detects likely binary content so you can clean up before pushing updates. Use the history mode when repairing old commits before publishing the branch.
