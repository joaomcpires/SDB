/**
 * main.c — SDB Command-Line Interface
 *
 * Usage:
 *   sdb [OPTIONS] COMMAND [ARGS...]
 *
 * Commands:
 *   pray <data>         Insert data, print assigned UUID.
 *   observe <id>        Attempt retrieval (50% collapse risk).
 *   mutate <id> <data>  Attempt mutation (~25% success rate).
 *   forget <id>         Delete with certainty.
 *   count               Destructive record count.
 *
 * Options:
 *   --data-dir <path>         Storage directory (default: ./sdb_data)
 *   --deterministic <seed>    Use deterministic entropy for testing
 *   --help                    Show help message
 */

#include "sdb.h"
#include "logging.h"

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Exit codes */
#define EXIT_SUCCESS_CODE      0
#define EXIT_COLLAPSED         1
#define EXIT_NOT_FOUND         2
#define EXIT_SYSTEM_ERROR      3

static void print_usage(const char *prog)
{
    fprintf(stderr,
        "Schrödinger's Database (SDB)\n"
        "\"In SDB, your data is safe as long as you don't care to see it.\"\n"
        "\n"
        "Usage: %s [OPTIONS] COMMAND [ARGS...]\n"
        "\n"
        "Commands:\n"
        "  pray <data>         Commit data to the void. Prints UUID.\n"
        "  observe <id>        Attempt retrieval. 50%% collapse risk.\n"
        "  mutate <id> <data>  Attempt mutation. ~25%% success rate.\n"
        "  forget <id>         Delete with absolute certainty.\n"
        "  count               Destructive count of all records.\n"
        "  track               Safely list QDI (UUIDs) of all records.\n"
        "\n"
        "Options:\n"
        "  --data-dir <path>         Storage directory (default: ./sdb_data)\n"
        "  --deterministic <seed>    Use deterministic entropy (for testing)\n"
        "  --help                    Show this message\n",
        prog);
}

static struct option long_options[] = {
    { "data-dir",       required_argument, NULL, 'd' },
    { "deterministic",  required_argument, NULL, 'D' },
    { "help",           no_argument,       NULL, 'h' },
    { NULL, 0, NULL, 0 }
};

static void track_print_cb(const char *uuid_str, void *user_data)
{
    (void)user_data;
    printf("%s\n", uuid_str);
}

