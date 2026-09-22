#include "ground_rx_fifo.h"

bool ground_rx_fifo_try_push(const ground_rx_record_t *record) {
  /* Integration point: replace with a nonblocking, copying FIFO enqueue.
   * For a FreeRTOS queue of ground_rx_record_t, use xQueueSend(..., 0).
   * The SD task will own dequeuing and storage writes. */
  (void)record;
  return false;
}
