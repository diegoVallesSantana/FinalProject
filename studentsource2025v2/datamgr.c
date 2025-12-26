/**
* \author {Diego Vallés}
 */
#include <stdio.h>
#include <stdlib.h>
#include "lib/dplist.h"
#include "config.h"
#include "sbuffer.h"
#include "datamgr.h"
#include "sensor_db.h"

static dplist_t *sensor_list = NULL;

static void element_free(void **element) {
    free(*element);
    *element = NULL;
}

static datamgr_sensor_t *find_sensor(sensor_id_t id) {
    if (sensor_list==NULL){ return NULL;}
    int lsize = dpl_size(sensor_list);
    for (int i = 0; i < lsize; i++) {
        datamgr_sensor_t *sensor = dpl_get_element_at_index(sensor_list, i);
        if (sensor && sensor->id == id) {return sensor;}
    }
    return NULL;
}

static int load_map(const char *map_filename) {
    FILE *fp = fopen(map_filename, "r");
    if (fp == NULL) {fprintf(stderr, "Error: could not open map_file\n"); return -1;}
    if (sensor_list == NULL) {
        sensor_list = dpl_create(NULL, element_free, NULL);
        if (sensor_list == NULL){fprintf(stderr, "Error: sensor list null\n");fclose(fp);return -1;}
    }

    uint16_t room;
    uint16_t sensor_id;
    while (fscanf(fp, "%hu %hu", &room, &sensor_id) == 2) {
        datamgr_sensor_t *sensor = malloc(sizeof(datamgr_sensor_t));
        if (sensor==NULL) {
            fprintf(stderr, "Error: sensor list null\n");
            fclose(fp);
            dpl_free(&sensor_list, true);
            sensor_list = NULL;
            return -1;
        }
        sensor->id = (sensor_id_t)sensor_id;
        sensor->room = room;
        sensor->history_count = 0;
        sensor->history_index = 0;
        sensor->running_avg = 0.0;
        sensor->last_ts = 0;
        sensor->last_com = 0;

        for (int i = 0; i < RUN_AVG_LENGTH; i++) {
            sensor->history[i] = 0.0;
        }
        dpl_insert_at_index(sensor_list, sensor, dpl_size(sensor_list), false);
    }
    fclose(fp);
    return 0;
}

void *datamgr_thread(void *arg) {
    datamgr_args_t *pargs = (datamgr_args_t *)arg;
    datamgr_args_t args = *pargs;
    free(pargs);

    if (load_map(args.map_filename) != 0) {
        log_event("Data manager aborted due to map load failure");
        return NULL;
    }

    sensor_data_t m;
    while (1){
        int rc = sbuffer_remove(args.buffer, &m, SBUFFER_READER_DM);
        if (rc == SBUFFER_SUCCESS) {
            datamgr_sensor_t *sensor = find_sensor(m.id);
            if (sensor == NULL) {
                log_event("Received sensor data with invalid sensor node ID %u", (unsigned)m.id);
                continue;
            }


            sensor->last_ts = m.ts;
            sensor->history[sensor->history_index] = m.value;
            sensor->history_index = (sensor->history_index + 1) % RUN_AVG_LENGTH;
            if (sensor->history_count < RUN_AVG_LENGTH) sensor->history_count++;
            if (sensor->history_count == RUN_AVG_LENGTH) {
                double sum = 0.0;
                for (int i = 0; i < RUN_AVG_LENGTH; i++) sum += sensor->history[i];
                sensor->running_avg = (sensor_value_t)(sum / RUN_AVG_LENGTH);
                int comment = 0;
                if (sensor->running_avg < SET_MIN_TEMP) comment = -1;
                else if (sensor->running_avg > SET_MAX_TEMP) comment = +1;

                if (comment != sensor->last_com) {
                    if (comment == -1) {
                        log_event("Sensor node %u reports it’s too cold (avg temp = %g)",
                                  (unsigned)m.id, sensor->running_avg);
                    } else if (comment == +1) {
                        log_event("Sensor node %u reports it’s too hot (avg temp = %g)",
                                  (unsigned)m.id, sensor->running_avg);
                    }
                    sensor->last_com = comment;
                }

            } else {
                sensor->running_avg = 0;
            }
        } else {
            break;
        }
    }
    log_event("Data manager stopped");
    return NULL;
}

void datamgr_free(){
    if (sensor_list == NULL) return;
    dpl_free(&sensor_list, true);
    sensor_list = NULL;
}