#ifndef SD_TASK_H
#define SD_TASK_H

#include <stdint.h>

#include "stm32f4xx.h"
#include "stm32f4xx_hal_sd.h"

extern SD_HandleTypeDef hsd;

HAL_StatusTypeDef SD_Init(void);

#endif /* SD_TASK_H */
