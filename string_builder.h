#pragma once

#include <unistd.h>

typedef struct {
    char *data;
    size_t size;
    size_t cap;
} StringBuilder;

void sb_init(StringBuilder *sb);

StringBuilder *sb_new();

void sb_cleanup(StringBuilder *sb);

void sb_appendf(StringBuilder *sb, const char *fmt, ...);
