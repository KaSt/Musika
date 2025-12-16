#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/musika"
PATTERN="$ROOT/tests/fixtures/simple_pattern.musika"
EXPECTED="$ROOT/tests/fixtures/simple_pattern_expected.json"
OUTPUT="$(mktemp)"

"$BIN" --midi-dump "$OUTPUT" "$PATTERN"

diff -u "$EXPECTED" "$OUTPUT"

echo "midi_dump_test: pass"
