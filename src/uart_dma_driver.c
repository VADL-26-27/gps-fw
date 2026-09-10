#include "uart_dma_driver.h"
#include "FreeRTOSConfig.h"

#define UART_DMA_BAUD 115200u
#define RX_FLAGS (DMA_LIFCR_CFEIF2 | DMA_LIFCR_CDMEIF2 | DMA_LIFCR_CTEIF2 | \
                  DMA_LIFCR_CHTIF2 | DMA_LIFCR_CTCIF2)
#define TX_FLAGS (DMA_HIFCR_CFEIF7 | DMA_HIFCR_CDMEIF7 | DMA_HIFCR_CTEIF7 | \
                  DMA_HIFCR_CHTIF7 | DMA_HIFCR_CTCIF7)
#define RX_ERRORS (DMA_LISR_TEIF2 | DMA_LISR_DMEIF2 | DMA_LISR_FEIF2)
#define TX_ERRORS (DMA_HISR_TEIF7 | DMA_HISR_DMEIF7 | DMA_HISR_FEIF7)
#define RX_PROGRESS (DMA_LISR_HTIF2 | DMA_LISR_TCIF2)

_Static_assert((UART_DMA_RX_RING_SIZE & (UART_DMA_RX_RING_SIZE - 1u)) == 0u,
               "RX ring must be a power of two");
static uart_dma_t lora_uart;

static bool dma_stream_disable(DMA_Stream_TypeDef *stream) {
  stream->CR &= ~DMA_SxCR_EN;
  for (uint32_t i = 0; i < 1000u; ++i) {
    if (!(stream->CR & DMA_SxCR_EN)) return true;
  }
  return false;
}

static void fault(uart_dma_t *dev, bool overflow) {
  if (!dev->fault) {
    dev->error_count++;
    if (overflow) dev->overflow_count++;
  }
  dev->fault = true;
}

/* Called with this UART's interrupts masked, or from equal-priority ISRs.
 * Clear only flags observed before NDTR: a later boundary remains pending.
 * If both boundaries accumulated, the number of laps is unknowable. */
static void snapshot_rx(uart_dma_t *dev) {
  if (!dev->rx_running || dev->fault) return;
  uint32_t flags = DMA2->LISR & (RX_ERRORS | RX_PROGRESS);
  DMA2->LIFCR = flags;
  uint32_t remaining = dev->rx_dma->NDTR;
  if ((flags & RX_ERRORS) || remaining > UART_DMA_RX_RING_SIZE) {
    fault(dev, false);
    return;
  }
  if ((flags & RX_PROGRESS) == RX_PROGRESS) {
    fault(dev, true);
    return;
  }
  uint16_t position = (UART_DMA_RX_RING_SIZE - remaining) &
                      (UART_DMA_RX_RING_SIZE - 1u);
  dev->rx_producer += (position - dev->rx_position) &
                      (UART_DMA_RX_RING_SIZE - 1u);
  dev->rx_position = position;
  if (dev->rx_producer - dev->rx_consumer >= UART_DMA_RX_RING_SIZE)
    fault(dev, true);
}

static void notify_isr(uart_dma_t *dev, uint32_t bits) {
  BaseType_t wake = pdFALSE;
  if (dev->fault) bits |= UART_DMA_NOTIFY_ERROR;
  if (bits && dev->notify_task)
    xTaskNotifyFromISR(dev->notify_task, bits, eSetBits, &wake);
  portYIELD_FROM_ISR(wake);
}

static void configure_uart(uart_dma_t *dev) {
  /* SystemCoreClock is HCLK; USART1 is on APB2. */
  SystemCoreClockUpdate();
  uint32_t ppre = (RCC->CFGR & RCC_CFGR_PPRE2) >> RCC_CFGR_PPRE2_Pos;
  uint32_t pclk = SystemCoreClock >> (ppre < 4u ? 0u : ppre - 3u);
  dev->usart->CR1 = 0u;
  dev->usart->BRR = (pclk + UART_DMA_BAUD / 2u) / UART_DMA_BAUD;
  dev->usart->CR2 = 0u;
  dev->usart->CR3 = USART_CR3_DMAR | USART_CR3_EIE;
  (void)dev->usart->SR;
  (void)dev->usart->DR;
  dev->usart->SR = ~(uint32_t)USART_SR_TC;
  dev->usart->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE |
                    USART_CR1_IDLEIE;
}

