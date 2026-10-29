#include "gps_mailbox.h"
#include "queue.h"

static QueueHandle_t latest_fix;
static TaskHandle_t notify_task;
static uint32_t notify_bits;

void gps_mailbox_init(void) {
  if (latest_fix) return;
  latest_fix = xQueueCreate(1, sizeof(gps_mailbox_t));
  configASSERT(latest_fix != NULL);
}

bool gps_mailbox_publish(const gps_mailbox_t *value) {
  if (!latest_fix || !value) return false;
  if (xQueueOverwrite(latest_fix, value) != pdPASS) return false;
  taskENTER_CRITICAL();
  TaskHandle_t task = notify_task;
  uint32_t bits = notify_bits;
  taskEXIT_CRITICAL();
  if (task) xTaskNotify(task, bits, eSetBits);
  return true;
}

bool gps_mailbox_receive(gps_mailbox_t *out) {
  return latest_fix && out && xQueueReceive(latest_fix, out, 0) == pdPASS;
}

void gps_mailbox_set_notify_task(TaskHandle_t task, uint32_t bits) {
  taskENTER_CRITICAL();
  notify_task = task;
  notify_bits = bits;
  taskEXIT_CRITICAL();
}
