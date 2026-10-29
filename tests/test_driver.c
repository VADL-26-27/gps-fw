#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/usart.c"
#include "usart_board.h"

USART_TypeDef test_usart;
DMA_Stream_TypeDef test_rx_dma, test_tx_dma;
DMA_TypeDef test_dma;
RCC_TypeDef test_rcc;
GPIO_TypeDef test_gpioa, test_gpioc;
uint32_t SystemCoreClock = 16000000u;
static uint32_t notifications;
static usart_t lora_uart;
static unsigned clock_calls, gpio_calls;
static void enable_clock(void) { clock_calls++; }
static void init_gpio(void) { gpio_calls++; }
void SystemCoreClockUpdate(void) { }
TickType_t xTaskGetTickCount(void) { return 0; }
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return &lora_uart; }
BaseType_t xTaskNotify(TaskHandle_t t, uint32_t b, eNotifyAction a) {
  (void)t; (void)a; notifications |= b; return pdTRUE;
}
BaseType_t xTaskNotifyFromISR(TaskHandle_t t, uint32_t b, eNotifyAction a, BaseType_t *w) {
  *w = pdFALSE; return xTaskNotify(t, b, a);
}
BaseType_t xTaskNotifyWait(uint32_t a, uint32_t b, uint32_t *v, TickType_t ticks) {
  (void)a; (void)b; (void)v; (void)ticks; return pdFALSE;
}
static void clear_flags(void) {
  test_dma.LISR &= ~test_dma.LIFCR; test_dma.LIFCR = 0;
  test_dma.HISR &= ~test_dma.HIFCR; test_dma.HIFCR = 0;
}
static void setup(void) {
  memset(&lora_uart, 0, sizeof(lora_uart)); memset(&test_usart, 0, sizeof(test_usart));
  memset(&test_rx_dma, 0, sizeof(test_rx_dma)); memset(&test_tx_dma, 0, sizeof(test_tx_dma));
  memset(&test_dma, 0, sizeof(test_dma)); memset(&test_rcc, 0, sizeof(test_rcc));
  notifications = 0;
  clock_calls = gpio_calls = 0;
  lora_uart = (usart_t){
    .regs = &test_usart, .dma = &test_dma,
    .rx_dma = &test_rx_dma, .tx_dma = &test_tx_dma,
    .irqn = USART1_IRQn, .rx_dma_irqn = DMA2_Stream2_IRQn,
    .tx_dma_irqn = DMA2_Stream7_IRQn, .rx_stream = 2, .tx_stream = 7,
    .rx_channel = 4, .tx_channel = 4,
    .clock_hz = 16000000, .baud_rate = 115200, .irq_priority = 6,
    .enable_clock = enable_clock, .init_gpio = init_gpio,
  };
  usart_set_notify_task(&lora_uart, &lora_uart);
  assert(usart_init(&lora_uart));
  clear_flags();
  assert(usart_start_rx(&lora_uart)); clear_flags();
  test_usart.SR = 0;
}
static void arrival(unsigned position, uint32_t flags) {
  test_rx_dma.NDTR = USART_RX_RING_SIZE - position;
  test_dma.LISR |= flags;
  usart_handle_rx_dma_irq(&lora_uart); clear_flags();
}
/* Exercise every stream's low/high flag bank using a separate DMA controller
 * and peripheral. Nothing in the test's original instance may be changed. */