uart_dma_t *uart_dma_lora_get(void) { return &lora_uart; }

bool uart_dma_lora_init(uart_dma_t *dev) {
  dev->usart = USART1;
  dev->rx_dma = DMA2_Stream2;
  dev->tx_dma = DMA2_Stream7;
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_DMA2EN;
  RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
  (void)RCC->AHB1ENR;
  (void)RCC->APB2ENR;
  if (!uart_dma_stop(dev)) return false;
  RCC->APB2RSTR |= RCC_APB2RSTR_USART1RST;
  (void)RCC->APB2RSTR;
  RCC->APB2RSTR &= ~RCC_APB2RSTR_USART1RST;

  GPIOA->OTYPER &= ~((1u << 9) | (1u << 10));
  GPIOA->OSPEEDR |= GPIO_OSPEEDR_OSPEED9 | GPIO_OSPEEDR_OSPEED10;
  GPIOA->PUPDR = (GPIOA->PUPDR & ~(GPIO_PUPDR_PUPD9 | GPIO_PUPDR_PUPD10)) |
                 GPIO_PUPDR_PUPD10_0;
  GPIOA->AFR[1] = (GPIOA->AFR[1] & ~(GPIO_AFRH_AFSEL9 | GPIO_AFRH_AFSEL10)) |
                  (7u << GPIO_AFRH_AFSEL9_Pos) | (7u << GPIO_AFRH_AFSEL10_Pos);
  GPIOA->MODER = (GPIOA->MODER & ~(GPIO_MODER_MODER9 | GPIO_MODER_MODER10)) |
                 GPIO_MODER_MODER9_1 | GPIO_MODER_MODER10_1;
  configure_uart(dev);
  dev->fault = false;
  NVIC_SetPriority(USART1_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);
  NVIC_SetPriority(DMA2_Stream2_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);
  NVIC_SetPriority(DMA2_Stream7_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);
  NVIC_EnableIRQ(USART1_IRQn);
  NVIC_EnableIRQ(DMA2_Stream2_IRQn);
  NVIC_EnableIRQ(DMA2_Stream7_IRQn);
  return true;
}

void uart_dma_set_notify_task(uart_dma_t *dev, TaskHandle_t task) {
  dev->notify_task = task;
}

bool uart_dma_stop(uart_dma_t *dev) {
  NVIC_DisableIRQ(USART1_IRQn);
  NVIC_DisableIRQ(DMA2_Stream2_IRQn);
  NVIC_DisableIRQ(DMA2_Stream7_IRQn);
  dev->usart->CR3 = 0u;
  dev->usart->CR1 = 0u;
  bool rx_stopped = dma_stream_disable(dev->rx_dma);
  bool tx_stopped = dma_stream_disable(dev->tx_dma);
  dev->rx_running = false;
  DMA2->LIFCR = RX_FLAGS;
  DMA2->HIFCR = TX_FLAGS;
  NVIC_ClearPendingIRQ(USART1_IRQn);
  NVIC_ClearPendingIRQ(DMA2_Stream2_IRQn);
  NVIC_ClearPendingIRQ(DMA2_Stream7_IRQn);
  if (!rx_stopped || !tx_stopped) {
    fault(dev, false);
    return false;
  }
  dev->tx_busy = false;
  dev->rx_producer = dev->rx_consumer = dev->rx_position = 0u;
  return true;
}

bool uart_dma_recover(uart_dma_t *dev) {
  if (!uart_dma_lora_init(dev)) return false;
  return uart_dma_start_rx(dev);
}

