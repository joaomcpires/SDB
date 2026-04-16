/**
 * errors.h — SDB Error Codes & Types
 *
 * Technical, objective error reporting with no editorializing.
 */

#ifndef SDB_ERRORS_H
#define SDB_ERRORS_H

/** SDB error codes. */
typedef enum {
    SDB_ERR_NONE      = 0,   /**< Success. */
    SDB_ERR_NOT_FOUND = 404, /**< Record does not exist in the index. */
    SDB_ERR_GONE      = 410, /**< Record collapsed during operation. */
    SDB_ERR_IO        = 1,   /**< I/O failure (check errno). */
    SDB_ERR_ENTROPY   = 2,   /**< Entropy source read failure. */
    SDB_ERR_INVALID   = 3    /**< Invalid argument. */
} sdb_error_t;

/** Observation outcome. */
typedef enum {
    SDB_OBSERVE_OK        = 0, /**< Data returned successfully. */
    SDB_OBSERVE_COLLAPSED = 1, /**< Record destroyed during observation. */
    SDB_OBSERVE_NOT_FOUND = 2  /**< Record not in index. */
} sdb_observe_result_t;

/** Mutation outcome. */
typedef enum {
    SDB_MUTATE_OK                 = 0, /**< Mutation succeeded (~25% path). */
    SDB_MUTATE_ORIGINAL_COLLAPSED = 1, /**< Original destroyed on observe. */
    SDB_MUTATE_WRITE_ABORTED      = 2, /**< Observe OK but write gate failed. */
    SDB_MUTATE_NOT_FOUND          = 3  /**< Original record not in index. */
} sdb_mutate_result_t;

/**
 * Return a technical description string for an SDB error code.
 */
const char *sdb_strerror(sdb_error_t err);

#endif /* SDB_ERRORS_H */
