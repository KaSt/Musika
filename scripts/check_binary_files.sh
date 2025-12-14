#!/usr/bin/env bash
set -euo pipefail

# Scan tracked files for binary content to help keep the repository free of
# committed binaries (e.g., generated audio assets).
python3 - <<'PY'
import os
import string
import subprocess
import sys

PRINTABLE = set(range(32, 127)) | {9, 10, 13}

try:
    tracked = subprocess.check_output(["git", "ls-files"], text=True).splitlines()
except subprocess.CalledProcessError as exc:
    print(f"Failed to list tracked files: {exc}", file=sys.stderr)
    sys.exit(1)

suspicious = []

for path in tracked:
    try:
        with open(path, "rb") as handle:
            chunk = handle.read(4096)
    except OSError as exc:
        suspicious.append((path, f"unreadable: {exc}"))
        continue

    if not chunk:
        continue

    if b"\0" in chunk:
        suspicious.append((path, "contains NUL byte"))
        continue

    non_printable = sum(byte not in PRINTABLE for byte in chunk)
    ratio = non_printable / len(chunk)
    if ratio > 0.3:
        suspicious.append((path, f"{ratio:.0%} non-text in initial block"))

if suspicious:
    print("Potential binary files detected:\n")
    for path, reason in suspicious:
        print(f" - {path}: {reason}")
    sys.exit(2)
else:
    print("No binary files detected in tracked sources.")
PY
