#ifndef TEST_QUEUE_H
#define TEST_QUEUE_H
#include "FreeRTOS.h"
#define configASSERT(x) assert(x)
typedef struct fake_queue *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned count, unsigned size);
BaseType_t xQueueOverwrite(QueueHandle_t q, const void *item);
BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t wait);
BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t wait);
#endif
