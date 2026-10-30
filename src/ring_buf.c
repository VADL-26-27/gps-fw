#include "ring_buf.h"

void init_buf(ring_buf *buffer) {
  atomic_init(&buffer->read, 0);
  atomic_init(&buffer->write, 0);
  atomic_init(&buffer->count, 0);
}

void enqueue(ring_buf *buffer, const uint8_t byte) {
  if (buffer == NULL) {
    return;
  }

  if (atomic_load_explicit(&buffer->count, memory_order_acquire) >=
      BUFFER_SIZE) {
    return;
  }

  uint32_t write = atomic_load_explicit(&buffer->write, memory_order_acquire);

  buffer->buf[write] = byte;

  write = (write + 1U) & (BUFFER_SIZE - 1);
  atomic_store_explicit(&buffer->write, write, memory_order_release);
  atomic_fetch_add_explicit(&buffer->count, 1, memory_order_release);
}

// Read only when the buffer contains at least len bytes.
void read_from_buf(ring_buf *buffer, uint8_t *payload, uint32_t len) {
  if (buffer == NULL || payload == NULL) {
    return;
  }
  if (len > atomic_load_explicit(&buffer->count, memory_order_acquire)) {
    return;
  }

  uint32_t read = atomic_load_explicit(&buffer->read, memory_order_acquire);

  for (uint32_t i = 0; i < len; ++i) {
    *payload = buffer->buf[read];
    ++payload;
    read = (read + 1U) & (BUFFER_SIZE - 1);
    atomic_store_explicit(&buffer->read, read, memory_order_release);
    atomic_fetch_sub_explicit(&buffer->count, 1, memory_order_release);
  }
}
