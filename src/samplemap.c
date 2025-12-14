#include "samplemap.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "cache.h"
#include "http_fetch.h"

static const char *embedded_default_map =
    "{\n"
    "  \"_base\": \"https://cdn.jsdelivr.net/gh/dxinteractive/strudel-samples@main/\",\n"
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
    "  ],\n"
    "  \"tone\": [\n"
    "    \"./assets/tone.wav\"\n"
    "  ]\n"
    "}\n";

static int semitone_for_letter(char c) {
    switch (tolower((unsigned char)c)) {
        case 'c': return 0;
        case 'd': return 2;
        case 'e': return 4;
        case 'f': return 5;
        case 'g': return 7;
        case 'a': return 9;
        case 'b': return 11;
        default: return -1;
    }
}

static bool midi_from_note_name(const char *text, int *out_midi) {
    if (!text || !out_midi || text[0] == '\0') return false;
    int base = semitone_for_letter(text[0]);
    if (base < 0) return false;

    size_t idx = 1;
    int accidental = 0;
    char accidental_char = text[idx];
    if (accidental_char == '#' || tolower((unsigned char)accidental_char) == 'b') {
        accidental = (accidental_char == '#') ? 1 : -1;
        idx++;
    }

    if (!isdigit((unsigned char)text[idx])) {
        return false;
    }

    char *end = NULL;
    long octave = strtol(&text[idx], &end, 10);
    if (end == &text[idx] || (*end != '\0' && !isspace((unsigned char)*end))) {
        return false;
    }

    int midi = (int)((octave + 1) * 12 + base + accidental);
    if (midi < 0) midi = 0;
    if (midi > 127) midi = 127;
    *out_midi = midi;
    return true;
}

static char *dup_range(const char *start, size_t len) {
    char *out = malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, start, len);
    out[len] = '\0';
    return out;
}

static bool append_pitched_entry(SampleSound *sound, const char *key, const char *variant) {
    if (!sound || !key || !variant) return false;
    char **next_keys = realloc(sound->pitched_keys, sizeof(char *) * (sound->pitched_entry_count + 1));
    int *next_midi = realloc(sound->pitched_midi, sizeof(int) * (sound->pitched_entry_count + 1));
    char **next_variants = realloc(sound->pitched_variants, sizeof(char *) * (sound->pitched_entry_count + 1));
    if (!next_keys || !next_midi || !next_variants) {
        free(next_keys);
        free(next_midi);
        free(next_variants);
        return false;
    }
    sound->pitched_keys = next_keys;
    sound->pitched_midi = next_midi;
    sound->pitched_variants = next_variants;
    sound->pitched_keys[sound->pitched_entry_count] = dup_range(key, strlen(key));
    if (!sound->pitched_keys[sound->pitched_entry_count]) return false;
    int midi = 0;
    if (!midi_from_note_name(key, &midi)) {
        fprintf(stderr, "Warning: ignoring pitched sample entry with unrecognized key '%s'\n", key);
        free(sound->pitched_keys[sound->pitched_entry_count]);
        sound->pitched_keys[sound->pitched_entry_count] = NULL;
        return false;
    }
    sound->pitched_midi[sound->pitched_entry_count] = midi;
    sound->pitched_variants[sound->pitched_entry_count] = dup_range(variant, strlen(variant));
    if (!sound->pitched_variants[sound->pitched_entry_count]) {
        free(sound->pitched_keys[sound->pitched_entry_count]);
        sound->pitched_keys[sound->pitched_entry_count] = NULL;
        return false;
    }
    sound->pitched_entry_count += 1;
    sound->variant_count = sound->pitched_entry_count;
    return true;
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
    if (read_len != (size_t)len) {
        free(buf);
        return NULL;
    }
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

static bool parse_pitched_map_json(const char *json, SampleSound *sound) {
    const char *p = skip_ws(json);
    if (!p || *p != '{' || !sound) return false;
    p++;
    while (p && *p) {
        p = skip_ws(p);
        if (*p == '}') {
            return sound->pitched_entry_count > 0;
        }
        char *key = parse_string(&p);
        if (!key) return false;
        p = skip_ws(p);
        if (*p != ':') {
            free(key);
            return false;
        }
        p++;
        p = skip_ws(p);

        char *variant = NULL;
        if (*p == '"') {
            variant = parse_string(&p);
        } else if (*p == '[') {
            p++;
            while (*p && *p != ']') {
                char *candidate = parse_string(&p);
                if (candidate && !variant) {
                    variant = candidate;
                } else if (candidate) {
                    free(candidate);
                }
                p = skip_ws(p);
                if (*p == ',') {
                    p++;
                    continue;
                }
                if (*p == ']') {
                    break;
                }
                break;
            }
            if (*p == ']') {
                p++;
            }
        }

        bool ok = variant != NULL && append_pitched_entry(sound, key, variant);
        free(key);
        free(variant);
        if (!ok) return false;

        p = skip_ws(p);
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p == '}') {
            return true;
        }
        return false;
    }
    return sound->pitched_entry_count > 0;
}

