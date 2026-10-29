#include "usart.h"
#include "FreeRTOSConfig.h"

#define DMA_FLAG_FE (1u << 0)
#define DMA_FLAG_DME (1u << 2)
#define DMA_FLAG_TE (1u << 3)
#define DMA_FLAG_HT (1u << 4)
#define DMA_FLAG_TC (1u << 5)
#define DMA_FLAGS (DMA_FLAG_FE | DMA_FLAG_DME | DMA_FLAG_TE | DMA_FLAG_HT | DMA_FLAG_TC)
#define DMA_ERRORS (DMA_FLAG_FE | DMA_FLAG_DME | DMA_FLAG_TE)
#define RX_PROGRESS (DMA_FLAG_HT | DMA_FLAG_TC)

static uint32_t flag_shift(uint8_t stream) {
  static const uint8_t shifts[] = {0, 6, 16, 22};
  return shifts[stream & 3u];
}

static uint32_t dma_status(usart_t *port, uint8_t stream) {
  uint32_t status = stream < 4u ? port->dma->LISR : port->dma->HISR;
  return (status >> flag_shift(stream)) & DMA_FLAGS;
}

static void clear_dma_flags(usart_t *port, uint8_t stream, uint32_t flags) {
  uint32_t mask = (flags & DMA_FLAGS) << flag_shift(stream);
  if (stream < 4u) port->dma->LIFCR = mask;
  else port->dma->HIFCR = mask;
}

_Static_assert((USART_RX_RING_SIZE & (USART_RX_RING_SIZE - 1u)) == 0u,
               "RX ring must be a power of two");

static bool dma_stream_disable(DMA_Stream_TypeDef *stream) {
  stream->CR &= ~DMA_SxCR_EN;
  for (uint32_t i = 0; i < 1000u; ++i) {
    if (!(stream->CR & DMA_SxCR_EN)) return true;
  }
  return false;
}

static void fault(usart_t *port, bool overflow) {
  if (!port->fault) {
    port->error_count++;
    if (overflow) port->overflow_count++;
  }
  port->fault = true;
}

/* Called with this UART's interrupts masked, or from equal-priority ISRs.
 * Clear only flags observed before NDTR: a later boundary remains pending.
 * If both boundaries accumulated, the number of laps is unknowable. */
static void snapshot_rx(usart_t *port) {
  if (!port || !port->initialized || !port->rx_running || port->fault) return;
  uint32_t flags = dma_status(port, port->rx_stream);
  clear_dma_flags(port, port->rx_stream, flags);
  uint32_t remaining = port->rx_dma->NDTR;
  if ((flags & DMA_ERRORS) || remaining > USART_RX_RING_SIZE) {
    fault(port, false);
    return;
  }
  if ((flags & RX_PROGRESS) == RX_PROGRESS) {
    fault(port, true);
    return;
  }
  uint16_t position = (USART_RX_RING_SIZE - remaining) &
                      (USART_RX_RING_SIZE - 1u);
  port->rx_producer += (position - port->rx_position) &
                      (USART_RX_RING_SIZE - 1u);
  port->rx_position = position;
  if (port->rx_producer - port->rx_consumer >= USART_RX_RING_SIZE)
    fault(port, true);
}

static void notify_isr(usart_t *port, uint32_t bits) {
  BaseType_t wake = pdFALSE;
  if (port->fault) bits |= USART_NOTIFY_ERROR;
  if (bits && port->notify_task)
    xTaskNotifyFromISR(port->notify_task, bits, eSetBits, &wake);
  portYIELD_FROM_ISR(wake);
}

static void configure_uart(usart_t *port) {
  port->regs->CR1 = 0u;
  port->regs->BRR = (port->clock_hz + port->baud_rate / 2u) / port->baud_rate;
  port->regs->CR2 = 0u;
  port->regs->CR3 = USART_CR3_EIE;
  (void)port->regs->SR;
  (void)port->regs->DR;
  port->regs->SR = ~(uint32_t)USART_SR_TC;
  port->regs->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE |
                    USART_CR1_PEIE;
}

