#ifndef MUSIKA_HTTP_FETCH_H
#define MUSIKA_HTTP_FETCH_H

#include <stddef.h>
#include <stdbool.h>

bool http_fetch_to_buffer(const char *url, char **out_buffer, size_t *out_len);

#endif // MUSIKA_HTTP_FETCH_H