static void alternate_mappings(void) {
  static const unsigned shifts[8] = {0, 6, 16, 22, 0, 6, 16, 22};
  setup();
  USART_TypeDef original_usart = test_usart;
  DMA_TypeDef original_dma = test_dma;
  for (unsigned tx = 0; tx < 8; ++tx) {
    unsigned rx = 7 - tx;
    USART_TypeDef regs = {0};
    DMA_TypeDef dma = {0};
    DMA_Stream_TypeDef tx_dma = {0}, rx_dma = {0};
    usart_t port = {
      .regs = &regs, .dma = &dma, .tx_dma = &tx_dma, .rx_dma = &rx_dma,
      .irqn = USART2_IRQn, .tx_dma_irqn = DMA1_Stream0_IRQn,
      .rx_dma_irqn = DMA1_Stream1_IRQn,
      .tx_stream = tx, .rx_stream = rx, .tx_channel = tx, .rx_channel = rx,
      .clock_hz = 8000000, .baud_rate = 9600, .irq_priority = 6,
      .enable_clock = enable_clock, .init_gpio = init_gpio,
    };
    assert(usart_init(&port));
    assert(regs.BRR == 833);
    regs.SR = 0;
    assert(usart_start_rx(&port));
    assert((rx_dma.CR & DMA_SxCR_CHSEL) == rx << DMA_SxCR_CHSEL_Pos);
    rx_dma.NDTR = USART_RX_RING_SIZE - 3;
    port.rx_ring[0] = 0x42;
    uint8_t bytes[3];
    assert(usart_rx_read(&port, bytes, sizeof(bytes)) == 3 && bytes[0] == 0x42);
    uint8_t payload[] = {0x12, 0x34};
    assert(usart_start_tx(&port, payload, sizeof(payload)));
    assert((tx_dma.CR & DMA_SxCR_CHSEL) == tx << DMA_SxCR_CHSEL_Pos);
    dma.LIFCR = dma.HIFCR = 0;
    if (tx < 4) dma.LISR = 1u << (shifts[tx] + 5);
    else dma.HISR = 1u << (shifts[tx] + 5);
    tx_dma.CR &= ~DMA_SxCR_EN;
    usart_handle_tx_dma_irq(&port);
    assert(port.tx_dma_done && port.tx_busy && !port.fault);
    assert((tx < 4 ? dma.LIFCR : dma.HIFCR) == 1u << (shifts[tx] + 5));
    assert(!(regs.CR1 & (USART_CR1_TXEIE | USART_CR1_RXNEIE |
                         USART_CR1_TCIE | USART_CR1_IDLEIE)));
    regs.SR = USART_SR_TC;
    assert(!usart_tx_busy(&port));
    dma.LISR = dma.HISR = dma.LIFCR = dma.HIFCR = 0;
    if (rx < 4) dma.LISR = 1u << (shifts[rx] + 3);
    else dma.HISR = 1u << (shifts[rx] + 3);
    usart_handle_rx_dma_irq(&port);
    assert(port.fault && port.error_count == 1);
    assert(!(regs.CR3 & USART_CR3_DMAR));
    assert((rx < 4 ? dma.LIFCR : dma.HIFCR) == 0x3du << shifts[rx]);
    assert(memcmp(&test_usart, &original_usart, sizeof(test_usart)) == 0);
    assert(memcmp(&test_dma, &original_dma, sizeof(test_dma)) == 0);
  }
}

