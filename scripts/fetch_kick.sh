#!/usr/bin/env bash
set -euo pipefail

ASSETS_DIR="assets"
OUTPUT="$ASSETS_DIR/kick.wav"

mkdir -p "$ASSETS_DIR"

python3 - <<'PY'
import math
import os
import struct
import wave

ASSETS_DIR = "assets"
OUTPUT = os.path.join(ASSETS_DIR, "kick.wav")
SAMPLE_RATE = 48000
DURATION_SEC = 0.5
BASE_FREQ = 60.0
DECAY = 5.0

os.makedirs(ASSETS_DIR, exist_ok=True)
frames = int(SAMPLE_RATE * DURATION_SEC)

with wave.open(OUTPUT, "w") as wf:
    wf.setnchannels(1)
    wf.setsampwidth(2)
    wf.setframerate(SAMPLE_RATE)

    for i in range(frames):
        t = i / SAMPLE_RATE
        env = math.exp(-DECAY * t)
        value = math.sin(2 * math.pi * BASE_FREQ * t) * env
        sample = max(-1.0, min(1.0, value))
        wf.writeframes(struct.pack("<h", int(sample * 32767)))

print(f"Wrote {OUTPUT} ({DURATION_SEC:.2f}s @ {SAMPLE_RATE} Hz)")
PY

echo "Kick sample ready at $OUTPUT"