static char *extract_object_json(const char **p) {
    const char *s = skip_ws(*p);
    if (!s || *s != '{') return NULL;
    int depth = 0;
    bool in_string = false;
    bool escape = false;
    const char *start = s;
    while (*s) {
        char c = *s;
        if (escape) {
            escape = false;
            s++;
            continue;
        }
        if (c == '\\') {
            escape = true;
            s++;
            continue;
        }
        if (c == '"') {
            in_string = !in_string;
            s++;
            continue;
        }
        if (!in_string) {
            if (c == '{') {
                depth++;
            } else if (c == '}') {
                depth--;
                if (depth == 0) {
                    s++;
                    char *json = dup_range(start, (size_t)(s - start));
                    *p = s;
                    return json;
                }
            }
        }
        s++;
    }
    return NULL;
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
                s = skip_ws(s);
                if (*s == ',') {
                    s++;
                    continue;
                }
                if (*s == ']') {
                    break;
                }
                return false;
            }
            s = skip_ws(s);
            if (*s == ']') {
                break;
            }
            if (*s == ',') {
                s++;
                continue;
            }
            return false;
        }
        if (*s == ']') {
            *p = s + 1;
            return sound->variant_count > 0;
        }
    }
    if (*s == '{') {
        char *object_json = extract_object_json(&s);
        if (!object_json) return false;
        sound->pitched_map_json = object_json;
        sound->pitched_entry_count = 0;
        sound->variant_count = 0;
        if (!parse_pitched_map_json(object_json, sound)) {
            return false;
        }
        *p = s;
        return true;
    }
    return false;
}

