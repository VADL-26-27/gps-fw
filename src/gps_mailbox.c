#include "gps_mailbox.h"

static QueueHandle_t latest_fix;

void gps_mailbox_init(void) {
  latest_fix = xQueueCreate(1, sizeof(gps_mailbox_t));
  configASSERT(latest_fix != NULL);
}