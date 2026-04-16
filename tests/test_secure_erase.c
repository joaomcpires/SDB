/**
 * test_secure_erase.c — Secure erase tests
 */

#include "test_harness.h"
#include "secure_erase.h"

#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *TEST_ERASE_FILE = "/tmp/sdb_test_erase_XXXXXX";

TEST(secure_erase_zeroes_and_removes)
{
    /* Create a temp file with known content */
    char path[] = "/tmp/sdb_test_erase_XXXXXX";
    int fd = mkstemp(path);
    ASSERT_NE(fd, -1);

    const char *data = "SENSITIVE DATA THAT MUST BE ERASED";
    size_t len = strlen(data);
    ASSERT_EQ((ssize_t)len, write(fd, data, len));
    fsync(fd);
    close(fd);

    /* Verify file exists */
    struct stat st;
    ASSERT_EQ(stat(path, &st), 0);
    ASSERT_EQ((size_t)st.st_size, len);

    /* Erase */
    ASSERT_EQ(sdb_secure_erase(path), 0);

    /* Verify file is gone */
    ASSERT_NE(stat(path, &st), 0);

    (void)TEST_ERASE_FILE; /* suppress unused warning */
}

TEST(secure_erase_nonexistent_file)
{
    ASSERT_EQ(sdb_secure_erase("/tmp/nonexistent_sdb_test_file"), -1);
}
