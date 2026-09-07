#include "uart_dma_driver.h"

#include "FreeRTOSConfig.h"

#ifndef USART_CR3_OREIE
#define USART_CR3_OREIE (1u << 5)
#endif

#define UART_DMA_APB2_HZ SystemCoreClock
#define UART_DMA_BAUD 115200u

#define UART_DMA_USART1_IRQ USART1_IRQn
#define UART_DMA_RX_STREAM_IRQ DMA2_Stream2_IRQn
#define UART_DMA_TX_STREAM_IRQ DMA2_Stream7_IRQn

#define UART_DMA_RX_DMA_CHSEL 4u
#define UART_DMA_TX_DMA_CHSEL 4u

#define UART_DMA_IRQ_PRIORITY configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY

#define UART_DMA_RX_PL (DMA_SxCR_PL_0 | DMA_SxCR_PL_1)
#define UART_DMA_TX_PL (DMA_SxCR_PL_1)

#define UART_DMA_RX_FLAG_CLEAR                                                        \
  (DMA_LIFCR_CFEIF2 | DMA_LIFCR_CDMEIF2 | DMA_LIFCR_CTEIF2 | DMA_LIFCR_CHTIF2 |       \
   DMA_LIFCR_CTCIF2)
#define UART_DMA_TX_FLAG_CLEAR                                                        \
  (DMA_HIFCR_CFEIF7 | DMA_HIFCR_CDMEIF7 | DMA_HIFCR_CTEIF7 | DMA_HIFCR_CHTIF7 |       \
   DMA_HIFCR_CTCIF7)

static uart_dma_t lora_uart;

static void dma_stream_disable(DMA_Stream_TypeDef *stream) {
  stream->CR &= ~DMA_SxCR_EN;
  for (volatile uint32_t i = 0u; i < 1000u && (stream->CR & DMA_SxCR_EN); i++) {
  }
}

static void uart_dma_snapshot_producer(uart_dma_t *dev) {
  uint32_t ndtr = dev->rx_dma->NDTR;
  uint32_t head = UART_DMA_RX_RING_SIZE - ndtr;
  if (head >= UART_DMA_RX_RING_SIZE) {
    head = 0u;
  }
  dev->rx_producer = (uint16_t)head;
}

void uart_dma_lora_init(uart_dma_t *dev) {
  dev->usart = USART1;
  dev->rx_dma = DMA2_Stream2;
  dev->tx_dma = DMA2_Stream7;
  dev->rx_chsel = UART_DMA_RX_DMA_CHSEL;
  dev->tx_chsel = UART_DMA_TX_DMA_CHSEL;
  dev->notify_task = NULL;
  dev->rx_producer = 0u;
  dev->rx_consumer = 0u;
  dev->tx_busy = false;
  dev->error_count = 0u;

  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_DMA2EN;
  RCC->APB2ENR |= RCC_APB2ENR_USART1EN;

  GPIOA->OSPEEDR |= GPIO_OSPEEDR_OSPEED9 | GPIO_OSPEEDR_OSPEED10;
  GPIOA->PUPDR &= ~GPIO_PUPDR_PUPD10;
  GPIOA->PUPDR |= GPIO_PUPDR_PUPD10_0;
  GPIOA->MODER =
      (GPIOA->MODER & ~(GPIO_MODER_MODER9 | GPIO_MODER_MODER10)) |
      GPIO_MODER_MODER9_1 | GPIO_MODER_MODER10_1;
  GPIOA->AFRH =
      (GPIOA->AFRH & ~(GPIO_AFRH_AFSEL9 | GPIO_AFRH_AFSEL10)) |
      (GPIO_AFRH_AFSEL9_0 | GPIO_AFRH_AFSEL9_1 | GPIO_AFRH_AFSEL9_2) |
      (GPIO_AFRH_AFSEL10_0 | GPIO_AFRH_AFSEL10_1 | GPIO_AFRH_AFSEL10_2);

  USART1->BRR = (UART_DMA_APB2_HZ + UART_DMA_BAUD / 2u) / UART_DMA_BAUD;
  USART1->CR2 = 0u;
  USART1->CR3 = USART_CR3_DMAT | USART_CR3_DMAR | USART_CR3_EIE |
                USART_CR3_OREIE;
  USART1->CR1 = USART_CR1_UE;
  USART1->CR1 |= USART_CR1_TE | USART_CR1_RE | USART_CR1_IDLEIE | USART_CR1_TCIE;

  NVIC_SetPriority(UART_DMA_USART1_IRQ, UART_DMA_IRQ_PRIORITY);
  NVIC_EnableIRQ(UART_DMA_USART1_IRQ);
  NVIC_SetPriority(UART_DMA_RX_STREAM_IRQ, UART_DMA_IRQ_PRIORITY);
  NVIC_EnableIRQ(UART_DMA_RX_STREAM_IRQ);
  NVIC_SetPriority(UART_DMA_TX_STREAM_IRQ, UART_DMA_IRQ_PRIORITY);
  NVIC_EnableIRQ(UART_DMA_TX_STREAM_IRQ);
}

