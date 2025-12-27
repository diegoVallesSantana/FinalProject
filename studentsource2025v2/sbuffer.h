/**
* \author {Bert Lagaisse + Diego Vallés}
 */

#ifndef _SBUFFER_H_
#define _SBUFFER_H_

#include <pthread.h>
#include <stdbool.h>
#include "config.h"

#define SBUFFER_FAILURE -1
#define SBUFFER_SUCCESS 0
#define SBUFFER_NO_DATA 1

typedef struct sbuffer sbuffer_t;

// syntax of enum:https://learn.microsoft.com/fr-fr/cpp/c-language/c-enumeration-declarations?view=msvc-170
typedef enum readConditions {
  SBUFFER_READER_DM = 0,
  SBUFFER_READER_SM = 1
} sbuffer_reader_t;

/**
 * Allocates and initializes a new shared buffer
 * \param buffer a double pointer to the buffer that needs to be initialized
 * \return SBUFFER_SUCCESS on success and SBUFFER_FAILURE if an error occurred
 */
int sbuffer_init(sbuffer_t **buffer);

/**
 * All allocated resources are freed and cleaned up
 * \param buffer a double pointer to the buffer that needs to be freed
 * \return SBUFFER_SUCCESS on success and SBUFFER_FAILURE if an error occurred
 */
int sbuffer_free(sbuffer_t **buffer);

/**
 * Removes the first sensor data in 'buffer' (at the 'head') and returns this sensor data as '*data'
 * \param data a pointer to pre-allocated sensor_data_t space, the data will be copied into this structure. No new memory is allocated for 'data' in this function.
 * \return SBUFFER_SUCCESS on success and SBUFFER_FAILURE if an error occurred
 */
// NEW: sbuffer_reader_t reader condition
//Blocks if empty and not closed
//Returns SBUFFER_NO_DATA only if closed and drained for that reader * \param buffer a pointer to the buffer that is used

int sbuffer_remove(sbuffer_t *buffer, sensor_data_t *data, sbuffer_reader_t reader);

/**
 * Inserts the sensor data in 'data' at the end of 'buffer' (at the 'tail')
 * \param buffer a pointer to the buffer that is used
 * \param data a pointer to sensor_data_t data, that will be copied into the buffer
 * \return SBUFFER_SUCCESS on success and SBUFFER_FAILURE if an error occured
*/
int sbuffer_insert(sbuffer_t *buffer, const sensor_data_t *data);


//broadcast to all threads waiting forever
int sbuffer_close(sbuffer_t *buffer);

#endif  //_SBUFFER_H_
