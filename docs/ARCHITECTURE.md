# Musika — Terminal Live Coding Language & Engine (Mac/Linux)

## Goal
Build a **terminal-first** live coding environment (like Strudel/Tidal concepts) with:
- **Builtin editor** (TUI), REPL, and transport controls
- **Low-latency audio output** on macOS + Linux
- A small, expressive **pattern language** with deterministic scheduling
- A stable plugin surface for synths/samples/effects
- Zero browser dependency

Non-goals (for v1):
- Full DAW features, timeline editing, VST hosting, Ableton integration
- Network collaboration
- Fancy GPU visualizers

## Product requirements (must-have)
1. `musika` runs in a terminal on macOS and Linux.
2. Start audio in <2 seconds; first sound within one bar at 120 BPM.
3. Hot reload patterns while playing (no glitch > 1 buffer).
4. Deterministic scheduling: same seed + same code => same result.
5. Crash safety: audio thread must never allocate/free or do I/O.

## Architecture overview
Musika is split into 5 layers:

1) **Parser / AST**
- Parse source into an AST for patterns and instrument graphs.
- Keep a clean grammar: explicit precedence, no “magic” parsing.

2) **Semantic model**
- Compile AST into an immutable **Program**:
  - Pattern objects (time -> events)
  - Instrument graph (nodes, params)
  - Global config (tempo, swing, seed)

3) **Scheduler**
- Produces events for a rolling window: e.g. next 200ms / next 1 bar.
- Uses a monotonic clock.
- Separates:
  - *Control timeline* (events)
  - *Audio timeline* (sample-accurate render times)

4) **Audio engine**
- Real-time thread renders audio buffers.
- Receives event batches via lock-free queue (SPSC ring buffer).
- Supports:
  - Sample playback (time-stretch optional later)
  - Basic synth (osc, env, filter)
  - Mixer + gain + pan
  - FX: delay, reverb (simple), LPF/HPF

5) **TUI editor + REPL**
- Single binary with built-in editor:
  - Left: editor
  - Right/bottom: REPL + status + meters
- Keybinds: save, eval selection, eval buffer, mute/solo track.

## Audio backend strategy (cross-platform)
Prefer one abstraction with two backends:

- macOS: **CoreAudio** via AudioUnit/AudioQueue
- Linux: **PipeWire / PulseAudio / ALSA** (choose one primary; ALSA is the most direct)

Pragmatic option:
- Use **miniaudio** or **PortAudio** as the portability layer,
  BUT keep backend isolated so we can drop it if needed.

Hard rules:
- Audio callback thread:
  - no malloc/free
  - no file I/O
  - no locks (mutex)
  - no logging
- Preload samples, prebuild DSP graphs.

## Language principles (v1)
### Time model
- Musical time in cycles/bars/beats.
- Global tempo; patterns map musical time to event lists.

### Core constructs
- `tempo 150`
- `let kick = sound("bd").every(4, accent)`
- `play(kick + bass + hats)`

### Pattern operators (minimum set)
- sequencing: `a then b`
- stacking: `a | b` (parallel)
- density: `a * 2` (twice as dense)
- slow: `a / 2`
- choose: `choose([a,b,c], seed)`
- euclid: `euclid(3,8, sound("hh"))`
- map: `a.map(note => note + 12)`

### Event schema
An event is:
- time (musical + sample-accurate target)
- duration (optional)
- instrument id
- params (note, gain, pan, cutoff, sample, etc.)
- per-event seed (for deterministic randomness)

## Editor / UX requirements
- Single command: `musika` opens TUI.
- Keys:
  - F5: evaluate buffer
  - F6: evaluate selection
  - Space: start/stop transport
  - Ctrl+M: metronome toggle
  - Ctrl+K: kill all sounds (panic)
- Status line shows:
  - tempo, CPU%, xruns/dropouts, buffer size, backend name
- A `--headless` mode:
  - `musika --headless file.mus` plays without TUI

## Testing & correctness
Must include:
- Parser unit tests (golden files)
- Determinism tests (same seed => same event stream)
- Scheduler jitter tests (event deadlines are met)
- Audio safety: CI check forbids allocation in callback (where possible)

## Increment plan (do not skip)
Milestone 0: "Beep"
- Produce a sine wave, output audio reliably on macOS + Linux.

Milestone 1: "Kick"
- Load one built-in sample, schedule on quarter notes.

Milestone 2: "Patterns"
- Parse a minimal language, hot-reload while playing.

Milestone 3: "Instruments"
- Add synth voice, envelope, filter, per-event params.

Milestone 4: "TUI"
- Embed editor, eval selection, status, meters.

Milestone 5: "Polish"
- Presets, docs, examples, performance tuning.

## Repo structure (recommended)
- /cmd/musika        (main entry)
- /core              (AST, compiler)
- /sched             (scheduler, event stream)
- /audio             (engine, backends, dsp)
- /tui               (editor, repl, ui)
- /examples          (*.mus)
- /docs              (this file, language spec)
- /tests

## Definition of done (v1)
- User can live-code a 4-track loop in terminal,
  with stable audio for 30 minutes, no crashes, no drift.
