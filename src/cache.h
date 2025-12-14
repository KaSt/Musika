#ifndef MUSIKA_CACHE_H
#define MUSIKA_CACHE_H

#include <stddef.h>
#include <stdbool.h>

bool cache_path_for_key(const char *key, char *out_path, size_t out_len);
bool cache_path_for_key_with_ext(const char *key, const char *ext, char *out_path, size_t out_len);
bool cache_write(const char *path, const char *data, size_t len);

#endif // MUSIKA_CACHE_H

