/**
 * secure_erase.h — Secure Erase Protocols
 *
 * Best-effort secure erasure: zero-fill + fsync + unlink.
 */

#ifndef SDB_SECURE_ERASE_H
#define SDB_SECURE_ERASE_H

/**
 * Number of overwrite passes. Override at compile time with
 * -DSDB_ERASE_PASSES=3 for DOD-standard multi-pass.
 */
#ifndef SDB_ERASE_PASSES
#define SDB_ERASE_PASSES 1
#endif

/**
 * Securely erase a file.
 *
 * For each pass:
 *   1. Open the file.
 *   2. Overwrite entire contents with zero bytes.
 *   3. fsync() to flush to disk.
 * Then unlink the file.
 *
 * @param path  Absolute or relative path to the file.
 * @return 0 on success, -1 on failure (errno set).
 */
int sdb_secure_erase(const char *path);

#endif /* SDB_SECURE_ERASE_H */
