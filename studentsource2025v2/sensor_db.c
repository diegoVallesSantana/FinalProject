/**
* \author Diego Vallés
 */
#include <stdio.h>
#include <stdbool.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include "sensor_db.h"

// Logger states
static int pipe_ready = -1;
static pthread_mutex_t log_mtx = PTHREAD_MUTEX_INITIALIZER; //initialised here to avoid giving too much info to main.c
static int logger_ready = 0;

static int write_all(int fd, const void *buf, size_t nbytes)
{
    const char *pbuf = buf;
    size_t left = nbytes;
    while (left > 0) {
        ssize_t w = write(fd, pbuf, left);
        if (w <= 0) {
            return -1;
        }
        pbuf += (size_t)w;
        left -= (size_t)w;
    }
    return 0;
}

int logger_init(int pipe_write_fd)
{
    int result;
    pthread_mutex_lock(&log_mtx);
    pipe_ready = pipe_write_fd;
    if (pipe_ready >= 0) {
        logger_ready = 1;
        result = 0;
    } else {
        logger_ready = 0;
        result = -1;
    }
    pthread_mutex_unlock(&log_mtx);
    return result;
}

void logger_close(void)
{
    pthread_mutex_lock(&log_mtx);
    pipe_ready = -1;
    logger_ready = 0;
    pthread_mutex_unlock(&log_mtx);
}

//I used a standard buffer for MS2 which is not the best for logging messages of variable sizes
//AI recommended I looked into stdarg.h as it is very commun for logging and easy to implement
//example implementation:
//Stackoverflow: https://stackoverflow.com/questions/40484293/stdarg-and-printf-in-c
//Stackexchange: https://codereview.stackexchange.com/questions/285703/logger-using-variadic-macros
//IBM vsnprintf https://www.ibm.com/docs/en/i/7.4.0?topic=functions-vsnprintf-print-argument-data-bufferhttps://www.ibm.com/docs/en/i/7.4.0?topic=functions-vsnprintf-print-argument-data-buffer
//ZetCode: https://zetcode.com/clang/vsnprintf/
void log_event(const char *fmt, ...)
{
    if (!fmt) return;
    char msg[MSG_MAX];
    memset(msg, 0, sizeof(msg));
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    pthread_mutex_lock(&log_mtx);
    if (!logger_ready || pipe_ready < 0) {
        pthread_mutex_unlock(&log_mtx);
        return;
    }
    (void)write_all(pipe_ready, msg, sizeof(msg));
    pthread_mutex_unlock(&log_mtx);
}

FILE * open_db(const char * filename, bool append) {
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
        log_event("A new data.csv file has been created");
    }
    return f;
}

int insert_sensor(FILE * f, sensor_id_t id, sensor_value_t value, sensor_ts_t ts) {
    if( f == NULL ) {return -1;}

    int success = fprintf(f,"%u,%f,%ld\n",id,value,ts);
    if(success < 0){fprintf(stderr, "Error: data insertion into data.csv failed\n");return -1;}
    log_event("Data insertion from sensor %u succeeded", (unsigned)id);
    return 0;
}

int close_db(FILE * f) {
    if (f==NULL){return -1;}

    int check = fclose(f);

    if (check < 0){
        fprintf(stderr, "Failed to close CSV file\n");
    }
    else {
        log_event("The data.csv file has been closed");
    }
    return check;
}