static bool valid_config(const usart_t *port) {
  return port && port->regs && port->dma && port->rx_dma && port->tx_dma &&
      port->rx_dma != port->tx_dma && port->rx_stream < 8u &&
      port->tx_stream < 8u && port->rx_stream != port->tx_stream &&
      port->rx_channel < 8u && port->tx_channel < 8u &&
      port->clock_hz && port->baud_rate &&
      port->clock_hz / port->baud_rate >= 16u &&
      port->clock_hz / port->baud_rate <= 65535u &&
      port->irqn >= 0 && port->rx_dma_irqn >= 0 && port->tx_dma_irqn >= 0 &&
      port->irqn != port->rx_dma_irqn && port->irqn != port->tx_dma_irqn &&
      port->rx_dma_irqn != port->tx_dma_irqn &&
      port->irq_priority >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY &&
      port->irq_priority <= configLIBRARY_LOWEST_INTERRUPT_PRIORITY &&
      port->enable_clock && port->init_gpio;
}

bool usart_init(usart_t *port) {
  if (!valid_config(port)) return false;
  port->enable_clock();
  if (!usart_stop(port)) return false;
  port->init_gpio();
  if (port->reset_peripheral) port->reset_peripheral(port);
  configure_uart(port);
  port->fault = false;
  port->tx_dma_done = false;
  port->initialized = true;
  NVIC_SetPriority(port->irqn, port->irq_priority);
  NVIC_SetPriority(port->rx_dma_irqn, port->irq_priority);
  NVIC_SetPriority(port->tx_dma_irqn, port->irq_priority);
  NVIC_ClearPendingIRQ(port->irqn);
  NVIC_ClearPendingIRQ(port->rx_dma_irqn);
  NVIC_ClearPendingIRQ(port->tx_dma_irqn);
  NVIC_EnableIRQ(port->irqn);
  NVIC_EnableIRQ(port->rx_dma_irqn);
  NVIC_EnableIRQ(port->tx_dma_irqn);
  return true;
}

void usart_set_notify_task(usart_t *port, TaskHandle_t task) {
  if (port) port->notify_task = task;
}

bool usart_stop(usart_t *port) {
  if (!valid_config(port)) return false;
  NVIC_DisableIRQ(port->irqn);
  NVIC_DisableIRQ(port->rx_dma_irqn);
  NVIC_DisableIRQ(port->tx_dma_irqn);
  port->regs->CR3 = 0u;
  port->regs->CR1 = 0u;
  bool rx_stopped = dma_stream_disable(port->rx_dma);
  bool tx_stopped = dma_stream_disable(port->tx_dma);
  port->rx_running = false;
  clear_dma_flags(port, port->rx_stream, DMA_FLAGS);
  clear_dma_flags(port, port->tx_stream, DMA_FLAGS);
  NVIC_ClearPendingIRQ(port->irqn);
  NVIC_ClearPendingIRQ(port->rx_dma_irqn);
  NVIC_ClearPendingIRQ(port->tx_dma_irqn);
  if (!rx_stopped || !tx_stopped) {
    fault(port, false);
    return false;
  }
  port->tx_busy = false;
  port->tx_dma_done = false;
  port->initialized = false;
  port->rx_producer = port->rx_consumer = port->rx_position = 0u;
  return true;
}

bool usart_recover(usart_t *port) {
  if (!usart_init(port)) return false;
  return usart_start_rx(port);
}

