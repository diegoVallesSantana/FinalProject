/**
 * \author {Diego Vallés}
 */
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/select.h>
#include <unistd.h>
#include "lib/tcpsock.h"
#include "config.h"
#include "sbuffer.h"
#include "connmgr.h"
#include "sensor_db.h"

// Static documentation: https://learn.microsoft.com/fr-fr/dotnet/csharp/language-reference/keywords/static
//Use of const to make variable unmodifyable: https://learn.microsoft.com/fr-fr/cpp/cpp/const-cpp?view=msvc-170
//Use of Select to implement time_out: https://www.ibm.com/docs/en/zos/2.5.0?topic=calls-select ; https://www.youtube.com/watch?v=Y6pFtgRdUts ; https://man7.org/linux/man-pages/man2/select.2.html
//Need two tiime outs: listening to socket and client inactivity
typedef struct {
    tcpsock_t *client;
    sbuffer_t *buffer;
    conn_state_t *state;
} client_handler_args_t;

static void conn_state_init(conn_state_t *state) {
    state->served = 0;
    state->active = 0;
    pthread_mutex_init(&state->mtx, NULL);
    pthread_cond_init(&state->condition, NULL);
}

static void conn_state_destroy(conn_state_t *state) {
    pthread_cond_destroy(&state->condition);
    pthread_mutex_destroy(&state->mtx);
}

static int wait_readable_with_timeout(tcpsock_t *sock, int timeout_sec)
{
    int fd = -1;
    if (tcp_get_sd(sock, &fd) != TCP_NO_ERROR || fd < 0) {return -1;}

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);

    struct timeval tv;
    tv.tv_sec = timeout_sec;
    tv.tv_usec = 0;

    int sel = select(fd + 1, &rfds, NULL, NULL, &tv);
    if (sel <= 0) {
        return sel;
    }
    if (!FD_ISSET(fd, &rfds)) {
        return -1;
    }
    return 1;
}

static void *client_handler(void *arg) {
    client_handler_args_t *clientInfo = (client_handler_args_t *)arg;
    sensor_data_t data;
    int bytes, result;
    int have_id = 0;
    sensor_id_t sensorid = 0;
    int timed_out = 0;

    do {
        int wr;
        wr = wait_readable_with_timeout(clientInfo->client, TIMEOUT);
        if (wr == 0) { timed_out = 1; break;}
        if (wr < 0)  { break;}
        bytes = sizeof(data.id);
        result = tcp_receive(clientInfo->client, (void *)&data.id, &bytes);
        if (result != TCP_NO_ERROR || bytes == 0) {break;}

        wr = wait_readable_with_timeout(clientInfo->client, TIMEOUT);
        if (wr == 0) { timed_out = 1; break;}
        if (wr < 0)  { break;}
        bytes = sizeof(data.value);
        result = tcp_receive(clientInfo->client, (void *)&data.value, &bytes);
        if (result != TCP_NO_ERROR || bytes == 0) {break;}

        wr = wait_readable_with_timeout(clientInfo->client, TIMEOUT);
        if (wr == 0) { timed_out = 1; break;}
        if (wr < 0)  { break;}
        bytes = sizeof(data.ts);
        result = tcp_receive(clientInfo->client, (void *)&data.ts, &bytes);
        if (result != TCP_NO_ERROR || bytes == 0) {break;}

        if (!have_id) {
            have_id = 1;
            sensorid = data.id;
            log_event("Sensor node %u has opened a new connection", (unsigned)sensorid);
        }

        if (sbuffer_insert(clientInfo->buffer, &data) != SBUFFER_SUCCESS) {
            fprintf(stderr, "sbuffer_insert failed\n");
            break;
        }
    } while (1);

    if (have_id) {
        if (timed_out) {
            log_event("Sensor node %u time out", (unsigned)sensorid);
        }
        log_event("Sensor node %u has closed the connection", (unsigned)sensorid);
    }

    tcp_close(&clientInfo->client);

    pthread_mutex_lock(&clientInfo->state->mtx);
    clientInfo->state->active--;
    clientInfo->state->served++;
    pthread_cond_broadcast(&clientInfo->state->condition);
    pthread_mutex_unlock(&clientInfo->state->mtx);

    free(clientInfo);
    return NULL;
}

