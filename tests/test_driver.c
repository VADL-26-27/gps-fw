#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "usart_board.h"

/* Real STM32F4 layout is required for pointer-derived DMA stream indices. */
static struct { DMA_TypeDef regs; DMA_Stream_TypeDef streams[8]; } controller;
static USART_TypeDef regs;
static usart_t port;
static ring_buf rx_buffer, tx_buffer;
static TickType_t ticks;
static unsigned clocks, gpios, resets;
TickType_t xTaskGetTickCount(void) { return ticks; }

#define usart_poll actual_usart_poll
#include "../src/usart.c"
#undef usart_poll

static void emulate_clear_flags(void) {
  controller.regs.LISR &= ~controller.regs.LIFCR;
  controller.regs.HISR &= ~controller.regs.HIFCR;
  controller.regs.LIFCR = controller.regs.HIFCR = 0;
}
void usart_poll(usart_t *p) { actual_usart_poll(p); emulate_clear_flags(); }
static void clock_on(void) { clocks++; }
static void gpio_init(void) { gpios++; }
static void peripheral_reset(usart_t *p) { (void)p; resets++; }

static void setup(unsigned tx, unsigned rx) {
  memset(&controller, 0, sizeof(controller)); memset(&regs, 0, sizeof(regs));
  ticks = 0; clocks = gpios = resets = 0;
  port = (usart_t){
    .regs = &regs, .rx = &rx_buffer, .tx = &tx_buffer,
    .clock_hz = 16000000, .baud_rate = 115200,
    .dma = &controller.regs,
    .tx_dma = &controller.streams[tx], .rx_dma = &controller.streams[rx],
    .irqn = USART1_IRQn, .tx_dma_irqn = DMA2_Stream7_IRQn,
    .rx_dma_irqn = DMA2_Stream2_IRQn, .tx_channel = 3, .rx_channel = 5,
    .enable_clock = clock_on, .init_gpio = gpio_init,
    .reset_peripheral = peripheral_reset,
  };
  assert(usart_init(&port)); emulate_clear_flags(); regs.SR = 0;
  assert(clocks == 1 && gpios == 1 && resets == 1 && regs.BRR == 139);
  assert(port.rx_busy && (port.rx_dma->CR & DMA_SxCR_CIRC));
  assert(!(regs.CR1 & (USART_CR1_RXNEIE | USART_CR1_TXEIE |
                       USART_CR1_TCIE | USART_CR1_IDLEIE)));
}
static void event(unsigned stream, unsigned bit) {
  static const unsigned shift[] = {0, 6, 16, 22, 0, 6, 16, 22};
  uint32_t mask = 1u << (shift[stream] + bit);
  if (stream < 4) controller.regs.LISR |= mask;
  else controller.regs.HISR |= mask;
}
static void tx_complete(unsigned stream) {
  event(stream, 5); port.tx_dma->CR &= ~DMA_SxCR_EN;
  usart_handle_tx_dma_irq(&port); emulate_clear_flags();
  assert(port.tx_dma_done && port.tx_busy);
  regs.SR = USART_SR_TC;
  usart_poll(&port);
  regs.SR = 0; /* A queued segment may have cleared TC via an SR write. */
  emulate_clear_flags();
}
static void arrival(unsigned position, bool lap) {
  port.rx_dma->NDTR = BUFFER_SIZE - position;
  if (lap) event(2, 5);
  usart_handle_rx_dma_irq(&port); emulate_clear_flags();
}

