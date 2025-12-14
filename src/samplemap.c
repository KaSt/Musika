#include "samplemap.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *embedded_default_map =
    "{\n"
    "  \"_base\": \"assets/samples\",\n"
    "  \"bd\": [\n"
    "    \"bd/boom.wav\",\n"
    "    \"bd/doom.wav\"\n"
    "  ],\n"
    "  \"sd\": [\n"
    "    \"sd/snare.wav\",\n"
    "    \"sd/tight.wav\"\n"
    "  ],\n"
    "  \"hh\": [\n"
    "    \"hh/hat1.wav\",\n"
    "    \"hh/hat2.wav\",\n"
    "    \"hh/hat3.wav\",\n"
    "    \"hh/hat4.wav\"\n"
    "  ],\n"
    "  \"oh\": [\n"
    "    \"oh/open1.wav\",\n"
    "    \"oh/open2.wav\"\n"
    "  ],\n"
    "  \"misc\": [\n"
    "    \"misc/cowbell.wav\",\n"
    "    \"misc/clap.wav\"\n"
    "  ]\n"
    "}\n";

static char *dup_range(const char *start, size_t len) {
    char *out = malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, start, len);
    out[len] = '\0';
    return out;
}

static char *load_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long len = ftell(f);
    if (len < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    char *buf = malloc((size_t)len + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t read_len = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[read_len] = '\0';
    if (out_len) *out_len = read_len;
    return buf;
}

static const char *skip_ws(const char *p) {
    while (p && *p && isspace((unsigned char)*p)) {
        ++p;
    }
    return p;
}

static char *parse_string(const char **p) {
    const char *s = skip_ws(*p);
    if (!s || *s != '"') return NULL;
    s++;
    const char *start = s;
    while (*s && *s != '"') {
        if (*s == '\\' && s[1]) {
            s += 2;
            continue;
        }
        s++;
    }
    if (*s != '"') return NULL;
    char *out = dup_range(start, (size_t)(s - start));
    *p = s + 1;
    return out;
}

static bool append_variant(SampleSound *sound, const char *value, size_t len) {
    if (!sound || !value || len == 0) return false;
    char **next = realloc(sound->variants, sizeof(char *) * (sound->variant_count + 1));
    if (!next) return false;
    sound->variants = next;
    sound->variants[sound->variant_count] = dup_range(value, len);
    if (!sound->variants[sound->variant_count]) return false;
    sound->variant_count += 1;
    return true;
}

static bool parse_value(const char **p, SampleSound *sound) {
    const char *s = skip_ws(*p);
    if (!s) return false;
    if (*s == '"') {
        char *val = parse_string(&s);
        if (!val) return false;
        bool ok = append_variant(sound, val, strlen(val));
        free(val);
        *p = s;
        return ok;
    }
    if (*s == '[') {
        s++;
        while (*s && *s != ']') {
            char *val = parse_string(&s);
            if (val) {
                if (!append_variant(sound, val, strlen(val))) {
                    free(val);
                    return false;
                }
                free(val);
            }
            s = skip_ws(s);
            if (*s == ',') {
                s++;
            }
        }
        if (*s == ']') {
            *p = s + 1;
            return sound->variant_count > 0;
        }
    }
    return false;
}

static bool append_sound(SampleRegistry *registry, const char *name, size_t len, SampleSound **out) {
    if (!registry || !name || len == 0) return false;
    SampleSound *next = realloc(registry->sounds, sizeof(SampleSound) * (registry->sound_count + 1));
    if (!next) return false;
    registry->sounds = next;
    SampleSound *sound = &registry->sounds[registry->sound_count];
    sound->name = dup_range(name, len);
    sound->variants = NULL;
    sound->variant_count = 0;
    if (!sound->name) return false;
    registry->sound_count += 1;
    if (out) *out = sound;
    return true;
}

static bool parse_object(const char *json, SampleRegistry *registry) {
    const char *p = skip_ws(json);
    if (!p || *p != '{') return false;
    p++;

    while (p && *p) {
        p = skip_ws(p);
        if (*p == '}') {
            return true;
        }
        char *key = parse_string(&p);
        if (!key) return false;
        p = skip_ws(p);
        if (*p != ':') {
            free(key);
            return false;
        }
        p++;

        if (strcmp(key, "_base") == 0) {
            char *base = parse_string(&p);
            if (!base) {
                free(key);
                return false;
            }
            free(registry->base);
            registry->base = base;
        } else {
            SampleSound *sound = NULL;
            if (!append_sound(registry, key, strlen(key), &sound)) {
                free(key);
                return false;
            }
            if (!parse_value(&p, sound)) {
                free(key);
                return false;
            }
        }
        free(key);
        p = skip_ws(p);
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p == '}') {
            return true;
        }
    }
    return false;
}

static void free_sound(SampleSound *sound) {
    if (!sound) return;
    free(sound->name);
    if (sound->variants) {
        for (size_t i = 0; i < sound->variant_count; ++i) {
            free(sound->variants[i]);
        }
        free(sound->variants);
    }
}

void sample_registry_free(SampleRegistry *registry) {
    if (!registry) return;
    free(registry->name);
    free(registry->base);
    if (registry->sounds) {
        for (size_t i = 0; i < registry->sound_count; ++i) {
            free_sound(&registry->sounds[i]);
        }
        free(registry->sounds);
    }
    registry->name = NULL;
    registry->base = NULL;
    registry->sounds = NULL;
    registry->sound_count = 0;
}

bool sample_registry_load_default(SampleRegistry *registry) {
    if (!registry) return false;
    memset(registry, 0, sizeof(*registry));
    registry->name = dup_range("default", strlen("default"));
    if (!registry->name) return false;

    size_t len = 0;
    char *json = load_file("assets/default_samplemap.json", &len);
    if (!json || len == 0) {
        free(json);
        json = dup_range(embedded_default_map, strlen(embedded_default_map));
    }
    if (!json) return false;

    bool ok = parse_object(json, registry);
    free(json);
    if (!ok) {
        sample_registry_free(registry);
    }
    return ok;
}

void sample_registry_print(const SampleRegistry *registry, FILE *out) {
    if (!registry || !out) return;
    fprintf(out, "Registry: %s\n", registry->name ? registry->name : "(unknown)");
    for (size_t i = 0; i < registry->sound_count; ++i) {
        const SampleSound *sound = &registry->sounds[i];
        fprintf(out, "  %s(%zu)\n", sound->name ? sound->name : "(unnamed)", sound->variant_count);
    }
}