static void *connmgr_main(void *arg) {
    connmgr_args_t const ConnInfo = *(connmgr_args_t *)arg;
    free(arg);

    tcpsock_t *server = NULL;

    conn_state_t state;
    conn_state_init(&state);

    if (tcp_passive_open(&server, ConnInfo.port) != TCP_NO_ERROR) {
        fprintf(stderr, "tcp_passive_open failed\n");
        sbuffer_close(ConnInfo.buffer);
        return NULL;
    }

    int listen_fd = -1;
    if (tcp_get_sd(server, &listen_fd) != TCP_NO_ERROR || listen_fd < 0) {
        fprintf(stderr, "tcp_get_sd failed\n");
        tcp_close(&server);
        sbuffer_close(ConnInfo.buffer);
        conn_state_destroy(&state);
        return NULL;
    }

    while (1) {
        pthread_mutex_lock(&state.mtx);
        int const done = (state.served >= ConnInfo.max_conn);
        pthread_mutex_unlock(&state.mtx);
        if (done) break;

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(listen_fd, &rfds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 200 * 1000; //200ms

        int sel = select(listen_fd + 1, &rfds, NULL, NULL, &tv);
        if (sel < 0) {
            fprintf(stderr, "select failed\n");
            break;
        }
        tcpsock_t *client = NULL;
        if (tcp_wait_for_connection(server, &client) != TCP_NO_ERROR) {
            fprintf(stderr, "tcp_wait_for_connection failed\n");
            break;
        }

        pthread_mutex_lock(&state.mtx);
        if (state.served >= ConnInfo.max_conn) {
            log_event("Connection refused: Max number of clients (%d) already served",ConnInfo.max_conn);
            pthread_mutex_unlock(&state.mtx);
            tcp_close(&client);
            continue;
        }
        state.active++;
        pthread_mutex_unlock(&state.mtx);


        client_handler_args_t *clientInfo = malloc(sizeof(*clientInfo));
        if (!clientInfo) {
            fprintf(stderr, "malloc failed\n");
            tcp_close(&client);

            pthread_mutex_lock(&state.mtx);
            state.active--;
            pthread_cond_broadcast(&state.condition);
            pthread_mutex_unlock(&state.mtx);
            continue;
        }

        clientInfo->client = client;
        clientInfo->buffer = ConnInfo.buffer;
        clientInfo->state = &state;

	pthread_t tid;
	int rc = pthread_create(&tid, NULL, client_handler, clientInfo);

        if (rc != 0) {
            fprintf(stderr, "pthread_create failed, closing client\n");
            tcp_close(&client);
            free(clientInfo);

            pthread_mutex_lock(&state.mtx);
            state.active--;
            pthread_cond_broadcast(&state.condition);
            pthread_mutex_unlock(&state.mtx);
            continue;
        }
        pthread_detach(tid);
    }
    tcp_close(&server);

    pthread_mutex_lock(&state.mtx);
    while (state.active > 0) {
        pthread_cond_wait(&state.condition, &state.mtx);
    }
    pthread_mutex_unlock(&state.mtx);

    sbuffer_close(ConnInfo.buffer);
    conn_state_destroy(&state);
    return NULL;
}

int connmgr_start(pthread_t *tid, const connmgr_args_t *args) {
    if (!tid || !args || !args->buffer) {return -1;}

    connmgr_args_t *heap_args = malloc(sizeof(*heap_args));
    if (!heap_args) {return -1;}

    *heap_args = *args;
    if (pthread_create(tid, NULL, connmgr_main, heap_args) != 0) {free(heap_args);return -1;}
    return 0;
}