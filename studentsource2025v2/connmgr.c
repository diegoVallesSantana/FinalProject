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
#include "sensor_db.h"


//Use of const: https://learn.microsoft.com/fr-fr/cpp/cpp/const-cpp?view=msvc-170

typedef struct {
    tcpsock_t *client;
    sbuffer_t *buffer;
    conn_state_t *st;
} client_handler_args_t;

static void conn_state_init(conn_state_t *st) {
    st->served = 0;
    st->active = 0;
    pthread_mutex_init(&st->mtx, NULL);
    pthread_cond_init(&st->cv, NULL);
}

static void conn_state_destroy(conn_state_t *st) {
    pthread_cond_destroy(&st->cv);
    pthread_mutex_destroy(&st->mtx);
}

static void *client_handler(void *arg) {
    client_handler_args_t *clientInfo = (client_handler_args_t *)arg;
    sensor_data_t data;
    int bytes, result;
    int have_id = 0;
    sensor_id_t sid = 0;

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

        if (!have_id) {
            have_id = 1;
            sid = data.id;
            log_event("Sensor node %u has opened a new connection", (unsigned)sid);
        }

        // Insert into shared buffer (fan-out to DM + SM will happen via sbuffer)
        if (sbuffer_insert(clientInfo->buffer, &data) != SBUFFER_SUCCESS) {
            fprintf(stderr, "sbuffer_insert failed\n");
            break;
        }
    } while (1);

    if (have_id) {
        log_event("Sensor node %u has closed the connection", (unsigned)sid);
    }
    tcp_close(&clientInfo->client);

    pthread_mutex_lock(&clientInfo->st->mtx);
    clientInfo->st->active--;
    clientInfo->st->served++;
    pthread_cond_broadcast(&clientInfo->st->cv);
    pthread_mutex_unlock(&clientInfo->st->mtx);

    free(clientInfo);
    return NULL;
}

static void *connmgr_main(void *arg) {
    connmgr_args_t const ConnInfo = *(connmgr_args_t *)arg;
    free(arg);

    tcpsock_t *server = NULL;

    conn_state_t st;
    conn_state_init(&st);

    if (tcp_passive_open(&server, ConnInfo.port) != TCP_NO_ERROR) {
        fprintf(stderr, "tcp_passive_open failed\n");
        sbuffer_close(ConnInfo.buffer);
        return NULL;
    }

    while (1) {
        pthread_mutex_lock(&st.mtx);
        int const done = (st.served >= ConnInfo.max_conn);
        pthread_mutex_unlock(&st.mtx);
        if (done) break;

        tcpsock_t *client = NULL;
        if (tcp_wait_for_connection(server, &client) != TCP_NO_ERROR) {
            fprintf(stderr, "tcp_wait_for_connection failed\n");
            break;
        }

        /* If we already reached the target (race), close immediately */
        pthread_mutex_lock(&st.mtx);
        if (st.served >= ConnInfo.max_conn) {
            pthread_mutex_unlock(&st.mtx);
            tcp_close(&client);
            break;
        }
        st.active++; /* handler will now exist */
        pthread_mutex_unlock(&st.mtx);

        client_handler_args_t *clientInfo = malloc(sizeof(*clientInfo));
        if (!clientInfo) {
            fprintf(stderr, "malloc failed\n");
            tcp_close(&client);

            pthread_mutex_lock(&st.mtx);
            st.active--;
            pthread_cond_broadcast(&st.cv);
            pthread_mutex_unlock(&st.mtx);
            continue;
        }

        clientInfo->client = client;
        clientInfo->buffer = ConnInfo.buffer;
        clientInfo->st = &st;
	pthread_t tid;
	int rc = pthread_create(&tid, NULL, client_handler, clientInfo);

        if (rc != 0) {
            fprintf(stderr, "pthread_create failed, closing client\n");
            tcp_close(&client);
            free(clientInfo);

            pthread_mutex_lock(&st.mtx);
            st.active--;
            pthread_cond_broadcast(&st.cv);
            pthread_mutex_unlock(&st.mtx);
            continue;
        }
            pthread_detach(tid);

    }

    tcp_close(&server);

    pthread_mutex_lock(&st.mtx);
    while (st.active > 0) {
        pthread_cond_wait(&st.cv, &st.mtx);
    }
    pthread_mutex_unlock(&st.mtx);

    sbuffer_close(ConnInfo.buffer);
    conn_state_destroy(&st);
    return NULL;
}

int connmgr_start(pthread_t *tid, const connmgr_args_t *args) {
    if (!tid || !args || !args->buffer) return -1;

    connmgr_args_t *heap_args = malloc(sizeof(*heap_args));
    if (!heap_args) return -1;
    *heap_args = *args;

    if (pthread_create(tid, NULL, connmgr_main, heap_args) != 0) {
        free(heap_args);
        return -1;
    }
    return 0;
}