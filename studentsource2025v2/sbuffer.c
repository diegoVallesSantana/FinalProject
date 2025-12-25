/**
 * \author {Bert Lagaisse + Diego Vallés}
 */
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <stdbool.h>
#include "sbuffer.h"

//Garbage collection removes fully read nodes: https://learn.microsoft.com/fr-fr/dotnet/standard/garbage-collection/fundamentals
//Would be nice to add Inline to make checks faster and avoid slowing the program: https://learn.microsoft.com/fr-fr/cpp/cpp/inline-functions-cpp?view=msvc-170
//Static to avoid use from other files
/**
 * basic node for the buffer, these nodes are linked together to create the buffer
 */
typedef struct sbuffer_node {
    struct sbuffer_node *next;
    sensor_data_t data;
    bool read_by_dm;//condition: both dm and sm should have read the value for it to be removed
    bool read_by_sm;
} sbuffer_node_t;

/**
 * a structure to keep track of the buffer
 */
struct sbuffer {
    sbuffer_node_t *head;
    sbuffer_node_t *tail;
    pthread_mutex_t mutex;
    bool closed; // condition: threads wait for sensor values while the buffer is not closed
    pthread_cond_t cond_nempty;
};

//Check if already read by a given reader
static bool node_read_by(const sbuffer_node_t *n, sbuffer_reader_t reader) {
    if (reader == SBUFFER_READER_DM) {
        return n->read_by_dm;
    } else {
        return n->read_by_sm;
    }
}

//check if node read by DM
static void node_mark_read(sbuffer_node_t *n, sbuffer_reader_t reader) {
    if (reader == SBUFFER_READER_DM) {
        n->read_by_dm = true;
    }
    else {
        n->read_by_sm = true;
    }
}

//Node read by both dm and sm => ready to be remove
static bool node_fully_read(const sbuffer_node_t *n) {
    return n->read_by_dm && n->read_by_sm;
}

//Garbage collection:
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

//Finds the oldest unread node by reader:
static sbuffer_node_t* find_oldest_unread(sbuffer_t *buffer, sbuffer_reader_t reader) {
    sbuffer_node_t *dummy = buffer->head;
    while (dummy) {
        if (!node_read_by(dummy, reader)) {return dummy;}
        dummy = dummy->next;
    }
    return NULL;
}

int sbuffer_init(sbuffer_t **buffer) {
    if (buffer == NULL) {return SBUFFER_FAILURE;}

    *buffer = malloc(sizeof(sbuffer_t));
    if (*buffer == NULL) {return SBUFFER_FAILURE;}
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

    while (1) {

        garbageCollectionFullyRead(buffer);

        sbuffer_node_t *dummy = find_oldest_unread(buffer, reader);
        if (dummy != NULL) {
            *data = dummy->data;
            node_mark_read(dummy, reader);
            garbageCollectionFullyRead(buffer);
            pthread_mutex_unlock(&buffer->mutex);
            return SBUFFER_SUCCESS;
        }

        if (buffer->closed==true) {
            pthread_mutex_unlock(&buffer->mutex);
            return SBUFFER_NO_DATA;
        }

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
        buffer->head = dummy;
        buffer->tail = dummy;
    }
    else{
        buffer->tail->next = dummy;
        buffer->tail = dummy;
    }

    pthread_cond_broadcast(&buffer->cond_nempty);
    pthread_mutex_unlock(&buffer->mutex);
    return SBUFFER_SUCCESS;
}
int sbuffer_close(sbuffer_t *buffer) {
    if (buffer == NULL) {return SBUFFER_FAILURE;}

    pthread_mutex_lock(&buffer->mutex);
    buffer->closed = true;
    pthread_cond_broadcast(&buffer->cond_nempty);
    pthread_mutex_unlock(&buffer->mutex);

    return SBUFFER_SUCCESS;
}
