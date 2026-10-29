#ifndef RING_BUF_H_
#define RING_BUF_H_

#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
/* Flight-logic ring layout, widened for DMA-sized USART buffers. */
#define BUFFER_SIZE 1024u
typedef struct {
  uint8_t buf[BUFFER_SIZE];
  _Atomic volatile uint32_t read;
  _Atomic volatile uint32_t write;
  _Atomic volatile uint32_t count;
} ring_buf;

void init_buf(ring_buf *buffer);
void enqueue(ring_buf *buffer, const uint8_t byte);
void read_from_buf(ring_buf *buffer, uint8_t *payload, uint32_t len);

#endif // RING_BUF_H_
