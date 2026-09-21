#ifndef SD_TASK_H
#define SD_TASK_H

#include <stdint.h>

#include "gps_structs.h"
#include "stm32f4xx.h"
#include "stm32f4xx_hal_sd.h"

extern SD_HandleTypeDef hsd;

// TODO REMOVE BELOW !!! PLACEHOLDER
void GPS_GetFix(gps_fix_t *fix);

HAL_StatusTypeDef SD_Init(void);
void SD_Task(void);

#endif /* SD_TASK_H */
