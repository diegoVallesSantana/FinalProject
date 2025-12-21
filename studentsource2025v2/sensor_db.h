/**
* \author Bert Lagaisse + Diego Vallés
 */

#ifndef _SENSOR_DB_H_
#define _SENSOR_DB_H_

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>   // bool
#include "config.h"

/* Size of one log record written to the pipe (fixed-size protocol). */
#ifndef LOG_MSG_MAX
#define LOG_MSG_MAX 256
#endif

int logger_init(int pipe_write_fd);

void logger_close(void);

void log_event(const char *fmt, ...);

FILE * open_db(char * filename, bool append);

int insert_sensor(FILE * f, sensor_id_t id, sensor_value_t value, sensor_ts_t ts);

int close_db(FILE * f);


#endif /* _SENSOR_DB_H_ */