bool usart_start_rx(usart_t *port) {
  if (!port || !port->initialized || port->fault) return false;
  taskENTER_CRITICAL();
  bool stopped = dma_stream_disable(port->rx_dma);
  if (stopped) {
    clear_dma_flags(port, port->rx_stream, DMA_FLAGS);
    port->rx_dma->CR = ((uint32_t)port->rx_channel << DMA_SxCR_CHSEL_Pos) | DMA_SxCR_PL |
        DMA_SxCR_CIRC | DMA_SxCR_MINC | DMA_SxCR_TCIE | DMA_SxCR_HTIE |
        DMA_SxCR_TEIE | DMA_SxCR_DMEIE;
    port->rx_dma->FCR = DMA_SxFCR_FEIE;
    port->rx_dma->NDTR = USART_RX_RING_SIZE;
    port->rx_dma->PAR = (uint32_t)(uintptr_t)&port->regs->DR;
    port->rx_dma->M0AR = (uint32_t)(uintptr_t)port->rx_ring;
    port->rx_producer = port->rx_consumer = port->rx_position = 0u;
    (void)port->regs->SR;
    (void)port->regs->DR;
    port->rx_running = true;
    port->regs->CR3 |= USART_CR3_DMAR;
    __DMB();
    port->rx_dma->CR |= DMA_SxCR_EN;
  } else {
    fault(port, false);
  }
  taskEXIT_CRITICAL();
  return stopped;
}

bool usart_start_tx(usart_t *port, const uint8_t *buf, uint16_t len) {
  if (!port || !port->initialized || !buf || !len || usart_tx_busy(port) || port->fault) return false;
  taskENTER_CRITICAL();
  /* An error IRQ could have arrived between the initial check and masking. */
  if (port->fault || port->tx_busy) {
    taskEXIT_CRITICAL();
    return false;
  }
  bool stopped = dma_stream_disable(port->tx_dma);
  if (stopped) {
    port->regs->CR1 &= ~USART_CR1_TCIE;
    port->regs->CR3 &= ~USART_CR3_DMAT;
    clear_dma_flags(port, port->tx_stream, DMA_FLAGS);
    port->tx_dma->CR = ((uint32_t)port->tx_channel << DMA_SxCR_CHSEL_Pos) | DMA_SxCR_PL_1 |
        DMA_SxCR_DIR_0 | DMA_SxCR_MINC | DMA_SxCR_TCIE |
        DMA_SxCR_TEIE | DMA_SxCR_DMEIE;
    port->tx_dma->FCR = DMA_SxFCR_FEIE;
    port->tx_dma->NDTR = len;
    port->tx_dma->PAR = (uint32_t)(uintptr_t)&port->regs->DR;
    port->tx_dma->M0AR = (uint32_t)(uintptr_t)buf;
    port->regs->SR = ~(uint32_t)USART_SR_TC;
    port->tx_dma_done = false;
    port->tx_busy = true;
    __DMB();
    port->tx_dma->CR |= DMA_SxCR_EN;
    port->regs->CR3 |= USART_CR3_DMAT;
  } else {
    fault(port, false);
  }
  taskEXIT_CRITICAL();
  return stopped;
}

uint16_t usart_rx_available(usart_t *port) {
  if (!port || !port->initialized) return 0u;
  taskENTER_CRITICAL();
  snapshot_rx(port);
  uint16_t n = port->fault ? 0u : (uint16_t)(port->rx_producer - port->rx_consumer);
  taskEXIT_CRITICAL();
  return n;
}

uint16_t usart_rx_read(usart_t *port, uint8_t *dst, uint16_t max) {
  if (!port || !port->initialized || !dst) return 0u;
  if (max > USART_RX_READ_MAX) max = USART_RX_READ_MAX;
  taskENTER_CRITICAL();
  snapshot_rx(port);
  uint32_t n = port->fault ? 0u : port->rx_producer - port->rx_consumer;
  if (n > max) n = max;
  for (uint32_t i = 0; i < n; ++i)
    dst[i] = ((volatile uint8_t *)port->rx_ring)[(port->rx_consumer + i) &
                                             (USART_RX_RING_SIZE - 1u)];
  /* DMA keeps running during the copy. Reject a copy if it was overtaken. */
  snapshot_rx(port);
  if (port->fault) n = 0u;
  port->rx_consumer += n;
  taskEXIT_CRITICAL();
  return (uint16_t)n;
}

