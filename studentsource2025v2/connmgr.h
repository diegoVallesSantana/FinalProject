/**
* \author {Diego Vallés}
 */
#ifndef CONNMGR_H
#define CONNMGR_H

#include <stdint.h>
#include "sbuffer.h"
#include <pthread.h>

typedef struct {
    int served;
    int active;
    pthread_mutex_t mtx;
    pthread_cond_t  condition;
} conn_state_t;

typedef struct {
    int port;
    int max_conn;
    sbuffer_t *buffer;
} connmgr_args_t;

int connmgr_start(pthread_t *tid, const connmgr_args_t *args);

#endif