/**
 * logging.h — SDB Structured Logging
 *
 * Technical, objective log output to stderr.
 */

#ifndef SDB_LOGGING_H
#define SDB_LOGGING_H

/** Log levels. */
typedef enum {
    SDB_LOG_INFO     = 0,
    SDB_LOG_WARN     = 1,
    SDB_LOG_CRITICAL = 2
} sdb_log_level_t;

/**
 * Set the minimum log level. Messages below this level are suppressed.
 * Default: SDB_LOG_INFO.
 */
void sdb_log_set_level(sdb_log_level_t level);

/**
 * Log a message.
 *
 * Format: [LEVEL] YYYY-MM-DDTHH:MM:SSZ message
 * Output goes to stderr.
 *
 * @param level  Log level.
 * @param fmt    printf-style format string.
 * @param ...    Format arguments.
 */
void sdb_log(sdb_log_level_t level, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

#endif /* SDB_LOGGING_H */
