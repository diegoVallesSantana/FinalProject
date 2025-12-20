/**
* \author Diego Vallés
 */

#include "sensor_db.h"
//#include "logger.h"
#include <stdbool.h>

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