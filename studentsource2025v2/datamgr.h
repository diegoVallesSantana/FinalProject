/**
 * \author {Diego Vallés}
 */

#ifndef DATAMGR_H_
#define DATAMGR_H_

#include <stdint.h>
#include <time.h>
#include "config.h"
#include "sbuffer.h"

#ifndef RUN_AVG_LENGTH
#define RUN_AVG_LENGTH 5
#endif

typedef struct {
    sensor_id_t id;
    uint16_t room;

    sensor_value_t history[RUN_AVG_LENGTH];
    int history_count;
    int history_index;

    sensor_value_t running_avg;
    time_t last_ts;

    int last_zone;   // -1 cold, 0 normal, +1 hot (used to avoid repeated logs)
} datamgr_sensor_t;

typedef struct {
    sbuffer_t *buffer;         // shared buffer to consume from
    const char *map_filename;  // typically "room_sensor.map"
} datamgr_args_t;


void *datamgr_run(void *arg);

/**
 * This method should be called to clean up the datamgr, and to free all used memory.
 * After this, any call to datamgr_get_room_id, datamgr_get_avg, datamgr_get_last_modified or datamgr_get_total_sensors will not return a valid result
 */
void datamgr_free();


#endif  //DATAMGR_H_
