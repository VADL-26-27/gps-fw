#ifndef GROUND_RX_FIFO_H
#define GROUND_RX_FIFO_H

#include <stdbool.h>
#include <stdint.h>
#include "rak3172_protocol.h"

typedef struct {
  uint16_t length;
  uint8_t payload[RAK3172_MAX_PAYLOAD];
} ground_rx_record_t;

/* Called from LoRa task context for each decoded ground packet.
 * The future FIFO implementation must copy the record before returning:
 * this pointer is temporary. Never block or write to SD here.
 * Return true only if a copy was queued; false means unavailable/full.
 * The current placeholder returns false and stores nothing. */
bool ground_rx_fifo_try_push(const ground_rx_record_t *record);

#endif