uart_dma_t *uart_dma_lora_get(void) {
  return &lora_uart;
}

void uart_dma_set_notify_task(uart_dma_t *dev, TaskHandle_t task) {
  dev->notify_task = task;
}

void uart_dma_start_rx(uart_dma_t *dev) {
  dma_stream_disable(dev->rx_dma);
  DMA2->LIFCR = UART_DMA_RX_FLAG_CLEAR;

  dev->rx_dma->CR = ((uint32_t)dev->rx_chsel << DMA_SxCR_CHSEL_Pos) |
                    UART_DMA_RX_PL | DMA_SxCR_CIRC | DMA_SxCR_MINC |
                    DMA_SxCR_TCIE | DMA_SxCR_HTIE | DMA_SxCR_TEIE |
                    DMA_SxCR_DMEIE;
  dev->rx_dma->NDTR = UART_DMA_RX_RING_SIZE;
  dev->rx_dma->PAR = (uint32_t)&dev->usart->DR;
  dev->rx_dma->M0AR = (uint32_t)dev->rx_ring;

  (void)dev->usart->SR;
  (void)dev->usart->DR;

  dev->rx_producer = 0u;
  dev->rx_consumer = 0u;

  dev->rx_dma->CR |= DMA_SxCR_EN;
}

void uart_dma_start_tx(uart_dma_t *dev, const uint8_t *buf, uint16_t len) {
  if (dev->tx_busy || len == 0u) {
    return;
  }

  dma_stream_disable(dev->tx_dma);
  DMA2->HIFCR = UART_DMA_TX_FLAG_CLEAR;

  dev->tx_dma->CR = ((uint32_t)dev->tx_chsel << DMA_SxCR_CHSEL_Pos) |
                    UART_DMA_TX_PL | DMA_SxCR_DIR_1 | DMA_SxCR_MINC |
                    DMA_SxCR_TCIE | DMA_SxCR_TEIE | DMA_SxCR_DMEIE;
  dev->tx_dma->NDTR = len;
  dev->tx_dma->PAR = (uint32_t)&dev->usart->DR;
  dev->tx_dma->M0AR = (uint32_t)buf;

  dev->tx_busy = true;
  dev->tx_dma->CR |= DMA_SxCR_EN;
}

uint16_t uart_dma_rx_available(const uart_dma_t *dev) {
  uint16_t producer = dev->rx_producer;
  uint16_t consumer = dev->rx_consumer;
  if (producer >= consumer) {
    return (uint16_t)(producer - consumer);
  }
  return (uint16_t)(UART_DMA_RX_RING_SIZE - consumer + producer);
}

uint16_t uart_dma_rx_read(uart_dma_t *dev, uint8_t *dst, uint16_t max) {
  uint16_t available = uart_dma_rx_available(dev);
  uint16_t n = (available < max) ? available : max;
  for (uint16_t i = 0u; i < n; i++) {
    dst[i] = dev->rx_ring[dev->rx_consumer];
    dev->rx_consumer =
        (uint16_t)((dev->rx_consumer + 1u) & (UART_DMA_RX_RING_SIZE - 1u));
  }
  return n;
}

