/**
* \author Diego Vallés
 */

//FIND DOCUMENTATION ON THIS IMPLEMENTATION
#include "sensor_db.h"
#include <stdbool.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>   // write(), ssize_t
#include <errno.h>    // errno, EINTR
#include <stdarg.h>   // va_list, va_start, va_end
#include <stdio.h>    // vsnprintf

// Logger state
static int pipe_ready = -1;
static pthread_mutex_t log_mtx = PTHREAD_MUTEX_INITIALIZER;
static int logger_ready = 0;

/* Write-all helper: ensures full LOG_MSG_MAX bytes are written */
static int write_all(int fd, const void *buf, size_t n)
{
    const char *p = (const char *)buf;
    size_t left = n;
    while (left > 0) {
        ssize_t w = write(fd, p, left);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        p += (size_t)w;
        left -= (size_t)w;
    }
    return 0;
}

int logger_init(int pipe_write_fd)
{
    pthread_mutex_lock(&log_mtx);
    pipe_ready = pipe_write_fd;
    logger_ready = (pipe_ready >= 0);
    pthread_mutex_unlock(&log_mtx);
    return logger_ready ? 0 : -1;
}

// OPTIONAL Function
void logger_close(void)
{
    pthread_mutex_lock(&log_mtx);
    pipe_ready = -1;
    logger_ready = 0;
    pthread_mutex_unlock(&log_mtx);

    /* Do not close(fd) here. main.c owns the pipe lifecycle. */
    /* If you want, you may destroy mutex at end of program, but not required. */
}

void log_event(const char *fmt, ...)
{
    if (!fmt) return;

    char msg[LOG_MSG_MAX];
    memset(msg, 0, sizeof(msg));

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    pthread_mutex_lock(&log_mtx);
    int fd = pipe_ready;
    int ready = logger_ready;
    pthread_mutex_unlock(&log_mtx);

    if (!ready || fd < 0) {
        /* If logger is not initialized, you may drop silently or fallback to stderr */
        /* fprintf(stderr, "%s\n", msg); */
        return;
    }

    pthread_mutex_lock(&log_mtx);
    /* One atomic “record write” per event: fixed size */
    (void)write_all(fd, msg, sizeof(msg));
    pthread_mutex_unlock(&log_mtx);
}

FILE * open_db(char * filename, bool append) {
    if (filename == NULL) return NULL;

    const char *mode;
    if (append) {mode = "a";}
    else {mode = "w";}

    FILE *f = fopen(filename,mode);
    if (f == NULL) {
        fprintf(stderr, "Error: could not open file in open_db\n");
        return NULL;
    }
    if (!append) {
        fprintf(stderr, "A new data.csv file has been created\n");
    }
    //write_to_log_process("Opened CSV file");
    return f;
}

int insert_sensor(FILE * f, sensor_id_t id, sensor_value_t value, sensor_ts_t ts) {
    if( f == NULL ) {return -1;}

    int success = fprintf(f,"%u,%f,%ld\n",id,value,ts);
    if(success < 0){fprintf(stderr, "Error: data insertion into data.csv failed\n");return -1;}
    fprintf(stderr, "New sensor value inserted\n");
    return 0;
}

int close_db(FILE * f) {
    if (f==NULL){return -1;}

    int check = fclose(f);

    if (check < 0){
        fprintf(stderr, "Failed to close CSV file\n");
    }
    else{
        fprintf(stderr, "Closed CSV file\n");
    }
    return check;
}