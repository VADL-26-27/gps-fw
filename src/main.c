#include "stm32f4xx.h"
#include "FreeRTOS.h"
#include "task.h"
#include "lora_task.h"

/* Add #include "gps_task.h" when GPS_Task is implemented. */
enum {
  HEARTBEAT_PRIORITY = 1,
  LORA_PRIORITY = 3,
  GPS_PRIORITY = 4,
};

static volatile uint32_t heartbeat;

static void heartbeat_task(void *argument) {
  (void)argument;

  for (;;) {
    heartbeat++;
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

int main(void) {
  /* TBD: define a static lora_config_t with approved RF settings and pass its
   * address instead of NULL. The task runs but stays OFFLINE without them.
   * No GPS task is needed to create LoRa_Task; no position is sent until
   * a producer calls lora_task_publish_fix() with a real parsed fix. */
  if (xTaskCreate(LoRa_Task, "LoRa", 1024, NULL, LORA_PRIORITY, NULL) != pdPASS) {
    for (;;) {
    }
  }

  /* Future GPS integration: add its sources to C_SOURCES in Makefile, include
   * gps_task.h above, and create GPS_Task here before starting the scheduler.
   * Choose its stack depth from parser requirements and measured stack use:
   *
   * if (xTaskCreate(GPS_Task, "GPS", GPS_STACK_WORDS, NULL,
   *                 GPS_PRIORITY, NULL) != pdPASS) {
   *   for (;;) {}
   * }
   *
   * GPS_Task owns USART6 RX/parsing. After assembling a valid gps_fix_t, call
   * lora_task_publish_fix(&fix) from task context, never from its UART ISR.
   * Task creation order is not execution order; pre-start fixes are retained.
   */

  if (xTaskCreate(heartbeat_task, "heartbeat", 128, NULL,
                  HEARTBEAT_PRIORITY, NULL) != pdPASS) {
    for (;;) {
    }
  }

  vTaskStartScheduler();

  /* Execution reaches here only if the scheduler cannot start. */
  for (;;) {
  }
}