bool uart_dma_start_rx(uart_dma_t *dev) {
  taskENTER_CRITICAL();
  bool stopped = dma_stream_disable(dev->rx_dma);
  if (stopped) {
    DMA2->LIFCR = RX_FLAGS;
    dev->rx_dma->CR = (4u << DMA_SxCR_CHSEL_Pos) | DMA_SxCR_PL |
        DMA_SxCR_CIRC | DMA_SxCR_MINC | DMA_SxCR_TCIE | DMA_SxCR_HTIE |
        DMA_SxCR_TEIE | DMA_SxCR_DMEIE;
    dev->rx_dma->FCR = DMA_SxFCR_FEIE;
    dev->rx_dma->NDTR = UART_DMA_RX_RING_SIZE;
    dev->rx_dma->PAR = (uint32_t)(uintptr_t)&dev->usart->DR;
    dev->rx_dma->M0AR = (uint32_t)(uintptr_t)dev->rx_ring;
    dev->rx_producer = dev->rx_consumer = dev->rx_position = 0u;
    (void)dev->usart->SR;
    (void)dev->usart->DR;
    dev->rx_running = true;
    __DMB();
    dev->rx_dma->CR |= DMA_SxCR_EN;
  } else {
    fault(dev, false);
  }
  taskEXIT_CRITICAL();
  return stopped;
}

bool uart_dma_start_tx(uart_dma_t *dev, const uint8_t *buf, uint16_t len) {
  if (!buf || !len || dev->tx_busy || dev->fault) return false;
  taskENTER_CRITICAL();
  bool stopped = dma_stream_disable(dev->tx_dma);
  if (stopped) {
    dev->usart->CR1 &= ~USART_CR1_TCIE;
    dev->usart->CR3 &= ~USART_CR3_DMAT;
    DMA2->HIFCR = TX_FLAGS;
    dev->tx_dma->CR = (4u << DMA_SxCR_CHSEL_Pos) | DMA_SxCR_PL_1 |
        DMA_SxCR_DIR_0 | DMA_SxCR_MINC | DMA_SxCR_TCIE |
        DMA_SxCR_TEIE | DMA_SxCR_DMEIE;
    dev->tx_dma->FCR = DMA_SxFCR_FEIE;
    dev->tx_dma->NDTR = len;
    dev->tx_dma->PAR = (uint32_t)(uintptr_t)&dev->usart->DR;
    dev->tx_dma->M0AR = (uint32_t)(uintptr_t)buf;
    dev->usart->SR = ~(uint32_t)USART_SR_TC;
    dev->tx_busy = true;
    __DMB();
    dev->tx_dma->CR |= DMA_SxCR_EN;
    dev->usart->CR3 |= USART_CR3_DMAT;
  } else {
    fault(dev, false);
  }
  taskEXIT_CRITICAL();
  return stopped;
}

uint16_t uart_dma_rx_available(uart_dma_t *dev) {
  taskENTER_CRITICAL();
  snapshot_rx(dev);
  uint16_t n = dev->fault ? 0u : (uint16_t)(dev->rx_producer - dev->rx_consumer);
  taskEXIT_CRITICAL();
  return n;
}

uint16_t uart_dma_rx_read(uart_dma_t *dev, uint8_t *dst, uint16_t max) {
  if (!dst) return 0u;
  if (max > UART_DMA_RX_READ_MAX) max = UART_DMA_RX_READ_MAX;
  taskENTER_CRITICAL();
  snapshot_rx(dev);
  uint32_t n = dev->fault ? 0u : dev->rx_producer - dev->rx_consumer;
  if (n > max) n = max;
  for (uint32_t i = 0; i < n; ++i)
    dst[i] = ((volatile uint8_t *)dev->rx_ring)[(dev->rx_consumer + i) &
                                             (UART_DMA_RX_RING_SIZE - 1u)];
  /* DMA keeps running during the copy. Reject a copy if it was overtaken. */
  snapshot_rx(dev);
  if (dev->fault) n = 0u;
  dev->rx_consumer += n;
  taskEXIT_CRITICAL();
  return (uint16_t)n;
}

void uart_dma_rx_reset(uart_dma_t *dev) {
  taskENTER_CRITICAL();
  snapshot_rx(dev);
  dev->rx_consumer = dev->rx_producer;
  taskEXIT_CRITICAL();
}

bool uart_dma_tx_busy(const uart_dma_t *dev) { return dev->tx_busy; }
bool uart_dma_faulted(const uart_dma_t *dev) { return dev->fault; }
uint32_t uart_dma_error_count(const uart_dma_t *dev) { return dev->error_count; }

