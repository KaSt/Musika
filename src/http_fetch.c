#include "http_fetch.h"

#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>

typedef struct {
    char *data;
    size_t len;
    size_t capacity;
} HttpBuffer;

static size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    HttpBuffer *buf = (HttpBuffer *)userdata;
    size_t total = size * nmemb;
    if (total == 0) return 0;

    if (buf->len + total + 1 > buf->capacity) {
        size_t new_capacity = buf->capacity ? buf->capacity * 2 : 4096;
        while (new_capacity < buf->len + total + 1) {
            new_capacity *= 2;
        }
        char *next = realloc(buf->data, new_capacity);
        if (!next) return 0;
        buf->data = next;
        buf->capacity = new_capacity;
    }

    memcpy(buf->data + buf->len, ptr, total);
    buf->len += total;
    buf->data[buf->len] = '\0';
    return total;
}

bool http_fetch_to_buffer(const char *url, char **out_buffer, size_t *out_len) {
    if (!url || !out_buffer) return false;
    *out_buffer = NULL;
    if (out_len) *out_len = 0;

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0) {
        return false;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        curl_global_cleanup();
        return false;
    }

    HttpBuffer buffer = {.data = NULL, .len = 0, .capacity = 0};

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Musika/1.0");

    CURLcode res = curl_easy_perform(curl);
    long response_code = 0;
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    }

    curl_easy_cleanup(curl);
    curl_global_cleanup();

    if (res != CURLE_OK || response_code >= 400 || buffer.len == 0) {
        free(buffer.data);
        return false;
    }

    *out_buffer = buffer.data;
    if (out_len) *out_len = buffer.len;
    return true;
}