int main(void) {
  /* The production board mapping remains deliberately empty. */
  assert(board_lora_usart_get() == &lora_usart);
  assert(!usart_init(&lora_usart));
  assert(!usart_init(NULL));
  assert(!usart_start_rx(&lora_usart));
  assert(!usart_start_tx(&lora_usart, (const uint8_t *)"x", 1));
  usart_poll(&lora_usart);
  assert(!usart_stop(&lora_usart));
  alternate_mappings();
  setup();
  assert(lora_uart.notify_task == &lora_uart);
  assert(clock_calls == 1 && gpio_calls == 1);
  assert(test_usart.BRR == 139);
  assert(!(test_usart.CR3 & USART_CR3_SCEN));
  assert(!(test_usart.CR1 & USART_CR1_TCIE));
  assert((test_rx_dma.CR & DMA_SxCR_DIR) == 0);
  assert(test_rx_dma.CR & DMA_SxCR_CIRC);
  lora_uart.clock_hz = 4000000;
  assert(usart_recover(&lora_uart)); clear_flags();
  assert(test_usart.BRR == 35); /* Board supplied peripheral clock. */

  setup();
  uint8_t tx[] = {1,2,3};
  assert(usart_start_tx(&lora_uart, tx, sizeof(tx)));
  test_usart.SR = 0; clear_flags();
  assert((test_tx_dma.CR & DMA_SxCR_DIR) == DMA_SxCR_DIR_0);
  assert(!(test_tx_dma.CR & DMA_SxCR_CIRC));
  assert(!usart_start_tx(&lora_uart, tx, 1));
  test_dma.HISR = DMA_HISR_TCIF7; test_tx_dma.CR &= ~DMA_SxCR_EN;
  usart_handle_tx_dma_irq(&lora_uart); clear_flags();
  assert(lora_uart.tx_busy && (notifications & USART_NOTIFY_TX_DONE));
  assert(!(test_usart.CR1 & USART_CR1_TCIE));
  assert(usart_tx_busy(&lora_uart));
  test_usart.SR = USART_SR_TC;
  assert(!usart_tx_busy(&lora_uart));
  assert(!(test_usart.CR1 & (USART_CR1_TXEIE | USART_CR1_RXNEIE |
                            USART_CR1_TCIE | USART_CR1_IDLEIE)));
  notifications = 0; test_usart.SR = USART_SR_TC; usart_handle_irq(&lora_uart);
  assert(notifications == 0); /* A stale TC cannot complete another transfer. */

  /* Polling reads find bytes even without an IDLE interrupt. */
  setup();
  for (unsigned i = 0; i < USART_RX_RING_SIZE; ++i) lora_uart.rx_ring[i] = (uint8_t)i;
  test_rx_dma.NDTR = 1014;
  uint8_t data[64];
  assert(usart_rx_available(&lora_uart) == 10);
  assert(usart_rx_read(&lora_uart, data, sizeof(data)) == 10);
  for (unsigned i = 0; i < 10; ++i) assert(data[i] == i);
  /* Service progress at half boundaries; consumer can wrap without data loss. */
  arrival(512, DMA_LISR_HTIF2);
  while (usart_rx_available(&lora_uart)) (void)usart_rx_read(&lora_uart, data, sizeof(data));
  arrival(1000, 0);
  while (usart_rx_available(&lora_uart)) (void)usart_rx_read(&lora_uart, data, sizeof(data));
  arrival(10, DMA_LISR_TCIF2);
  assert(usart_rx_read(&lora_uart, data, sizeof(data)) == 34);
  for (unsigned i = 0; i < 34; ++i) assert(data[i] == (uint8_t)(1000 + i));
  assert(!lora_uart.fault);

  /* A lapped task and an ISR blackout are both observable data loss. */
  setup(); arrival(512, DMA_LISR_HTIF2); arrival(0, DMA_LISR_TCIF2);
  assert(lora_uart.fault && lora_uart.overflow_count == 1);
  assert(notifications & USART_NOTIFY_ERROR);
  assert(usart_rx_read(&lora_uart, data, sizeof(data)) == 0);
  assert(usart_recover(&lora_uart)); clear_flags();
  assert(!lora_uart.fault && lora_uart.rx_producer == 0 && lora_uart.error_count == 1);
  arrival(200, DMA_LISR_HTIF2 | DMA_LISR_TCIF2);
  assert(lora_uart.fault && lora_uart.overflow_count == 2);

  /* An error wins over simultaneous DMA completion; buffer stays owned. */
  setup(); assert(usart_start_tx(&lora_uart, tx, sizeof(tx))); clear_flags();
  test_dma.HISR = DMA_HISR_TEIF7 | DMA_HISR_TCIF7;
  usart_handle_tx_dma_irq(&lora_uart); clear_flags();
  assert(lora_uart.fault && lora_uart.tx_busy);
  assert(!(notifications & USART_NOTIFY_TX_DONE));
  assert(usart_recover(&lora_uart)); clear_flags();
  assert(!lora_uart.tx_busy && !lora_uart.fault);
  assert(test_rx_dma.CR & DMA_SxCR_EN);

  setup(); test_usart.SR = USART_SR_IDLE | USART_SR_ORE;
  usart_handle_irq(&lora_uart);
  assert(lora_uart.fault && lora_uart.error_count == 1);
  assert(notifications & USART_NOTIFY_ERROR);
  puts("USART/DMA register tests passed");
}