void uart_dma_isr_usart(uart_dma_t *dev) {
  uint32_t sr = dev->usart->SR;
  uint32_t bits = 0u;
  if (sr & (USART_SR_IDLE | USART_SR_ORE | USART_SR_FE | USART_SR_NE | USART_SR_PE)) {
    /* One SR/DR sequence clears IDLE and receive errors together. */
    (void)dev->usart->DR;
    if (sr & (USART_SR_ORE | USART_SR_FE | USART_SR_NE | USART_SR_PE))
      fault(dev, false);
    snapshot_rx(dev);
    bits |= UART_DMA_NOTIFY_RX_DATA;
  }
  if ((sr & USART_SR_TC) && (dev->usart->CR1 & USART_CR1_TCIE)) {
    dev->usart->CR1 &= ~USART_CR1_TCIE;
    dev->usart->SR = ~(uint32_t)USART_SR_TC;
    if (dev->tx_busy && !dev->fault) {
      dev->tx_busy = false;
      bits |= UART_DMA_NOTIFY_TX_DONE;
    }
  }
  notify_isr(dev, bits);
}

void uart_dma_isr_rx_dma(uart_dma_t *dev) {
  snapshot_rx(dev);
  /* A faulted stream must not keep interrupting while the task recovers. */
  if (dev->fault) {
    dev->usart->CR3 &= ~USART_CR3_DMAR;
    dev->rx_dma->CR &= ~(DMA_SxCR_HTIE | DMA_SxCR_TCIE | DMA_SxCR_TEIE | DMA_SxCR_DMEIE);
    dev->rx_dma->FCR &= ~DMA_SxFCR_FEIE;
    DMA2->LIFCR = RX_FLAGS;
  }
  notify_isr(dev, UART_DMA_NOTIFY_RX_DATA);
}

void uart_dma_isr_tx_dma(uart_dma_t *dev) {
  uint32_t flags = DMA2->HISR;
  DMA2->HIFCR = flags & TX_FLAGS;
  if (flags & TX_ERRORS) {
    dev->usart->CR3 &= ~USART_CR3_DMAT;
    dev->usart->CR1 &= ~USART_CR1_TCIE;
    dev->tx_dma->CR &= ~(DMA_SxCR_TCIE | DMA_SxCR_TEIE | DMA_SxCR_DMEIE);
    dev->tx_dma->FCR &= ~DMA_SxFCR_FEIE;
    fault(dev, false);
    /* Keep buffer owned until task-context abort confirms DMA stopped. */
  } else if ((flags & DMA_HISR_TCIF7) && dev->tx_busy && !dev->fault) {
    dev->usart->CR3 &= ~USART_CR3_DMAT;
    /* TC may already be set; enabling TCIE also handles that case. */
    dev->usart->CR1 |= USART_CR1_TCIE;
  }
  notify_isr(dev, 0u);
}

void uart_dma_radio_control_init(void) {
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;
  (void)RCC->AHB1ENR;
  /* README: PC0 BOOT0 low = normal boot; PC1 NRST open-drain, released.
   * Module NRST has a pull-up. Never actively drive its reset line high. */
  GPIOC->BSRR = (1u << (0u + 16u)) | (1u << 1u);
  GPIOC->OTYPER = (GPIOC->OTYPER & ~1u) | (1u << 1u);
  GPIOC->PUPDR &= ~(GPIO_PUPDR_PUPD0 | GPIO_PUPDR_PUPD1);
  GPIOC->MODER = (GPIOC->MODER & ~(GPIO_MODER_MODER0 | GPIO_MODER_MODER1)) |
                 GPIO_MODER_MODER0_0 | GPIO_MODER_MODER1_0;
}

void uart_dma_radio_reset(bool asserted) {
  GPIOC->BSRR = (1u << 16u) | (asserted ? (1u << 17u) : (1u << 1u));
}

void USART1_IRQHandler(void) { uart_dma_isr_usart(&lora_uart); }
void DMA2_Stream2_IRQHandler(void) { uart_dma_isr_rx_dma(&lora_uart); }
void DMA2_Stream7_IRQHandler(void) { uart_dma_isr_tx_dma(&lora_uart); }
