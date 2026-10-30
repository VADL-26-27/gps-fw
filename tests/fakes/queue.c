#include "queue.h"
#include <string.h>
struct fake_queue {
  unsigned depth, size, count, head;
  unsigned char data[8][512];
};
static struct fake_queue queues[4];
static unsigned used;
QueueHandle_t xQueueCreate(unsigned count, unsigned size) {
  assert(used < 4 && count <= 8 && size <= 512);
  struct fake_queue *q = &queues[used++];
  q->depth = count; q->size = size;
  return q;
}
BaseType_t xQueueOverwrite(QueueHandle_t q, const void *item) {
  assert(q->depth == 1);
  memcpy(q->data[0], item, q->size); q->count = 1; return pdPASS;
}
BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t wait) {
  assert(wait == 0);
  if (q->count == q->depth) return pdFALSE;
  memcpy(q->data[(q->head + q->count++) % q->depth], item, q->size);
  return pdPASS;
}
BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t wait) {
  assert(wait == 0);
  if (!q->count) return pdFALSE;
  memcpy(item, q->data[q->head], q->size);
  q->head = (q->head + 1) % q->depth; q->count--; return pdPASS;
}