void usart_rx_reset(usart_t *port) {
  if (!port || !port->initialized) return;
  taskENTER_CRITICAL();
  snapshot_rx(port);
  port->rx_consumer = port->rx_producer;
  taskEXIT_CRITICAL();
}

void usart_poll(usart_t *port) {
  if (!port || !port->initialized) return;
  taskENTER_CRITICAL();
  /* Catch receive errors before acknowledging TX, even if its IRQ is pending. */
  if (port->regs->SR & (USART_SR_ORE | USART_SR_FE | USART_SR_NE | USART_SR_PE)) {
    (void)port->regs->DR;
    port->regs->CR3 &= ~(USART_CR3_DMAR | USART_CR3_DMAT | USART_CR3_EIE);
    port->regs->CR1 &= ~USART_CR1_PEIE;
    fault(port, false);
  }
  snapshot_rx(port);
  if (port->tx_busy && port->tx_dma_done && !port->fault &&
      !(port->tx_dma->CR & DMA_SxCR_EN) && (port->regs->SR & USART_SR_TC)) {
    port->tx_busy = false;
    port->tx_dma_done = false;
  }
  taskEXIT_CRITICAL();
}

bool usart_tx_busy(usart_t *port) {
  usart_poll(port);
  return port && port->tx_busy;
}
bool usart_faulted(const usart_t *port) { return port && port->fault; }
uint32_t usart_error_count(const usart_t *port) { return port ? port->error_count : 0u; }

void usart_handle_irq(usart_t *port) {
  if (!port || !port->initialized) return;
  uint32_t sr = port->regs->SR;
  if (sr & (USART_SR_ORE | USART_SR_FE | USART_SR_NE | USART_SR_PE)) {
    /* Error IRQ only. USART RXNEIE, TXEIE, TCIE and IDLEIE stay disabled. */
    (void)port->regs->DR;
    port->regs->CR3 &= ~(USART_CR3_DMAR | USART_CR3_DMAT | USART_CR3_EIE);
    port->regs->CR1 &= ~USART_CR1_PEIE;
    fault(port, false);
    notify_isr(port, USART_NOTIFY_ERROR);
  }
}

void usart_handle_rx_dma_irq(usart_t *port) {
  if (!port || !port->initialized) return;
  snapshot_rx(port);
  /* A faulted stream must not keep interrupting while the task recovers. */
  if (port->fault) {
    port->regs->CR3 &= ~USART_CR3_DMAR;
    port->rx_dma->CR &= ~(DMA_SxCR_HTIE | DMA_SxCR_TCIE | DMA_SxCR_TEIE | DMA_SxCR_DMEIE);
    port->rx_dma->FCR &= ~DMA_SxFCR_FEIE;
    clear_dma_flags(port, port->rx_stream, DMA_FLAGS);
  }
  notify_isr(port, USART_NOTIFY_RX_DATA);
}

void usart_handle_tx_dma_irq(usart_t *port) {
  if (!port || !port->initialized) return;
  uint32_t flags = dma_status(port, port->tx_stream);
  clear_dma_flags(port, port->tx_stream, flags);
  if (flags & DMA_ERRORS) {
    port->regs->CR3 &= ~USART_CR3_DMAT;
    port->regs->CR1 &= ~USART_CR1_TCIE;
    port->tx_dma->CR &= ~(DMA_SxCR_TCIE | DMA_SxCR_TEIE | DMA_SxCR_DMEIE);
    port->tx_dma->FCR &= ~DMA_SxFCR_FEIE;
    fault(port, false);
    /* Keep buffer owned until task-context abort confirms DMA stopped. */
  } else if ((flags & DMA_FLAG_TC) && port->tx_busy && !port->fault) {
    port->regs->CR3 &= ~USART_CR3_DMAT;
    /* DMA released memory; task polling waits for the last stop bit (TC). */
    port->tx_dma_done = true;
  }
  notify_isr(port, port->tx_dma_done ? USART_NOTIFY_TX_DONE : 0u);
}

