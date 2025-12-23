/**
* \author Bert Lagaisse + Diego Vallés
 */
#ifndef _SENSOR_DB_H_
#define _SENSOR_DB_H_

#include <stdio.h>
#include <stdbool.h>
#include "config.h"

#define MSG_MAX 256

int logger_init(int pipe_write_fd);

void logger_close(void);

void log_event(const char *fmt, ...);

FILE * open_db(char * filename, bool append);

int insert_sensor(FILE * f, sensor_id_t id, sensor_value_t value, sensor_ts_t ts);

int close_db(FILE * f);

#endif