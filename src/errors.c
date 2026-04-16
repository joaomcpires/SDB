/**
 * errors.c — SDB Error Code Implementation
 */

#include "errors.h"

const char *sdb_strerror(sdb_error_t err)
{
    switch (err) {
    case SDB_ERR_NONE:
        return "No error";
    case SDB_ERR_NOT_FOUND:
        return "Record does not exist in the current index state";
    case SDB_ERR_GONE:
        return "Record underwent permanent wave function collapse during operation";
    case SDB_ERR_IO:
        return "I/O operation failed";
    case SDB_ERR_ENTROPY:
        return "Entropy source read failure";
    case SDB_ERR_INVALID:
        return "Invalid argument";
    default:
        return "Unknown error";
    }
}
