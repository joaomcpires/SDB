/**
 * secure_erase.c — Secure Erase Implementation
 *
 * Zero-fill overwrite + fsync + unlink. Uses pwrite() for aligned I/O.
 */

#include "secure_erase.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/** Block size for zero-fill writes. */
#define ERASE_BLOCK_SIZE 4096

int sdb_secure_erase(const char *path)
{
    if (!path) {
        errno = EINVAL;
        return -1;
    }

    /* Get file size */
    struct stat st;
    if (stat(path, &st) != 0)
        return -1;

    off_t file_size = st.st_size;

    /* Zero-fill buffer */
    char zeros[ERASE_BLOCK_SIZE];
    memset(zeros, 0, sizeof(zeros));

    for (int pass = 0; pass < SDB_ERASE_PASSES; pass++) {
        int fd = open(path, O_WRONLY);
        if (fd < 0)
            return -1;

        off_t offset = 0;
        while (offset < file_size) {
            size_t chunk = (size_t)(file_size - offset);
            if (chunk > ERASE_BLOCK_SIZE)
                chunk = ERASE_BLOCK_SIZE;

            ssize_t written = pwrite(fd, zeros, chunk, offset);
            if (written < 0) {
                int saved_errno = errno;
                close(fd);
                errno = saved_errno;
                return -1;
            }
            offset += written;
        }

        if (fsync(fd) != 0) {
            int saved_errno = errno;
            close(fd);
            errno = saved_errno;
            return -1;
        }

        close(fd);
    }

    /* Remove the file */
    if (unlink(path) != 0)
        return -1;

    return 0;
}
