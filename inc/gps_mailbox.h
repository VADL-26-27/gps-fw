#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "gps_structs.h"

typedef struct {
  gps_fix_t fix;
  TickType_t acquired_tick; // Local monotonic time for freshness checks
} gps_mailbox_t;

_Static_assert(sizeof(gps_mailbox_t) == 56, "telemetry_frame_t layout changed");

void gps_mailbox_init(void);