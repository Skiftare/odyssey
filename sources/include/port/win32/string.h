#pragma once
#include_next <string.h>
static inline char *stpncpy(char *dst, const char *src, size_t n) { // are we need for "char *restrict dst, const char *restrict src"?
    while (n > 0 && *src != '\0') {
        *dst = *src;
        dst++;
        src++;
        n--;
    }

    char *end = dst;

    while (n > 0) {
        *dst = '\0';
        dst++;
        n--;
    }

    return end;
}
