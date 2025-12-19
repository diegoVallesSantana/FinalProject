/**
 * \author {Diego Vallés}
 */

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <pthread.h>
#include "config.h"
#include "sbuffer.h"
#include "connmgr.h"
#include "lib/tcpsock.h"

//Use of const: https://learn.microsoft.com/fr-fr/cpp/cpp/const-cpp?view=msvc-170

typedef struct {
    tcpsock_t *client;
    sbuffer_t *buffer;
    int *served_counter;
    pthread_mutex_t *counter_mutex;
} client_handler_args_t;

static void *client_handler(void *arg) {
    client_handler_args_t *clientInfo = (client_handler_args_t *)arg;
    sensor_data_t data;
    int bytes, result;

    do {
        bytes = sizeof(data.id);
        result = tcp_receive(clientInfo->client, (void *)&data.id, &bytes);
        if (result != TCP_NO_ERROR || bytes == 0) break;

        bytes = sizeof(data.value);
        result = tcp_receive(clientInfo->client, (void *)&data.value, &bytes);
        if (result != TCP_NO_ERROR || bytes == 0) break;

        bytes = sizeof(data.ts);
        result = tcp_receive(clientInfo->client, (void *)&data.ts, &bytes);
        if (result != TCP_NO_ERROR || bytes == 0) break;

        // Insert into shared buffer (fan-out to DM + SM will happen via sbuffer)
        if (sbuffer_insert(clientInfo->buffer, &data) != SBUFFER_SUCCESS) {
            fprintf(stderr, "sbuffer_insert failed\n");
            break;
        }
    } while (1);

    tcp_close(&clientInfo->client);

    pthread_mutex_lock(clientInfo->counter_mutex);
    (*clientInfo->served_counter)++;
    pthread_mutex_unlock(clientInfo->counter_mutex);

    free(clientInfo);
    return NULL;
}

static void *connmgr_main(void *arg) {
    connmgr_args_t const ConnInfo = *(connmgr_args_t *)arg;
    tcpsock_t *server = NULL, *client = NULL;

    int served = 0;
    pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;

    if (tcp_passive_open(&server, ConnInfo.port) != TCP_NO_ERROR) {
        fprintf(stderr, "tcp_passive_open failed\n");
        sbuffer_close(ConnInfo.buffer);
        return NULL;
    }

    while (1) {
        pthread_mutex_lock(&counter_mutex);
        int const done = (served >= ConnInfo.max_conn);
        pthread_mutex_unlock(&counter_mutex);
        if (done) break;

        if (tcp_wait_for_connection(server, &client) != TCP_NO_ERROR) {
            fprintf(stderr, "tcp_wait_for_connection failed\n");
            break;
        }


        client_handler_args_t *clientInfo = malloc(sizeof(*clientInfo));
        if (!clientInfo) {
            fprintf(stderr, "malloc failed\n");
            tcp_close(&client);
            continue;
        }

        clientInfo->client = client;
        clientInfo->buffer = ConnInfo.buffer;
        clientInfo->served_counter = &served;
        clientInfo->counter_mutex = &counter_mutex;
	pthread_t tid;
	int const rc = pthread_create(&tid, NULL, client_handler, clientInfo);

        if (rc != 0) {
            fprintf(stderr, "pthread_create failed, closing client\n");
            tcp_close(&client);
            free(clientInfo);
        } else {
            pthread_detach(tid);
        }
    }

    tcp_close(&server);

    // Critical for Step B and final project: unblock consumers and let them terminate
    sbuffer_close(ConnInfo.buffer);
    return NULL;
}

int connmgr_start(pthread_t *tid, const connmgr_args_t *args) {
    if (!tid || !args || !args->buffer) return -1;
    if (pthread_create(tid, NULL, connmgr_main, (void *)args) != 0) return -1;
    return 0;
}