void uart_dma_rx_reset(uart_dma_t *dev) {
  dev->rx_consumer = dev->rx_producer;
}

bool uart_dma_tx_busy(const uart_dma_t *dev) {
  return dev->tx_busy;
}

uint32_t uart_dma_error_count(const uart_dma_t *dev) {
  return dev->error_count;
}

void uart_dma_isr_usart(uart_dma_t *dev) {
  USART_TypeDef *usart = dev->usart;
  uint32_t sr = usart->SR;
  uint32_t notify = 0u;
  BaseType_t yield = pdFALSE;

  if (sr & USART_SR_IDLE) {
    (void)usart->DR;
    uart_dma_snapshot_producer(dev);
    notify |= UART_DMA_NOTIFY_RX_DATA;
  }

  if (sr & (USART_SR_ORE | USART_SR_FE | USART_SR_NE | USART_SR_PE)) {
    (void)usart->DR;
    dev->error_count++;
    notify |= UART_DMA_NOTIFY_ERROR;
  }

  if (sr & USART_SR_TC) {
    dev->tx_busy = false;
    notify |= UART_DMA_NOTIFY_TX_DONE;
  }

  if (notify != 0u && dev->notify_task != NULL) {
    xTaskNotifyFromISR(dev->notify_task, notify, eSetBits, &yield);
  }

  portYIELD_FROM_ISR(yield);
}

void uart_dma_isr_rx_dma(uart_dma_t *dev) {
  uint32_t lisr = DMA2->LISR;
  uint32_t notify = 0u;
  BaseType_t yield = pdFALSE;

  if (lisr & (DMA_LISR_TEIF2 | DMA_LISR_DMEIF2 | DMA_LISR_FEIF2)) {
    DMA2->LIFCR = DMA_LIFCR_CTEIF2 | DMA_LIFCR_CDMEIF2 | DMA_LIFCR_CFEIF2;
    dev->error_count++;
    notify |= UART_DMA_NOTIFY_ERROR;
  }

  if (lisr & (DMA_LISR_TCIF2 | DMA_LISR_HTIF2)) {
    DMA2->LIFCR = DMA_LIFCR_CTCIF2 | DMA_LIFCR_CHTIF2;
    uart_dma_snapshot_producer(dev);
    notify |= UART_DMA_NOTIFY_RX_DATA;
  }

  if (notify != 0u && dev->notify_task != NULL) {
    xTaskNotifyFromISR(dev->notify_task, notify, eSetBits, &yield);
  }

  portYIELD_FROM_ISR(yield);
}

void uart_dma_isr_tx_dma(uart_dma_t *dev) {
  uint32_t hisr = DMA2->HISR;
  uint32_t notify = 0u;
  BaseType_t yield = pdFALSE;

  if (hisr & (DMA_HISR_TEIF7 | DMA_HISR_DMEIF7 | DMA_HISR_FEIF7)) {
    DMA2->HIFCR = DMA_HIFCR_CTEIF7 | DMA_HIFCR_CDMEIF7 | DMA_HIFCR_CFEIF7;
    dev->tx_busy = false;
    dev->error_count++;
    notify |= UART_DMA_NOTIFY_ERROR;
  }

  if (hisr & DMA_HISR_TCIF7) {
    DMA2->HIFCR = DMA_HIFCR_CTCIF7;
    dev->tx_busy = false;
    notify |= UART_DMA_NOTIFY_TX_DONE;
  }

  if (notify != 0u && dev->notify_task != NULL) {
    xTaskNotifyFromISR(dev->notify_task, notify, eSetBits, &yield);
  }

  portYIELD_FROM_ISR(yield);
}

void USART1_IRQHandler(void) {
  uart_dma_isr_usart(&lora_uart);
}

void DMA2_Stream2_IRQHandler(void) {
  uart_dma_isr_rx_dma(&lora_uart);
}

void DMA2_Stream7_IRQHandler(void) {
  uart_dma_isr_tx_dma(&lora_uart);
}