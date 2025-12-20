/**
 * main.c — Step 4 test harness for your connection manager + SM + DM + sbuffer
 *
 * Goal:
 *  - Start connmgr in its own thread
 *  - Start two consumer threads (DM + SM simulators) that call sbuffer_remove()
 *  - Print every consumed measurement so you can verify:
 *      (a) connmgr receives data from multiple clients
 *      (b) every measurement is delivered to BOTH consumers exactly once
 *      (c) shutdown occurs cleanly after max_conn clients disconnect
 *
 * Usage:
 *   ./sensor_gateway <port> <max_conn>
 *
 * Example test:
 *   Terminal 1: ./sensor_gateway 1234 2
 *   Terminal 2: ./sensor_node 101 1 127.0.0.1 1234
 *   Terminal 3: ./sensor_node 202 1 127.0.0.1 1234
 *
 * Notes:
 *  - This is a test-only main.c focusing on step 4.
 *  - No logging process is integrated here.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "sbuffer.h"
#include "connmgr.h"
#include "sensor_db.h"
#include "datamgr.h"


typedef struct {
    sbuffer_t *buffer;
    const char *csv_filename;
} storagemgr_args_t;

static void *storagemgr_thread(void *arg) {
    storagemgr_args_t *sa = (storagemgr_args_t *)arg;

    // Create a NEW empty CSV at server start (assignment requirement)
    FILE *f = open_db((char *)sa->csv_filename, false);
    if (f == NULL) {
        fprintf(stderr, "[SM] open_db failed\n");
        return NULL;
    }

    sensor_data_t data;

    for (;;) {
        int rc = sbuffer_remove(sa->buffer, &data, SBUFFER_READER_SM);

        if (rc == SBUFFER_SUCCESS) {
            if (insert_sensor(f, data.id, data.value, data.ts) != 0) {
                fprintf(stderr, "[SM] insert_sensor failed (id=%u)\n", (unsigned)data.id);
                // Decide policy: continue is usually safest for test harness
            }
        } else if (rc == SBUFFER_NO_DATA) {
            // buffer closed + drained for SM
            break;
        } else {
            fprintf(stderr, "[SM] sbuffer_remove failed\n");
            break;
        }
    }

    if (close_db(f) != 0) {
        fprintf(stderr, "[SM] close_db failed\n");
    }

    return NULL;
}

static void print_help(const char *prog) {
    fprintf(stderr, "Usage: %s <port> <max_conn>\n", prog);
    fprintf(stderr, "Example: %s 1234 3\n", prog);
}

int main(int argc, char **argv) {
    if (argc != 3) {
        print_help(argv[0]);
        return EXIT_FAILURE;
    }

    char *end = NULL;
    long port_l = strtol(argv[1], &end, 10);
    if (*argv[1] == '\0' || (end && *end != '\0') || port_l <= 0 || port_l > 65535) {
        fprintf(stderr, "Invalid port: %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    end = NULL;
    long max_conn_l = strtol(argv[2], &end, 10);
    if (*argv[2] == '\0' || (end && *end != '\0') || max_conn_l <= 0 || max_conn_l > 1000000) {
        fprintf(stderr, "Invalid max_conn: %s\n", argv[2]);
        return EXIT_FAILURE;
    }

    int port = (int)port_l;
    int max_conn = (int)max_conn_l;

    sbuffer_t *buffer = NULL;
    if (sbuffer_init(&buffer) != SBUFFER_SUCCESS) {
        fprintf(stderr, "sbuffer_init failed\n");
        return EXIT_FAILURE;
    }

    // Start DM and SM
    pthread_t dm_tid;
    datamgr_args_t dm_args = {
        .buffer = buffer,
        .map_filename = "room_sensor.map"
    };

    if (pthread_create(&dm_tid, NULL, datamgr_run, &dm_args) != 0) {
        fprintf(stderr, "pthread_create(datamgr) failed: %s\n", strerror(errno));
        sbuffer_close(buffer);
        sbuffer_free(&buffer);
        return EXIT_FAILURE;
    }


    pthread_t sm_tid;
    storagemgr_args_t sm_args = {.buffer = buffer, .csv_filename = "data.csv"};
    if (pthread_create(&sm_tid, NULL, storagemgr_thread, &sm_args) != 0) {
        fprintf(stderr, "pthread_create(SM) failed: %s\n", strerror(errno));
        sbuffer_close(buffer);
        pthread_join(dm_tid, NULL);
        sbuffer_free(&buffer);
        return EXIT_FAILURE;
    }

    // Start connection manager
    pthread_t conn_tid;
    connmgr_args_t conn_args = {
        .port = port,
        .max_conn = max_conn,
        .buffer = buffer
    };

    if (connmgr_start(&conn_tid, &conn_args) != 0) {
        fprintf(stderr, "connmgr_start failed\n");
        sbuffer_close(buffer);
        pthread_join(dm_tid, NULL);
        pthread_join(sm_tid, NULL);
        sbuffer_free(&buffer);
        return EXIT_FAILURE;
    }

    // Wait for connmgr to stop (it should stop after max_conn clients disconnect)
    pthread_join(conn_tid, NULL);

    // At this point connmgr closes the buffer in your current implementation.
    // Consumers will drain and exit.
    pthread_join(dm_tid, NULL);
    pthread_join(sm_tid, NULL);

    if (sbuffer_free(&buffer) != SBUFFER_SUCCESS) {
        fprintf(stderr, "sbuffer_free failed\n");
        return EXIT_FAILURE;
    }

    printf("Step 4 test completed (connmgr + storagemgr + datamgr + sbuffer).\n");
    return EXIT_SUCCESS;
}
