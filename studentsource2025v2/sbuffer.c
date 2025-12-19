/**
 * \author {Bert Lagaisse + Diego Vallés}
 */

#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <stdbool.h>
#include "sbuffer.h"

/**
 * basic node for the buffer, these nodes are linked together to create the buffer
 */
typedef struct sbuffer_node {
    struct sbuffer_node *next;  /**< a pointer to the next node*/
    sensor_data_t data;/**< a structure containing the data */
    bool read_by_dm;//condition: both dm and sm should have read a sensor value for it to be removed
    bool read_by_sm;
} sbuffer_node_t;

/**
 * a structure to keep track of the buffer
 */
struct sbuffer {
    sbuffer_node_t *head;       /**< a pointer to the first node in the buffer */
    sbuffer_node_t *tail;       /**< a pointer to the last node in the buffer */
    pthread_mutex_t mutex;
    bool closed; // condition: threads wait for sensor values while the buffer is not closed
    pthread_cond_t cond_nempty;
};

// Helper check functions:
//Literature Inline to make check faster and avoid slowing the program: https://learn.microsoft.com/fr-fr/cpp/cpp/inline-functions-cpp?view=msvc-170

// Check if already read by a given reader
static inline bool node_read_by(const sbuffer_node_t *n, sbuffer_reader_t reader) {
    return (reader == SBUFFER_READER_DM) ? n->read_by_dm : n->read_by_sm;
}
//check if node read by DM */
static inline void node_mark_read(sbuffer_node_t *n, sbuffer_reader_t reader) {
    if (reader == SBUFFER_READER_DM) n->read_by_dm = true;
    else n->read_by_sm = true;
}
// node read by both dm and sm and ready to be remove */
static inline bool node_fully_read(const sbuffer_node_t *n) {
    return n->read_by_dm && n->read_by_sm;
}
//Garbage collection removes fully read nodes
//https://learn.microsoft.com/fr-fr/dotnet/standard/garbage-collection/fundamentals
static void garbageCollectionFullyRead(sbuffer_t *buffer) {
    while (buffer->head && node_fully_read(buffer->head)) {
        sbuffer_node_t *dummy = buffer->head;
        buffer->head = buffer->head->next;
        free(dummy);
    }
    if (buffer->head == NULL) {
        buffer->tail = NULL;
    }
}
//Finds the oldest node that is NOT yet read by this reader
static sbuffer_node_t* find_oldest_unread(sbuffer_t *buffer, sbuffer_reader_t reader) {
    sbuffer_node_t *dummy = buffer->head;
    while (dummy) {
        if (!node_read_by(dummy, reader)) return dummy;
        dummy = dummy->next;
    }
    return NULL;
}

int sbuffer_init(sbuffer_t **buffer) {
    if (buffer == NULL) return SBUFFER_FAILURE;

    *buffer = malloc(sizeof(sbuffer_t));
    if (*buffer == NULL) return SBUFFER_FAILURE;
    (*buffer)->head = NULL;
    (*buffer)->tail = NULL;
    (*buffer)->closed = false;

	if (pthread_mutex_init(&(*buffer)->mutex, NULL) != 0) {free(*buffer);*buffer = NULL;return SBUFFER_FAILURE;}
    if (pthread_cond_init(&(*buffer)->cond_nempty, NULL) != 0) {pthread_mutex_destroy(&(*buffer)->mutex);free(*buffer);*buffer = NULL;return SBUFFER_FAILURE;}

    return SBUFFER_SUCCESS;
}

int sbuffer_free(sbuffer_t **buffer) {
    sbuffer_node_t *dummy;
    if ((buffer == NULL) || (*buffer == NULL)) {return SBUFFER_FAILURE;}

	pthread_mutex_lock(&(*buffer)->mutex); // locks for critical operation

    dummy = (*buffer)->head;
    while (dummy) {
        sbuffer_node_t *next = dummy->next;
        free(dummy);
        dummy = next;
    }
    (*buffer)->head = NULL;
    (*buffer)->tail = NULL;
    pthread_mutex_unlock(&(*buffer)->mutex);

    pthread_mutex_destroy(&(*buffer)->mutex);
    pthread_cond_destroy(&(*buffer)->cond_nempty);
    free(*buffer);
    *buffer = NULL;
    return SBUFFER_SUCCESS;
}

int sbuffer_remove(sbuffer_t *buffer, sensor_data_t *data, sbuffer_reader_t reader) {
    if (buffer == NULL || data == NULL) return SBUFFER_FAILURE;

    pthread_mutex_lock(&buffer->mutex);

    while (true) {

        /* First: collect fully-read nodes to keep head clean */
        garbageCollectionFullyRead(buffer);

        /* Find oldest unread for this reader */
        sbuffer_node_t *dummy = find_oldest_unread(buffer, reader);
        if (dummy != NULL) {
            *data = dummy->data;
            node_mark_read(dummy, reader);

            /* If this read makes head fully read, GC might free it now */
            garbageCollectionFullyRead(buffer);

            pthread_mutex_unlock(&buffer->mutex);
            return SBUFFER_SUCCESS;
        }

        /* No unread data for this reader */
        if (buffer->closed) {
            pthread_mutex_unlock(&buffer->mutex);
            return SBUFFER_NO_DATA;  // drained for this reader
        }

        /* Wait for inserts or close */
        pthread_cond_wait(&buffer->cond_nempty, &buffer->mutex);
    }
}

int sbuffer_insert(sbuffer_t *buffer, const sensor_data_t *data) {
    if (buffer == NULL || data == NULL) return SBUFFER_FAILURE;

    sbuffer_node_t *dummy = malloc(sizeof(sbuffer_node_t));
    if (dummy == NULL) return SBUFFER_FAILURE;

    dummy->data = *data;
    dummy->next = NULL;
    dummy->read_by_dm = false;
    dummy->read_by_sm = false;

    pthread_mutex_lock(&buffer->mutex);

    if (buffer->closed) {
        pthread_mutex_unlock(&buffer->mutex);
        free(dummy);
        return SBUFFER_FAILURE;
    }

    if (buffer->tail == NULL) {
        buffer->head = buffer->tail = dummy;
    } else {
        buffer->tail->next = dummy;
        buffer->tail = dummy;
    }

    /* Multiple consumers may be waiting */
    pthread_cond_broadcast(&buffer->cond_nempty);
    pthread_mutex_unlock(&buffer->mutex);

    return SBUFFER_SUCCESS;
}
int sbuffer_close(sbuffer_t *buffer) {
    if (buffer == NULL) return SBUFFER_FAILURE;

    pthread_mutex_lock(&buffer->mutex);
    buffer->closed = true;

    pthread_cond_broadcast(&buffer->cond_nempty);
    pthread_mutex_unlock(&buffer->mutex);

    return SBUFFER_SUCCESS;
}
