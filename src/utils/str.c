#include "aegis/str.h"

char *aegis_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *copy = malloc(len + 1);
    if (!copy) return NULL;
    memcpy(copy, s, len + 1);
    return copy;
}

int aegis_streq(const char *a, const char *b) {
    if (!a || !b) return 0;
    return strcmp(a, b) == 0;
}

int aegis_str_has_prefix(const char *s, const char *prefix) {
    if (!s || !prefix) return 0;
    size_t prefix_len = strlen(prefix);
    size_t s_len = strlen(s);
    if (prefix_len > s_len) return 0;
    return strncmp(s, prefix, prefix_len) == 0;
}

size_t aegis_strlcpy(char *dst, const char *src, size_t dst_size) {
    if (!dst || !src || dst_size == 0) return 0;
    size_t src_len = strlen(src);
    size_t copy_len = (src_len < dst_size - 1) ? src_len : dst_size - 1;
    memcpy(dst, src, copy_len);
    dst[copy_len] = '\0';
    return src_len;
}

