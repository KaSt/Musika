#!/usr/bin/env bash
set -euo pipefail

# Scan tracked files (and, optionally, historical blobs) for binary content to
# help keep the repository free of committed binaries (e.g., generated audio
# assets).
python3 - "$@" <<'PY'
import os
import string
import subprocess
import sys

PRINTABLE = set(range(32, 127)) | {9, 10, 13}
HISTORY_MODE = "--history" in sys.argv[1:]


def check_blob(name: str, data: bytes):
    if not data:
        return None
    if b"\0" in data:
        return f"contains NUL byte"
    non_printable = sum(byte not in PRINTABLE for byte in data)
    ratio = non_printable / len(data)
    if ratio > 0.3:
        return f"{ratio:.0%} non-text in initial block"
    return None


def check_working_tree():
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

        reason = check_blob(path, chunk)
        if reason:
            suspicious.append((path, reason))

    return suspicious


def check_history():
    try:
        entries = subprocess.check_output(
            ["git", "rev-list", "--objects", "--all"], text=True
        ).splitlines()
    except subprocess.CalledProcessError as exc:
        print(f"Failed to walk history: {exc}", file=sys.stderr)
        sys.exit(1)

    seen_hashes = set()
    object_list = []
    for line in entries:
        parts = line.strip().split(maxsplit=1)
        if not parts:
            continue
        obj_hash = parts[0]
        path = parts[1] if len(parts) > 1 else obj_hash
        if obj_hash in seen_hashes:
            continue
        seen_hashes.add(obj_hash)
        object_list.append((obj_hash, path))

    suspicious = []

    try:
        with subprocess.Popen(
            ["git", "cat-file", "--batch"], stdin=subprocess.PIPE, stdout=subprocess.PIPE
        ) as proc:
            assert proc.stdin is not None and proc.stdout is not None
            for obj_hash, path in object_list:
                proc.stdin.write(f"{obj_hash}\n".encode())
                proc.stdin.flush()
                header = proc.stdout.readline()
                if not header:
                    suspicious.append((path, "unable to read object header"))
                    break
                header_parts = header.decode(errors="ignore").strip().split()
                if len(header_parts) < 3:
                    suspicious.append((path, f"unexpected header: {header!r}"))
                    continue
                _, obj_type, size_str = header_parts[:3]
                try:
                    size = int(size_str)
                except ValueError:
                    suspicious.append((path, f"invalid size in header: {header!r}"))
                    continue

                if obj_type != "blob":
                    # Skip non-blob objects while consuming their content.
                    remaining = size
                    while remaining > 0:
                        chunk = proc.stdout.read(min(4096, remaining))
                        if not chunk:
                            break
                        remaining -= len(chunk)
                    proc.stdout.readline()  # trailing newline
                    continue

                chunk = proc.stdout.read(min(4096, size))
                remaining = size - len(chunk)
                while remaining > 0:
                    skipped = proc.stdout.read(min(4096, remaining))
                    if not skipped:
                        break
                    remaining -= len(skipped)
                proc.stdout.readline()  # trailing newline

                reason = check_blob(path, chunk)
                if reason:
                    suspicious.append((path, reason))

            proc.stdin.close()
            proc.wait()
    except subprocess.CalledProcessError as exc:
        suspicious.append(("history", f"git cat-file failed: {exc}"))

    return suspicious


def main():
    suspicious = check_working_tree()
    if HISTORY_MODE:
        suspicious.extend(check_history())

    if suspicious:
        print("Potential binary files detected:\n")
        for path, reason in suspicious:
            print(f" - {path}: {reason}")
        sys.exit(2)
    else:
        if HISTORY_MODE:
            print("No binary files detected in working tree or history.")
        else:
            print("No binary files detected in tracked sources.")


if __name__ == "__main__":
    main()
PY