static bool append_sound(SampleRegistry *registry, const char *name, size_t len, SampleSound **out) {
    if (!registry || !name || len == 0) return false;
    SampleSound *next = realloc(registry->sounds, sizeof(SampleSound) * (registry->sound_count + 1));
    if (!next) return false;
    registry->sounds = next;
    SampleSound *sound = &registry->sounds[registry->sound_count];
    memset(sound, 0, sizeof(*sound));
    sound->name = dup_range(name, len);
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
            if (!base || base[0] == '\0') {
                free(base);
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

static bool validate_registry(const SampleRegistry *registry) {
    if (!registry || !registry->name || registry->name[0] == '\0' || registry->sound_count == 0) {
        return false;
    }
    if (registry->base && registry->base[0] == '\0') {
        return false;
    }
    for (size_t i = 0; i < registry->sound_count; ++i) {
        const SampleSound *sound = &registry->sounds[i];
        if (!sound->name || sound->name[0] == '\0') {
            return false;
        }
        if (sound->variant_count == 0 && !sound->pitched_map_json) {
            return false;
        }
        if (sound->variants) {
            for (size_t v = 0; v < sound->variant_count; ++v) {
                if (!sound->variants[v] || sound->variants[v][0] == '\0') {
                    return false;
                }
            }
        }
        if (sound->pitched_map_json) {
            if (sound->pitched_entry_count == 0 || !sound->pitched_keys || !sound->pitched_variants || !sound->pitched_midi) {
                return false;
            }
            for (size_t v = 0; v < sound->pitched_entry_count; ++v) {
                if (!sound->pitched_keys[v] || !sound->pitched_variants[v]) {
                    return false;
                }
            }
        }
    }
    return true;
}

static void free_sound(SampleSound *sound) {
    if (!sound) return;
    free(sound->name);
    free(sound->pitched_map_json);
    if (sound->pitched_keys) {
        for (size_t i = 0; i < sound->pitched_entry_count; ++i) {
            free(sound->pitched_keys[i]);
            free(sound->pitched_variants ? sound->pitched_variants[i] : NULL);
        }
    }
    free(sound->pitched_keys);
    free(sound->pitched_midi);
    free(sound->pitched_variants);
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
    bool ok = false;
    if (json && len > 0) {
        ok = parse_object(json, registry) && validate_registry(registry);
    }
    if (!ok) {
        free(json);
        sample_registry_free(registry);
        memset(registry, 0, sizeof(*registry));
        registry->name = dup_range("default", strlen("default"));
        if (!registry->name) return false;
        json = dup_range(embedded_default_map, strlen(embedded_default_map));
        if (!json) return false;
        ok = parse_object(json, registry) && validate_registry(registry);
    }

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
        size_t count = sound->variant_count > 0 ? sound->variant_count : (sound->pitched_map_json ? 1 : 0);
        fprintf(out, "  %s(%zu)\n", sound->name ? sound->name : "(unnamed)", count);
    }
}

static const SampleSound *find_sound(const SampleRegistry *registry, const char *name) {
    if (!registry || !name) return NULL;
    for (size_t i = 0; i < registry->sound_count; ++i) {
        if (registry->sounds[i].name && strcmp(registry->sounds[i].name, name) == 0) {
            return &registry->sounds[i];
        }
    }
    return NULL;
}

const SampleSound *sample_registry_find_sound(const SampleRegistry *registry, const char *name) {
    return find_sound(registry, name);
}

void sample_registry_print_merged(const SampleRegistry *default_registry, const SampleRegistry *user_registry, const char *filter, FILE *out) {
    if (!out) return;
    bool show_default = true;
    bool show_user = true;
    if (filter && strcmp(filter, "default") == 0) {
        show_user = false;
    } else if (filter && strcmp(filter, "user") == 0) {
        show_default = false;
    }
    if (show_user && user_registry && user_registry->sound_count > 0) {
        for (size_t i = 0; i < user_registry->sound_count; ++i) {
            const SampleSound *sound = &user_registry->sounds[i];
            size_t count = sound->variant_count > 0 ? sound->variant_count : (sound->pitched_map_json ? 1 : 0);
            fprintf(out, "[user] %s (%zu)\n", sound->name ? sound->name : "(unnamed)", count);
        }
    }
    if (show_default && default_registry) {
        for (size_t i = 0; i < default_registry->sound_count; ++i) {
            const SampleSound *sound = &default_registry->sounds[i];
            if (show_user && find_sound(user_registry, sound->name)) {
                continue;
            }
            size_t count = sound->variant_count > 0 ? sound->variant_count : (sound->pitched_map_json ? 1 : 0);
            fprintf(out, "[default] %s (%zu)\n", sound->name ? sound->name : "(unnamed)", count);
        }
    }
}

static bool registry_from_json(SampleRegistry *registry, const char *json, const char *name) {
    if (!registry || !json) return false;
    memset(registry, 0, sizeof(*registry));
    registry->name = dup_range(name ? name : "user", strlen(name ? name : "user"));
    if (!registry->name) return false;
    bool ok = parse_object(json, registry) && validate_registry(registry);
    if (!ok) {
        sample_registry_free(registry);
    }
    return ok;
}

static bool resolve_github_url(const char *source, char *url, size_t url_len) {
    const char *p = source + strlen("github:");
    const char *slash = strchr(p, '/');
    if (!slash) return false;
    const char *repo = slash + 1;
    const char *at = strchr(repo, '@');
    const char *ref = "main";
    size_t repo_len = at ? (size_t)(at - repo) : strlen(repo);
    if (at) {
        ref = at + 1;
    }
    size_t user_len = (size_t)(slash - p);
    if (user_len == 0 || repo_len == 0) return false;
    if (snprintf(url, url_len, "https://raw.githubusercontent.com/%.*s/%.*s/%s/strudel.json", (int)user_len, p, (int)repo_len, repo, ref) >= (int)url_len) {
        return false;
    }
    return true;
}

static bool resolve_source_url(const char *source, char *url, size_t url_len) {
    if (!source) return false;
    if (strncmp(source, "github:", 7) == 0) {
        return resolve_github_url(source, url, url_len);
    }
    if (strncmp(source, "http://", 7) == 0 || strncmp(source, "https://", 8) == 0) {
        if (strlen(source) + 1 > url_len) return false;
        memcpy(url, source, strlen(source) + 1);
        return true;
    }
    return false;
}

static bool file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

bool sample_registry_load_from_source(SampleRegistry *registry, const char *source, const char *name, bool refresh, char *cache_path, size_t cache_path_len, bool *out_cached, char *resolved_url, size_t resolved_url_len, char *error, size_t error_len) {
    if (out_cached) *out_cached = false;
    if (error && error_len > 0) error[0] = '\0';
    if (!registry || !source) return false;
    char url[512];
    if (!resolve_source_url(source, url, sizeof(url))) {
        if (error) snprintf(error, error_len, "Unrecognized source '%s'", source);
        return false;
    }

    if (resolved_url && resolved_url_len > 0) {
        snprintf(resolved_url, resolved_url_len, "%s", url);
    }

    char resolved_cache_path[512];
    if (!cache_path_for_key(source, resolved_cache_path, sizeof(resolved_cache_path))) {
        if (error) snprintf(error, error_len, "Failed to resolve cache path");
        return false;
    }
    if (cache_path && cache_path_len > 0) {
        snprintf(cache_path, cache_path_len, "%s", resolved_cache_path);
    }

    char *json = NULL;
    size_t len = 0;
    bool loaded_from_cache = false;
    if (!refresh && file_exists(resolved_cache_path)) {
        json = load_file(resolved_cache_path, &len);
        if (json) {
            loaded_from_cache = true;
        }
    }

    if (!json) {
        if (!http_fetch_to_buffer(url, &json, &len)) {
            if (error) snprintf(error, error_len, "Failed to fetch %s", url);
            return false;
        }
        if (!cache_write(resolved_cache_path, json, len)) {
            if (error) snprintf(error, error_len, "Failed to write cache file");
            free(json);
            return false;
        }
    }

    SampleRegistry parsed;
    bool ok = registry_from_json(&parsed, json, name ? name : "user");
    free(json);
    if (!ok) {
        if (error) snprintf(error, error_len, "Malformed sample map");
        return false;
    }

    if (registry->name || registry->sounds) {
        sample_registry_free(registry);
    }
    *registry = parsed;
    if (cache_path && cache_path_len > 0) {
        snprintf(cache_path, cache_path_len, "%s", resolved_cache_path);
    }
    if (out_cached) *out_cached = loaded_from_cache;
    return true;
}

