#include "stm32f4xx.h"
#include "FreeRTOS.h"
#include "task.h"

static volatile uint32_t heartbeat;

static void heartbeat_task(void *argument) {
  (void)argument;

  for (;;) {
    heartbeat++;
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

int main(void) {
  if (xTaskCreate(heartbeat_task, "heartbeat", 128, NULL, 1, NULL) != pdPASS) {
    for (;;) {
    }
  }

  vTaskStartScheduler();

  /* Execution reaches here only if the scheduler cannot start. */
  for (;;) {
  }
}