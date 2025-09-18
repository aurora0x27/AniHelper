#include <stdio.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "debug.h"

extern char debug_mode;

void lc_log(lc_log_level_t level, const char *format, ...) {
    if (!debug_mode) {
        return;
    }

#define COLOR_NONE "\033[0m"
#define RED "\033[1;31m"
#define BLUE "\033[1;34m"
#define GREEN "\033[1;32m"
#define YELLOW "\033[1;33m"

    char *msg_content = NULL;
    va_list arg_list_raw;
    va_start(arg_list_raw, format);

    va_list arg_list;
    va_copy(arg_list, arg_list_raw);
    int len = vsnprintf(NULL, 0, format, arg_list);
    if (len < 0) {
        fprintf(stderr, "Logger memery calculate error\n");
        va_end(arg_list_raw);
        return;
    }

    msg_content = (char *)malloc(len + 1);
    if (msg_content == NULL) {
        fprintf(stderr, "Logger memery alloc failure\n");
        va_end(arg_list_raw);
        return;
    }

    va_copy(arg_list, arg_list_raw);
    vsnprintf(msg_content, len + 1, format, arg_list);

    va_end(arg_list);
    va_end(arg_list_raw);
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    char time_buf[20];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);

    switch (level) {
        case LC_LOG_INFO: {
            fprintf(stderr, GREEN "[%s] [INFO] %s\n" COLOR_NONE, time_buf, msg_content);
            break;
        }
        case LC_LOG_DEBUG: {
            fprintf(stderr, BLUE "[%s] [DEBUG]: %s\n" COLOR_NONE, time_buf, msg_content);
            break;
        }
        case LC_LOG_WARN: {
            fprintf(stderr, YELLOW "[%s] [WARN]: %s\n" COLOR_NONE, time_buf, msg_content);
            break;
        }
        case LC_LOG_ERROR: {
            fprintf(stderr, RED "[%s] [ERROR]: %s\n" COLOR_NONE, time_buf, msg_content);
            break;
        }
        default:
            // Assert unreachable
            assert(0);
    }

    free(msg_content);

#undef COLOR_NONE
#undef RED
#undef BLUE
#undef GREEN
#undef YELLOW
}
