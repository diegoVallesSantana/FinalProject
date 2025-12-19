/**
* \author {Diego Vallés}
 */
#ifndef CONNMGR_H
#define CONNMGR_H

#include <stdint.h>
#include "sbuffer.h"
#include <pthread.h>



typedef struct {
    int port;
    int max_conn;
    sbuffer_t *buffer;
} connmgr_args_t;

/**
 * Starts the connection manager in its own thread.
 * Returns 0 on success, -1 on failure.
 */
int connmgr_start(pthread_t *tid, const connmgr_args_t *args);

#endif