int main(int argc, char *argv[])
{
    const char *data_dir = "./sdb_data";
    uint64_t det_seed = 0;
    int opt;

    while ((opt = getopt_long(argc, argv, "d:D:h", long_options, NULL)) != -1) {
        switch (opt) {
        case 'd':
            data_dir = optarg;
            break;
        case 'D':
            det_seed = strtoull(optarg, NULL, 10);
            break;
        case 'h':
            print_usage(argv[0]);
            return EXIT_SUCCESS_CODE;
        default:
            print_usage(argv[0]);
            return EXIT_SYSTEM_ERROR;
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "Error: No command specified.\n\n");
        print_usage(argv[0]);
        return EXIT_SYSTEM_ERROR;
    }

    const char *command = argv[optind];

    /* Create entropy source */
    sdb_entropy_source_t *entropy;
    if (det_seed != 0) {
        entropy = sdb_entropy_deterministic_create(det_seed);
        if (!entropy) {
            fprintf(stderr, "Error: Failed to create deterministic "
                    "entropy source.\n");
            return EXIT_SYSTEM_ERROR;
        }
    } else {
        entropy = sdb_entropy_hardware_create();
        if (!entropy) {
            fprintf(stderr, "Error: Failed to open /dev/random.\n");
            return EXIT_SYSTEM_ERROR;
        }
    }

    /* Open database */
    sdb_t *db = sdb_open(data_dir, entropy);
    if (!db) {
        fprintf(stderr, "Error: Failed to open database at '%s'.\n",
                data_dir);
        sdb_entropy_destroy(entropy);
        return EXIT_SYSTEM_ERROR;
    }

    int exit_code = EXIT_SUCCESS_CODE;

    /* ── PRAY ──────────────────────────────────────────────────────── */
    if (strcmp(command, "pray") == 0) {
        if (optind + 1 >= argc) {
            fprintf(stderr, "Usage: %s pray <data>\n", argv[0]);
            exit_code = EXIT_SYSTEM_ERROR;
            goto cleanup;
        }

        const char *data = argv[optind + 1];
        sdb_uuid_t uuid;

        if (sdb_pray(db, (const uint8_t *)data, strlen(data), &uuid) != 0) {
            fprintf(stderr, "Error: PRAY failed.\n");
            exit_code = EXIT_SYSTEM_ERROR;
            goto cleanup;
        }

        char uuid_str[33];
        sdb_uuid_to_string(&uuid, uuid_str, sizeof(uuid_str));
        printf("%s\n", uuid_str);
    }

    /* ── OBSERVE ───────────────────────────────────────────────────── */
    else if (strcmp(command, "observe") == 0) {
        if (optind + 1 >= argc) {
            fprintf(stderr, "Usage: %s observe <id>\n", argv[0]);
            exit_code = EXIT_SYSTEM_ERROR;
            goto cleanup;
        }

        sdb_uuid_t uuid;
        if (sdb_uuid_from_string(argv[optind + 1], &uuid) != 0) {
            fprintf(stderr, "Error: Invalid UUID format.\n");
            exit_code = EXIT_SYSTEM_ERROR;
            goto cleanup;
        }

        size_t buf_size = 1024 * 1024; /* 1 MiB */
        uint8_t *buf = malloc(buf_size);
        if (!buf) {
            fprintf(stderr, "Error: Memory allocation failed.\n");
            exit_code = EXIT_SYSTEM_ERROR;
            goto cleanup;
        }

        size_t payload_len = 0;
        sdb_observe_result_t res = sdb_observe(db, &uuid, buf, buf_size,
                                               &payload_len);

        switch (res) {
        case SDB_OBSERVE_OK:
            fwrite(buf, 1, payload_len, stdout);
            printf("\n");
            break;
        case SDB_OBSERVE_COLLAPSED:
            fprintf(stderr, "Status 410: Record underwent permanent wave "
                    "function collapse during retrieval.\n");
            exit_code = EXIT_COLLAPSED;
            break;
        case SDB_OBSERVE_NOT_FOUND:
            fprintf(stderr, "Status 404: Record does not exist in the "
                    "current index state.\n");
            exit_code = EXIT_NOT_FOUND;
            break;
        }

        free(buf);
    }

    /* ── MUTATE ────────────────────────────────────────────────────── */
    else if (strcmp(command, "mutate") == 0) {
        if (optind + 2 >= argc) {
            fprintf(stderr, "Usage: %s mutate <id> <data>\n", argv[0]);
            exit_code = EXIT_SYSTEM_ERROR;
            goto cleanup;
        }

        sdb_uuid_t uuid;
        if (sdb_uuid_from_string(argv[optind + 1], &uuid) != 0) {
            fprintf(stderr, "Error: Invalid UUID format.\n");
            exit_code = EXIT_SYSTEM_ERROR;
            goto cleanup;
        }

        const char *new_data = argv[optind + 2];
        sdb_uuid_t new_uuid;
        sdb_mutate_result_t res = sdb_mutate(db, &uuid,
                                             (const uint8_t *)new_data,
                                             strlen(new_data), &new_uuid);

        switch (res) {
        case SDB_MUTATE_OK: {
            char uuid_str[33];
            sdb_uuid_to_string(&new_uuid, uuid_str, sizeof(uuid_str));
            printf("Mutation successful. New record: %s\n", uuid_str);
            break;
        }
        case SDB_MUTATE_ORIGINAL_COLLAPSED:
            fprintf(stderr, "Status 410: Original record collapsed during "
                    "observation phase.\n");
            exit_code = EXIT_COLLAPSED;
            break;
        case SDB_MUTATE_WRITE_ABORTED:
            fprintf(stderr, "Status 410: Write gate collapsed. Original "
                    "erased, new data discarded.\n");
            exit_code = EXIT_COLLAPSED;
            break;
        case SDB_MUTATE_NOT_FOUND:
            fprintf(stderr, "Status 404: Record not found.\n");
            exit_code = EXIT_NOT_FOUND;
            break;
        }
    }

    /* ── FORGET ────────────────────────────────────────────────────── */
    else if (strcmp(command, "forget") == 0) {
        if (optind + 1 >= argc) {
            fprintf(stderr, "Usage: %s forget <id>\n", argv[0]);
            exit_code = EXIT_SYSTEM_ERROR;
            goto cleanup;
        }

        sdb_uuid_t uuid;
        if (sdb_uuid_from_string(argv[optind + 1], &uuid) != 0) {
            fprintf(stderr, "Error: Invalid UUID format.\n");
            exit_code = EXIT_SYSTEM_ERROR;
            goto cleanup;
        }

        if (sdb_forget(db, &uuid) != 0) {
            fprintf(stderr, "Status 404: Record not found.\n");
            exit_code = EXIT_NOT_FOUND;
        } else {
            printf("Record erased with certainty.\n");
        }
    }

    /* ── COUNT ─────────────────────────────────────────────────────── */
    else if (strcmp(command, "count") == 0) {
        size_t survived = 0, collapsed = 0;

        if (sdb_count(db, &survived, &collapsed) != 0) {
            fprintf(stderr, "Error: COUNT operation failed.\n");
            exit_code = EXIT_SYSTEM_ERROR;
            goto cleanup;
        }

        printf("Surviving: %zu\nCollapsed: %zu\n", survived, collapsed);
    }

    /* ── TRACK ─────────────────────────────────────────────────────── */
    else if (strcmp(command, "track") == 0) {
        if (sdb_track(db, track_print_cb, NULL) != 0) {
            fprintf(stderr, "Error: TRACK operation failed.\n");
            exit_code = EXIT_SYSTEM_ERROR;
            goto cleanup;
        }
    }

    /* ── Unknown ───────────────────────────────────────────────────── */
    else {
        fprintf(stderr, "Error: Unknown command '%s'.\n\n", command);
        print_usage(argv[0]);
        exit_code = EXIT_SYSTEM_ERROR;
    }

cleanup:
    sdb_close(db);
    return exit_code;
}
