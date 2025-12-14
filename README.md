# Musika

Music made code, or code made music.

Musika is a tiny terminal-first live coding playground inspired by TidalCycles and Strudel. It ships as a single C program that loads a `config.json` to discover sample repositories and playback settings. The current implementation focuses on text-first composition and a simulated audio timeline so it runs anywhere without extra dependencies.

## Features

- Inline text editor inside the terminal to sketch multi-line patterns.
- Minimal pattern language with tempo modifiers (`fast(n)`, `slow(n)`) and stacked phrases (`[bd/sn/hh]`).
- Configurable tempo, audio backend name, and remote sample packs through `config.json`.
- Cross-platform friendly C11 codebase with no external runtime dependencies.

## Building

```bash
make
```

This produces the `musika` binary.

## Running

```bash
./musika
```

Type `:help` for an overview of commands. Enter `:edit` to open the inline buffer, type pattern lines, and finish with a single `.` line. Use `:run` to render the pattern timeline.

## Configuring samples

The default `config.json` lists a few public repositories that host drum and synth samples from the Strudel/TidalCycles community. Replace or add URLs to point Musika at your own packs.
