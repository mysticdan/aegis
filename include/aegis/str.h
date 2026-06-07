#ifndef AEGIS_STR_H
#define AEGIS_STR_H

#include <stddef.h>

char *aegis_strdup(const char *s);
int aegis_streq(const char *a, const char *b);
int aegis_str_has_prefix(const char *s, const char *prefix);
size_t aegis_strlcpy(char *dst, const char *src, size_t dst_size);

#endif
