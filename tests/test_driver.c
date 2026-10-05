#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/uart_dma_driver.c"

USART_TypeDef test_usart;
DMA_Stream_TypeDef test_rx_dma, test_tx_dma;
DMA_TypeDef test_dma;
RCC_TypeDef test_rcc;
GPIO_TypeDef test_gpioa, test_gpioc;
uint32_t SystemCoreClock = 16000000u;
static uint32_t notifications;
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
  uart_dma_set_notify_task(&lora_uart, &lora_uart);
  assert(uart_dma_lora_init(&lora_uart));
  clear_flags();
  assert(uart_dma_start_rx(&lora_uart)); clear_flags();
  test_usart.SR = 0;
}
static void arrival(unsigned position, uint32_t flags) {
  test_rx_dma.NDTR = UART_DMA_RX_RING_SIZE - position;
  test_dma.LISR |= flags;
  uart_dma_isr_rx_dma(&lora_uart); clear_flags();
}
int main(void) {
  setup();
  assert(lora_uart.notify_task == &lora_uart);
  assert((test_gpioa.AFR[1] & (GPIO_AFRH_AFSEL9 | GPIO_AFRH_AFSEL10)) == 0x770);
  assert(test_usart.BRR == 139);
  assert(!(test_usart.CR3 & USART_CR3_SCEN));
  assert(!(test_usart.CR1 & USART_CR1_TCIE));
  assert((test_rx_dma.CR & DMA_SxCR_DIR) == 0);
  assert(test_rx_dma.CR & DMA_SxCR_CIRC);
  test_rcc.CFGR = 5u << RCC_CFGR_PPRE2_Pos;
  assert(uart_dma_recover(&lora_uart)); clear_flags();
  assert(test_usart.BRR == 35); /* APB2 = HCLK / 4. */

  setup();
  uint8_t tx[] = {1,2,3};
  assert(uart_dma_start_tx(&lora_uart, tx, sizeof(tx)));
  test_usart.SR = 0; clear_flags();
  assert((test_tx_dma.CR & DMA_SxCR_DIR) == DMA_SxCR_DIR_0);
  assert(!(test_tx_dma.CR & DMA_SxCR_CIRC));
  assert(!uart_dma_start_tx(&lora_uart, tx, 1));
  test_dma.HISR = DMA_HISR_TCIF7; test_tx_dma.CR &= ~DMA_SxCR_EN;
  uart_dma_isr_tx_dma(&lora_uart); clear_flags();
  assert(lora_uart.tx_busy && !(notifications & UART_DMA_NOTIFY_TX_DONE));
  assert(test_usart.CR1 & USART_CR1_TCIE);
  test_usart.SR = USART_SR_TC; uart_dma_isr_usart(&lora_uart);
  assert(!lora_uart.tx_busy && (notifications & UART_DMA_NOTIFY_TX_DONE));
  assert(!(test_usart.CR1 & USART_CR1_TCIE) && !(test_usart.SR & USART_SR_TC));
  notifications = 0; test_usart.SR = USART_SR_TC; uart_dma_isr_usart(&lora_uart);
  assert(notifications == 0); /* A stale TC cannot complete another transfer. */

  /* Polling reads find bytes even without an IDLE interrupt. */
  setup();
  for (unsigned i = 0; i < UART_DMA_RX_RING_SIZE; ++i) lora_uart.rx_ring[i] = (uint8_t)i;
  test_rx_dma.NDTR = 1014;
  uint8_t data[64];
  assert(uart_dma_rx_available(&lora_uart) == 10);
  assert(uart_dma_rx_read(&lora_uart, data, sizeof(data)) == 10);
  for (unsigned i = 0; i < 10; ++i) assert(data[i] == i);
  /* Service progress at half boundaries; consumer can wrap without data loss. */
  arrival(512, DMA_LISR_HTIF2);
  while (uart_dma_rx_available(&lora_uart)) (void)uart_dma_rx_read(&lora_uart, data, sizeof(data));
  arrival(1000, 0);
  while (uart_dma_rx_available(&lora_uart)) (void)uart_dma_rx_read(&lora_uart, data, sizeof(data));
  arrival(10, DMA_LISR_TCIF2);
  assert(uart_dma_rx_read(&lora_uart, data, sizeof(data)) == 34);
  for (unsigned i = 0; i < 34; ++i) assert(data[i] == (uint8_t)(1000 + i));
  assert(!lora_uart.fault);

  /* A lapped task and an ISR blackout are both observable data loss. */
  setup(); arrival(512, DMA_LISR_HTIF2); arrival(0, DMA_LISR_TCIF2);
  assert(lora_uart.fault && lora_uart.overflow_count == 1);
  assert(notifications & UART_DMA_NOTIFY_ERROR);
  assert(uart_dma_rx_read(&lora_uart, data, sizeof(data)) == 0);
  assert(uart_dma_recover(&lora_uart)); clear_flags();
  assert(!lora_uart.fault && lora_uart.rx_producer == 0 && lora_uart.error_count == 1);
  arrival(200, DMA_LISR_HTIF2 | DMA_LISR_TCIF2);
  assert(lora_uart.fault && lora_uart.overflow_count == 2);

  /* An error wins over simultaneous DMA completion; buffer stays owned. */
  setup(); assert(uart_dma_start_tx(&lora_uart, tx, sizeof(tx))); clear_flags();
  test_dma.HISR = DMA_HISR_TEIF7 | DMA_HISR_TCIF7;
  uart_dma_isr_tx_dma(&lora_uart); clear_flags();
  assert(lora_uart.fault && lora_uart.tx_busy);
  assert(!(notifications & UART_DMA_NOTIFY_TX_DONE));
  assert(uart_dma_recover(&lora_uart)); clear_flags();
  assert(!lora_uart.tx_busy && !lora_uart.fault);
  assert(test_rx_dma.CR & DMA_SxCR_EN);

  setup(); test_usart.SR = USART_SR_IDLE | USART_SR_ORE;
  uart_dma_isr_usart(&lora_uart);
  assert(lora_uart.fault && lora_uart.error_count == 1);
  assert(notifications & UART_DMA_NOTIFY_ERROR);
  uart_dma_radio_control_init();
  assert(test_gpioc.OTYPER & (1u << 1));
  uart_dma_radio_reset(true); assert(test_gpioc.BSRR == ((1u << 16) | (1u << 17)));
  uart_dma_radio_reset(false); assert(test_gpioc.BSRR == ((1u << 16) | (1u << 1)));
  puts("UART/DMA register tests passed");
}
