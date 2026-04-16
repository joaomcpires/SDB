/**
 * logging.c — SDB Structured Logging Implementation
 *
 * Outputs timestamped, leveled log messages to stderr.
 */

#include "logging.h"

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

static sdb_log_level_t g_min_level = SDB_LOG_INFO;

void sdb_log_set_level(sdb_log_level_t level)
{
    g_min_level = level;
}

static const char *level_string(sdb_log_level_t level)
{
    switch (level) {
    case SDB_LOG_INFO:     return "INFO";
    case SDB_LOG_WARN:     return "WARN";
    case SDB_LOG_CRITICAL: return "CRITICAL";
    default:               return "UNKNOWN";
    }
}

void sdb_log(sdb_log_level_t level, const char *fmt, ...)
{
    if (level < g_min_level)
        return;

    /* ISO 8601 timestamp */
    time_t now = time(NULL);
    struct tm utc;
    gmtime_r(&now, &utc);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &utc);

    fprintf(stderr, "[%s] %s ", level_string(level), timestamp);

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fprintf(stderr, "\n");
}