int main(void) {
  assert(!usart_init(NULL));
  assert(!usart_init(board_lora_usart_get()));
  assert(!usart_write(&lora_usart, (const uint8_t *)"x", 1));
  assert(!usart_stop(&lora_usart));
  uint8_t data[64];
  for (unsigned stream = 0; stream < 8; ++stream) {
    setup(stream, 7 - stream);
    assert((port.rx_dma->CR & DMA_SxCR_CHSEL) == 5u << DMA_SxCR_CHSEL_Pos);
    port.rx->buf[0] = 0x42; port.rx_dma->NDTR = BUFFER_SIZE - 1;
    assert(usart_rx_available(&port) == 1);
    assert(!usart_read(&port, data, 2));
    assert(usart_rx_available(&port) == 1);
    assert(usart_read(&port, data, 1) && data[0] == 0x42);
    uint8_t tx[] = {1, 2, 3};
    assert(usart_write(&port, tx, sizeof(tx))); regs.SR = 0; emulate_clear_flags();
    memset(tx, 9, sizeof(tx)); /* write copies, unlike the old zero-copy API. */
    assert(port.tx->buf[0] == 1 && port.tx->buf[2] == 3);
    assert((port.tx_dma->CR & DMA_SxCR_CHSEL) == 3u << DMA_SxCR_CHSEL_Pos);
    assert(usart_write(&port, (const uint8_t *)"AB", 2));
    assert(port.tx->buf[0] == 1 && atomic_load(&port.tx->count) == 5);
    assert(port.tx_dma->NDTR == 3); /* In-flight DMA isn't restarted by write. */
    tx_complete(stream);
    assert(port.tx_busy && port.tx_dma->NDTR == 2);
    assert(atomic_load(&port.tx->count) == 2);
    tx_complete(stream);
    assert(!port.tx_busy && port.tx_result == USART_RESULT_OK);
    assert(atomic_load(&port.tx->count) == 0);
    assert(!(regs.CR1 & USART_CR1_TCIE));
    event(7 - stream, 3); usart_handle_rx_dma_irq(&port); emulate_clear_flags();
    assert(usart_faulted(&port) && port.rx_busy);
    assert(usart_stop(&port)); emulate_clear_flags();
  }

  setup(7, 2);
  uint8_t full[BUFFER_SIZE]; memset(full, 0xa5, sizeof(full));
  assert(usart_write(&port, full, sizeof(full))); regs.SR = 0; emulate_clear_flags();
  assert(!usart_write(&port, (const uint8_t *)"x", 1));
  assert(atomic_load(&port.tx->count) == BUFFER_SIZE && port.tx->buf[0] == 0xa5);
  tx_complete(7);
  assert(!usart_tx_busy(&port));

  /* Ring helper lengths/counts must support more than 255 bytes. */
  ring_buf helper;
  init_buf(&helper);
  for (unsigned i = 0; i < BUFFER_SIZE; ++i) enqueue(&helper, (uint8_t)i);
  read_from_buf(&helper, full, 300);
  assert(atomic_load(&helper.count) == BUFFER_SIZE - 300);
  for (unsigned i = 0; i < 300; ++i) assert(full[i] == (uint8_t)i);

  /* TX wraps across two DMA segments, preserving byte order. */
  setup(7, 2);
  assert(usart_write(&port, full, BUFFER_SIZE - 2)); regs.SR = 0; emulate_clear_flags();
  tx_complete(7);
  assert(usart_write(&port, (const uint8_t *)"ABCD", 4)); regs.SR = 0; emulate_clear_flags();
  assert(port.tx_dma->NDTR == 2);
  assert(port.tx->buf[BUFFER_SIZE - 2] == 'A' && port.tx->buf[0] == 'C');
  tx_complete(7); assert(port.tx_dma->NDTR == 2 && port.tx_busy);
  tx_complete(7); assert(!port.tx_busy);

  setup(7, 2);
  ticks = UINT32_MAX - 10u;
  assert(usart_write(&port, (const uint8_t *)"x", 1)); regs.SR = 0; emulate_clear_flags();
  assert(port.tx_start_ms == UINT32_MAX - 10u);
  ticks += USART_TRANSFER_TIMEOUT_MS;
  usart_poll(&port);
  assert(usart_faulted(&port) && port.tx_busy);
  assert(!usart_write(&port, (const uint8_t *)"x", 1));
  assert(usart_recover(&port)); emulate_clear_flags(); regs.SR = 0;
  assert(!usart_faulted(&port) && !port.tx_busy && port.rx_busy);
  assert(atomic_load(&port.tx->count) == 0);

  assert(usart_write(&port, (const uint8_t *)"x", 1)); regs.SR = 0; emulate_clear_flags();
  event(7, 3); event(7, 5);
  usart_handle_tx_dma_irq(&port); emulate_clear_flags();
  assert(usart_faulted(&port) && port.tx_busy && port.tx_result == USART_RESULT_FAILED);

  setup(7, 2);
  for (unsigned i = 0; i < BUFFER_SIZE; ++i) port.rx->buf[i] = (uint8_t)i;
  arrival(10, false);
  assert(usart_read(&port, data, 10));
  for (unsigned i = 0; i < 10; ++i) assert(data[i] == i);
  arrival(512, false);
  while (usart_rx_available(&port)) {
    unsigned n = usart_rx_available(&port); if (n > sizeof(data)) n = sizeof(data);
    assert(usart_read(&port, data, n));
  }
  arrival(1000, false);
  while (usart_rx_available(&port)) {
    unsigned n = usart_rx_available(&port); if (n > sizeof(data)) n = sizeof(data);
    assert(usart_read(&port, data, n));
  }
  arrival(10, true);
  assert(usart_rx_available(&port) == 34 && usart_read(&port, data, 34));
  for (unsigned i = 0; i < 34; ++i) assert(data[i] == (uint8_t)(1000 + i));
  ticks += USART_TRANSFER_TIMEOUT_MS * 2;
  usart_poll(&port); assert(!usart_faulted(&port));
  arrival(10, true);
  assert(usart_rx_available(&port) == 0 && usart_faulted(&port));
  assert(usart_recover(&port)); emulate_clear_flags(); regs.SR = 0;
  event(2, 4); event(2, 5);
  usart_handle_rx_dma_irq(&port); emulate_clear_flags(); assert(usart_faulted(&port));

  setup(7, 2);
  arrival(5, false); assert(usart_rx_available(&port) == 5);
  usart_rx_reset(&port); assert(usart_rx_available(&port) == 0);
  regs.SR = USART_SR_ORE; usart_handle_irq(&port); assert(usart_faulted(&port));

  /* A second port has independent rings, DMA registers and completion state. */
  setup(7, 2);
  struct { DMA_TypeDef regs; DMA_Stream_TypeDef streams[8]; } other_dma = {0};
  USART_TypeDef other_regs = {0};
  ring_buf other_rx, other_tx;
  usart_t other = port;
  other.regs = &other_regs; other.dma = &other_dma.regs;
  other.rx_dma = &other_dma.streams[1]; other.tx_dma = &other_dma.streams[0];
  other.rx = &other_rx; other.tx = &other_tx;
  other.irqn = USART2_IRQn;
  other.rx_dma_irqn = DMA1_Stream1_IRQn; other.tx_dma_irqn = DMA1_Stream0_IRQn;
  other.clock_hz = 8000000;
  assert(usart_init(&other)); other_regs.SR = 0;
  assert(usart_write(&other, (const uint8_t *)"other", 5)); other_regs.SR = 0;
  assert(other_regs.BRR == 69 && regs.BRR == 139);
  assert(atomic_load(&other_tx.count) == 5 && atomic_load(&port.tx->count) == 0);
  assert(other.tx_busy && !port.tx_busy);
  other_rx.buf[0] = 0x99; other.rx_dma->NDTR = BUFFER_SIZE - 1;
  assert(usart_read(&other, data, 1) && data[0] == 0x99);
  assert(usart_rx_available(&port) == 0);

  setup(7, 2);
  port.clock_hz = 4000000; port.reset_peripheral = NULL;
  assert(usart_recover(&port)); emulate_clear_flags(); regs.SR = 0;
  assert(regs.BRR == 35); /* Driver computes baud; reset callback is optional. */
  unsigned previous_clocks = clocks;
  port.rx_dma = (DMA_Stream_TypeDef *)((uintptr_t)port.dma + 3u);
  assert(!usart_init(&port) && clocks == previous_clocks);
  puts("Buffered USART/DMA tests passed");
}
