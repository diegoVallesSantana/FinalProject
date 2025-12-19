/**
 * Step A sbuffer test harness (2 producers, 2 consumers).
 * Paste into main.c and compile with your project.
 *
 * Assumptions:
 *  - config.h defines sensor_data_t with fields:
 *      sensor_id_t id;
 *      sensor_value_t value;
 *      sensor_ts_t ts;
 *    (This is the usual course setup.)
 *  - sbuffer.h exposes:
 *      sbuffer_init, sbuffer_free, sbuffer_insert, sbuffer_remove, sbuffer_close
 *      sbuffer_reader_t with SBUFFER_READER_DM / SBUFFER_READER_SM
 *      return codes SBUFFER_SUCCESS / SBUFFER_NO_DATA
 *
 * If your sensor_data_t field names differ, adjust make_data().
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

#include "config.h"
#include "sbuffer.h"

#ifndef N_PRODUCERS
#define N_PRODUCERS 2
#endif

#ifndef N_CONSUMERS
#define N_CONSUMERS 2
#endif

#ifndef ITEMS_PER_PRODUCER
#define ITEMS_PER_PRODUCER 50000
#endif

/* Small jitter so threads interleave more */
static inline void tiny_jitter(unsigned i) {
    if ((i & 0x3FFu) == 0) { // every 1024 iterations
        struct timespec ts = {.tv_sec = 0, .tv_nsec = 1000000}; // 1ms
        nanosleep(&ts, NULL);
    }
}

/* ---- Adjust this if your sensor_data_t differs ---- */
static sensor_data_t make_data(uint32_t producer_id, uint32_t seq) {
    sensor_data_t d;

    /* Common course typedefs:
       sensor_id_t is integer-like
       sensor_value_t is double-like
       sensor_ts_t is time_t-like
    */
    d.id = (sensor_id_t)(1000 + producer_id);           // deterministic id per producer
    d.value = (sensor_value_t)(producer_id * 1e6 + seq); // encodes producer+seq
    d.ts = (sensor_ts_t)time(NULL);

    return d;
}

/* ---- Thread args ---- */
typedef struct {
    sbuffer_t *buf;
    uint32_t producer_id;
    uint32_t n_items;
} producer_arg_t;

typedef struct {
    sbuffer_t *buf;
    sbuffer_reader_t reader;
    uint64_t count;
    long double checksum;  // use wide type to reduce overflow risk
} consumer_arg_t;

/* ---- Producer thread ---- */
static void *producer_thread(void *argp) {
    producer_arg_t *arg = (producer_arg_t *)argp;

    for (uint32_t i = 0; i < arg->n_items; i++) {
        sensor_data_t d = make_data(arg->producer_id, i);

        if (sbuffer_insert(arg->buf, &d) != SBUFFER_SUCCESS) {
            fprintf(stderr, "Producer %u: sbuffer_insert failed at i=%u\n",
                    arg->producer_id, i);
            return (void *)1;
        }

        tiny_jitter(i);
    }

    return NULL;
}

/* ---- Consumer thread ---- */
static void *consumer_thread(void *argp) {
    consumer_arg_t *arg = (consumer_arg_t *)argp;

    while (1) {
        sensor_data_t d;
        int rc = sbuffer_remove(arg->buf, &d, arg->reader);

        if (rc == SBUFFER_SUCCESS) {
            arg->count++;

            /* checksum based on value; should match across consumers */
            arg->checksum += (long double)d.value;

        } else if (rc == SBUFFER_NO_DATA) {
            /* closed + drained for this reader */
            break;
        } else {
            fprintf(stderr, "Consumer %d: sbuffer_remove failure\n", (int)arg->reader);
            return (void *)1;
        }
    }

    return NULL;
}

int main(void) {
    sbuffer_t *buf = NULL;

    if (sbuffer_init(&buf) != SBUFFER_SUCCESS || buf == NULL) {
        fprintf(stderr, "sbuffer_init failed\n");
        return EXIT_FAILURE;
    }

    /* Create consumers first so they block correctly */
    pthread_t consumers[N_CONSUMERS];
    consumer_arg_t carg[N_CONSUMERS] = {
        {.buf = buf, .reader = SBUFFER_READER_DM, .count = 0, .checksum = 0},
        {.buf = buf, .reader = SBUFFER_READER_SM, .count = 0, .checksum = 0},
    };

    for (int i = 0; i < N_CONSUMERS; i++) {
        if (pthread_create(&consumers[i], NULL, consumer_thread, &carg[i]) != 0) {
            fprintf(stderr, "pthread_create consumer %d failed\n", i);
            sbuffer_free(&buf);
            return EXIT_FAILURE;
        }
    }

    /* Producers */
    pthread_t producers[N_PRODUCERS];
    producer_arg_t parg[N_PRODUCERS];

    for (uint32_t p = 0; p < N_PRODUCERS; p++) {
        parg[p].buf = buf;
        parg[p].producer_id = p;
        parg[p].n_items = ITEMS_PER_PRODUCER;

        if (pthread_create(&producers[p], NULL, producer_thread, &parg[p]) != 0) {
            fprintf(stderr, "pthread_create producer %u failed\n", p);
            sbuffer_close(buf);
            for (int i = 0; i < N_CONSUMERS; i++) pthread_join(consumers[i], NULL);
            sbuffer_free(&buf);
            return EXIT_FAILURE;
        }
    }

    /* Join producers */
    for (uint32_t p = 0; p < N_PRODUCERS; p++) {
        void *ret = NULL;
        pthread_join(producers[p], &ret);
        if (ret != NULL) {
            fprintf(stderr, "Producer %u returned error\n", p);
        }
    }

    /* Close buffer so consumers can finish once drained */
    if (sbuffer_close(buf) != SBUFFER_SUCCESS) {
        fprintf(stderr, "sbuffer_close failed\n");
    }

    /* Join consumers */
    for (int i = 0; i < N_CONSUMERS; i++) {
        void *ret = NULL;
        pthread_join(consumers[i], &ret);
        if (ret != NULL) {
            fprintf(stderr, "Consumer %d returned error\n", i);
        }
    }

    const uint64_t expected = (uint64_t)N_PRODUCERS * (uint64_t)ITEMS_PER_PRODUCER;

    printf("Expected items inserted: %llu\n", (unsigned long long)expected);
    printf("DM consumed: %llu, checksum: %.0Lf\n",
           (unsigned long long)carg[0].count, carg[0].checksum);
    printf("SM consumed: %llu, checksum: %.0Lf\n",
           (unsigned long long)carg[1].count, carg[1].checksum);

    int ok = 1;
    if (carg[0].count != expected) {
        fprintf(stderr, "ERROR: DM count mismatch\n");
        ok = 0;
    }
    if (carg[1].count != expected) {
        fprintf(stderr, "ERROR: SM count mismatch\n");
        ok = 0;
    }
    if (carg[0].checksum != carg[1].checksum) {
        fprintf(stderr, "ERROR: checksum mismatch (data not identical across consumers)\n");
        ok = 0;
    }

    if (sbuffer_free(&buf) != SBUFFER_SUCCESS || buf != NULL) {
        fprintf(stderr, "sbuffer_free failed\n");
        ok = 0;
    }

    if (!ok) {
        fprintf(stderr, "SBUFFER TEST FAILED\n");
        return EXIT_FAILURE;
    }

    printf("SBUFFER TEST PASSED\n");
    return EXIT_SUCCESS;
}
