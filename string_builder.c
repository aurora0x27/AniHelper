#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

#include "string_builder.h"

void sb_init(StringBuilder *sb) {
    sb->data = malloc(64);
    sb->size = 0;
    sb->cap = 64;
    sb->data[0] = '\0';
}

StringBuilder *sb_new() {
    StringBuilder *sb = malloc(sizeof(StringBuilder));
    if (!sb) {
        return NULL;
    }
    sb_init(sb);
    return sb;
}

void sb_cleanup(StringBuilder *sb) {
    if (sb) {
        if (sb->data) {
            free(sb->data);
        }
        free(sb);
    }
}

void sb_appendf(StringBuilder *sb, const char *fmt, ...) {
    va_list ap;
    while (1) {
        va_start(ap, fmt);
        int n = vsnprintf(sb->data + sb->size, sb->cap - sb->size, fmt, ap);
        va_end(ap);

        if (n < 0) {
            return;
        }
        if (sb->size + n < sb->cap) {
            sb->size += n;
            return;
        }
        sb->cap *= 2;
        sb->data = realloc(sb->data, sb->cap);
    }
}
