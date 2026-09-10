#ifndef SD_TASK_H
#define SD_TASK_H

#include <stdint.h>

#include "stm32f4xx.h"
#include "stm32f4xx_hal_sd.h"

extern SD_HandleTypeDef hsd;

// TODO: is this fine? im guessing at what the struct should look like,
// regardless this is not the file that should define it
typedef struct {
    float       latitude;
    float       longitude;
    float       altitude;
    uint32_t    timestamp;
} GPS_Fix_t;

// TODO REMOVE BELOW !!! PLACEHOLDER
void GPS_GetFix(GPS_Fix_t *fix);

HAL_StatusTypeDef SD_Init(void);
void SD_Task(void);

#endif /* SD_TASK_H */
