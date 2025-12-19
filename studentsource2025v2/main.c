/**
 * Step B test harness: Connection Manager + sbuffer + two dummy consumers (DM + SM).
 *
 * Build goal:
 *  - Start sbuffer
 *  - Start two consumer threads that read from sbuffer (DM/SM stand-ins)
 *  - Start connection manager thread that accepts sensor_node TCP clients and inserts into sbuffer
 *  - After max_conn clients have disconnected, connmgr closes sbuffer
 *  - Consumers exit cleanly (SBUFFER_NO_DATA), main prints stats and frees resources
 *
 * Usage:
 *   ./server_test <port> <max_conn>
 *
 * Example:
 *   Terminal 1: ./server_test 5678 3
 *   Terminal 2: ./sensor_node 1 1 127.0.0.1 5678
 *   Terminal 3: ./sensor_node 2 2 127.0.0.1 5678
 *   Terminal 4: ./sensor_node 3 1 127.0.0.1 5678
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <inttypes.h>
#include <time.h>

#include "config.h"
#include "sbuffer.h"
#include "connmgr.h"

typedef struct {
    sbuffer_t *buf;
    sbuffer_reader_t reader;
    uint64_t count;
    long double checksum;
    sensor_data_t last;
    int has_last;
} consumer_arg_t;

static void *consumer_thread(void *argp) {
    consumer_arg_t *arg = (consumer_arg_t *)argp;

    while (1) {
        sensor_data_t d;
        int rc = sbuffer_remove(arg->buf, &d, arg->reader);

        if (rc == SBUFFER_SUCCESS) {
            arg->count++;
            arg->checksum += (long double)d.value;
            arg->last = d;
            arg->has_last = 1;

            /* Optional progress print (kept light) */
            if ((arg->count % 5000ULL) == 0ULL) {
                printf("[reader=%d] consumed=%" PRIu64 " (latest id=%u)\n",
                       (int)arg->reader, arg->count, (unsigned)d.id);
                fflush(stdout);
            }

        } else if (rc == SBUFFER_NO_DATA) {
            /* closed + drained for this reader */
            break;
        } else {
            fprintf(stderr, "[reader=%d] sbuffer_remove failed\n", (int)arg->reader);
            return (void *)1;
        }
    }

    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <port> <max_conn>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int port = atoi(argv[1]);
    int max_conn = atoi(argv[2]);
    if (port <= 0 || max_conn <= 0) {
        fprintf(stderr, "ERROR: port and max_conn must be positive integers\n");
        return EXIT_FAILURE;
    }

    sbuffer_t *buf = NULL;
    if (sbuffer_init(&buf) != SBUFFER_SUCCESS || buf == NULL) {
        fprintf(stderr, "sbuffer_init failed\n");
        return EXIT_FAILURE;
    }

    /* Start dummy consumers (DM + SM stand-ins) */
    pthread_t dm_tid, sm_tid;
    consumer_arg_t dm = {.buf = buf, .reader = SBUFFER_READER_DM, .count = 0, .checksum = 0, .has_last = 0};
    consumer_arg_t sm = {.buf = buf, .reader = SBUFFER_READER_SM, .count = 0, .checksum = 0, .has_last = 0};

    if (pthread_create(&dm_tid, NULL, consumer_thread, &dm) != 0) {
        fprintf(stderr, "pthread_create DM failed\n");
        sbuffer_free(&buf);
        return EXIT_FAILURE;
    }
    if (pthread_create(&sm_tid, NULL, consumer_thread, &sm) != 0) {
        fprintf(stderr, "pthread_create SM failed\n");
        sbuffer_close(buf);
        pthread_join(dm_tid, NULL);
        sbuffer_free(&buf);
        return EXIT_FAILURE;
    }

    /* Start connection manager */
    pthread_t conn_tid;
    connmgr_args_t cargs = {.port = port, .max_conn = max_conn, .buffer = buf};

    if (connmgr_start(&conn_tid, &cargs) != 0) {
        fprintf(stderr, "connmgr_start failed\n");
        sbuffer_close(buf);
        pthread_join(dm_tid, NULL);
        pthread_join(sm_tid, NULL);
        sbuffer_free(&buf);
        return EXIT_FAILURE;
    }

    /* Wait for connmgr (it should close sbuffer when done) */
    pthread_join(conn_tid, NULL);

    /* Now consumers should finish (buffer closed + drained) */
    pthread_join(dm_tid, NULL);
    pthread_join(sm_tid, NULL);

    printf("\n=== STEP B RESULT ===\n");
    printf("DM consumed: %" PRIu64 ", checksum: %.0Lf\n", dm.count, dm.checksum);
    printf("SM consumed: %" PRIu64 ", checksum: %.0Lf\n", sm.count, sm.checksum);

    if (dm.has_last) {
        printf("DM last: id=%u value=%.2f ts=%ld\n",
               (unsigned)dm.last.id, (double)dm.last.value, (long)dm.last.ts);
    }
    if (sm.has_last) {
        printf("SM last: id=%u value=%.2f ts=%ld\n",
               (unsigned)sm.last.id, (double)sm.last.value, (long)sm.last.ts);
    }

    int ok = 1;

    /* For Step B, we at least require fan-out consistency */
    if (dm.count != sm.count) {
        fprintf(stderr, "ERROR: DM/SM count mismatch (fan-out broken)\n");
        ok = 0;
    }
    if (dm.checksum != sm.checksum) {
        fprintf(stderr, "ERROR: DM/SM checksum mismatch (fan-out broken)\n");
        ok = 0;
    }

    if (sbuffer_free(&buf) != SBUFFER_SUCCESS || buf != NULL) {
        fprintf(stderr, "ERROR: sbuffer_free failed\n");
        ok = 0;
    }

    if (!ok) {
        fprintf(stderr, "STEP B TEST FAILED\n");
        return EXIT_FAILURE;
    }

    printf("STEP B TEST PASSED\n");
    return EXIT_SUCCESS;
}
