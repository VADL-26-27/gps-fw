#ifndef GPS_MAILBOX_H
#define GPS_MAILBOX_H

#include <stdbool.h>
#include "FreeRTOS.h"
#include "task.h"
#include "gps_structs.h"

typedef struct {
  gps_fix_t fix;
  TickType_t acquired_tick;
} gps_mailbox_t;

/* Initialize once before creating producers/consumers. Task context only.
 * Publish overwrites an unsent fix; receive removes the latest value. */
void gps_mailbox_init(void);
bool gps_mailbox_publish(const gps_mailbox_t *value);
bool gps_mailbox_receive(gps_mailbox_t *out);
void gps_mailbox_set_notify_task(TaskHandle_t task, uint32_t bits);

#endif
