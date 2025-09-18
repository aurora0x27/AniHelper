#pragma once

typedef enum { LC_LOG_INFO, LC_LOG_DEBUG, LC_LOG_WARN, LC_LOG_ERROR } lc_log_level_t;

void lc_log(lc_log_level_t level, const char *format, ...);

#define info(fmt, ...) lc_log(LC_LOG_INFO, fmt, ##__VA_ARGS__)
#define debug(fmt, ...) lc_log(LC_LOG_DEBUG, fmt, ##__VA_ARGS__)
#define warn(fmt, ...) lc_log(LC_LOG_WARN, fmt, ##__VA_ARGS__)
#define err(fmt, ...) lc_log(LC_LOG_ERROR, fmt, ##__VA_ARGS__)
