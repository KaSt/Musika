#include "cache.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static unsigned long fnv1a(const char *data) {
    unsigned long hash = 1469598103934665603UL;
    const unsigned long prime = 1099511628211UL;
    for (const unsigned char *p = (const unsigned char *)data; *p; ++p) {
        hash ^= (unsigned long)(*p);
        hash *= prime;
    }
    return hash;
}

static bool ensure_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    if (mkdir(path, 0700) == 0) return true;
    return false;
}

bool cache_path_for_key_with_ext(const char *key, const char *ext, char *out_path, size_t out_len) {
    if (!key || !out_path || out_len == 0) return false;
    const char *home = getenv("HOME");
    if (!home || strlen(home) == 0) return false;
    char base_dir[512];
    if (snprintf(base_dir, sizeof(base_dir), "%s/.cache", home) >= (int)sizeof(base_dir)) return false;
    if (!ensure_dir(base_dir)) return false;
    char musika_dir[512];
    if (snprintf(musika_dir, sizeof(musika_dir), "%s/musika", base_dir) >= (int)sizeof(musika_dir)) return false;
    if (!ensure_dir(musika_dir)) return false;

    unsigned long hash = fnv1a(key);
    const char *extension = (ext && ext[0]) ? ext : ".json";
    if (snprintf(out_path, out_len, "%s/%lx%s", musika_dir, hash, extension) >= (int)out_len) return false;
    return true;
}

bool cache_path_for_key(const char *key, char *out_path, size_t out_len) {
    return cache_path_for_key_with_ext(key, ".json", out_path, out_len);
}

bool cache_write(const char *path, const char *data, size_t len) {
    if (!path || !data || len == 0) return false;
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    size_t written = fwrite(data, 1, len, f);
    fclose(f);
    return written == len;
}

