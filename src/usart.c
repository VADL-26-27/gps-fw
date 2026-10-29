#include "usart.h"
#include "FreeRTOSConfig.h"

_Static_assert((BUFFER_SIZE & (BUFFER_SIZE - 1u)) == 0u && BUFFER_SIZE <= 65535u,
               "Ring size must be a DMA-sized power of two");

#define DMA_FLAG_FE (1u << 0)
#define DMA_FLAG_DME (1u << 2)
#define DMA_FLAG_TE (1u << 3)
#define DMA_FLAG_HT (1u << 4)
#define DMA_FLAG_TC (1u << 5)
#define DMA_FLAGS (DMA_FLAG_FE | DMA_FLAG_DME | DMA_FLAG_TE | DMA_FLAG_HT | DMA_FLAG_TC)
#define DMA_ERRORS (DMA_FLAG_FE | DMA_FLAG_DME | DMA_FLAG_TE)

static uint32_t now_ms(void) {
  return (uint32_t)((uint64_t)xTaskGetTickCount() * 1000u / configTICK_RATE_HZ);
}

/* STM32F4 stream registers follow the DMA status registers at a 24-byte stride.
 * Derive the flag bank/index from the pointers instead of storing duplicates. */
static int stream_index(const usart_t *port, const DMA_Stream_TypeDef *stream) {
  if (!port || !port->dma || !stream) return -1;
  uintptr_t first = (uintptr_t)port->dma + sizeof(DMA_TypeDef);
  uintptr_t address = (uintptr_t)stream;
  if (address < first) return -1;
  uintptr_t offset = address - first;
  if (offset % sizeof(DMA_Stream_TypeDef) ||
      offset / sizeof(DMA_Stream_TypeDef) >= 8u) return -1;
  return (int)(offset / sizeof(DMA_Stream_TypeDef));
}

static uint32_t flag_shift(unsigned stream) {
  static const uint8_t shifts[] = {0, 6, 16, 22};
  return shifts[stream & 3u];
}

static uint32_t dma_status(usart_t *port, DMA_Stream_TypeDef *stream) {
  unsigned index = (unsigned)stream_index(port, stream);
  uint32_t status = index < 4u ? port->dma->LISR : port->dma->HISR;
  return (status >> flag_shift(index)) & DMA_FLAGS;
}

static void clear_flags(usart_t *port, DMA_Stream_TypeDef *stream, uint32_t flags) {
  unsigned index = (unsigned)stream_index(port, stream);
  uint32_t mask = (flags & DMA_FLAGS) << flag_shift(index);
  if (index < 4u) port->dma->LIFCR = mask;
  else port->dma->HIFCR = mask;
}

static bool valid_port(const usart_t *port) {
  return port && port->regs && port->rx && port->tx && port->rx != port->tx &&
      port->clock_hz && port->baud_rate &&
      port->clock_hz / port->baud_rate >= 16u &&
      ((uint64_t)port->clock_hz + port->baud_rate / 2u) / port->baud_rate <= 65535u &&
      port->tx_channel < 8u && port->rx_channel < 8u && stream_index(port, port->rx_dma) >= 0 &&
      stream_index(port, port->tx_dma) >= 0 && port->tx_dma != port->rx_dma &&
      port->irqn >= 0 && port->rx_dma_irqn >= 0 && port->tx_dma_irqn >= 0 &&
      port->irqn != port->rx_dma_irqn && port->irqn != port->tx_dma_irqn &&
      port->rx_dma_irqn != port->tx_dma_irqn;
}

static bool ready(const usart_t *port) {
  return valid_port(port) && (port->regs->CR1 & USART_CR1_UE);
}

static bool disable_stream(DMA_Stream_TypeDef *stream) {
  stream->CR &= ~DMA_SxCR_EN;
  for (unsigned i = 0; i < 1000u; ++i)
    if (!(stream->CR & DMA_SxCR_EN)) return true;
  return false;
}

static void fail(usart_t *port) {
  port->regs->CR3 &= ~(USART_CR3_DMAT | USART_CR3_DMAR | USART_CR3_EIE);
  port->regs->CR1 &= ~USART_CR1_PEIE;
  port->tx_dma->CR &= ~(DMA_SxCR_TCIE | DMA_SxCR_TEIE | DMA_SxCR_DMEIE);
  port->rx_dma->CR &= ~(DMA_SxCR_TCIE | DMA_SxCR_HTIE | DMA_SxCR_TEIE | DMA_SxCR_DMEIE);
  port->tx_dma->FCR &= ~DMA_SxFCR_FEIE;
  port->rx_dma->FCR &= ~DMA_SxFCR_FEIE;
  port->tx_result = port->rx_result = USART_RESULT_FAILED;
  /* Busy flags retain buffer ownership until a successful stop. */
}

bool usart_stop(usart_t *port) {
  if (!valid_port(port)) return false;
  NVIC_DisableIRQ(port->irqn);
  NVIC_DisableIRQ(port->tx_dma_irqn);
  NVIC_DisableIRQ(port->rx_dma_irqn);
  port->regs->CR3 = 0;
  port->regs->CR1 = 0;
  bool tx_stopped = disable_stream(port->tx_dma);
  bool rx_stopped = disable_stream(port->rx_dma);
  if (!tx_stopped || !rx_stopped) { fail(port); return false; }
  clear_flags(port, port->tx_dma, DMA_FLAGS);
  clear_flags(port, port->rx_dma, DMA_FLAGS);
  NVIC_ClearPendingIRQ(port->irqn);
  NVIC_ClearPendingIRQ(port->tx_dma_irqn);
  NVIC_ClearPendingIRQ(port->rx_dma_irqn);
  port->tx_busy = port->rx_busy = false;
  port->tx_dma_done = port->rx_dma_done = false;
  port->tx_result = port->rx_result = USART_RESULT_NONE;
  return true;
}

bool usart_init(usart_t *port) {
  if (!valid_port(port) || !port->enable_clock || !port->init_gpio) return false;
  port->enable_clock();
  if (!usart_stop(port)) return false;
  port->init_gpio();
  if (port->reset_peripheral) port->reset_peripheral(port);
  init_buf(port->rx);
  init_buf(port->tx);
  port->rx_position = port->tx_dma_len = 0;
  port->regs->BRR = (uint32_t)(((uint64_t)port->clock_hz +
                             port->baud_rate / 2u) / port->baud_rate);
  port->regs->CR1 = 0;
  port->regs->CR2 = 0;
  port->regs->CR3 = USART_CR3_EIE;
  (void)port->regs->SR;
  (void)port->regs->DR;
  port->regs->SR = ~(uint32_t)USART_SR_TC;
  port->regs->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_PEIE;
  NVIC_SetPriority(port->irqn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);
  NVIC_SetPriority(port->tx_dma_irqn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);
  NVIC_SetPriority(port->rx_dma_irqn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);
  clear_flags(port, port->rx_dma, DMA_FLAGS);
  port->rx_dma->CR = ((uint32_t)port->rx_channel << DMA_SxCR_CHSEL_Pos) |
      DMA_SxCR_CIRC | DMA_SxCR_MINC | DMA_SxCR_HTIE | DMA_SxCR_TCIE |
      DMA_SxCR_TEIE | DMA_SxCR_DMEIE;
  port->rx_dma->FCR = DMA_SxFCR_FEIE;
  port->rx_dma->PAR = (uint32_t)(uintptr_t)&port->regs->DR;
  port->rx_dma->M0AR = (uint32_t)(uintptr_t)port->rx->buf;
  port->rx_dma->NDTR = BUFFER_SIZE;
  port->rx_busy = true;
  port->rx_result = USART_RESULT_PENDING;
  port->rx_start_ms = now_ms();
  __DMB();
  port->rx_dma->CR |= DMA_SxCR_EN;
  port->regs->CR3 |= USART_CR3_DMAR;
  port->regs->CR1 |= USART_CR1_UE;
  NVIC_EnableIRQ(port->irqn);
  NVIC_EnableIRQ(port->tx_dma_irqn);
  NVIC_EnableIRQ(port->rx_dma_irqn);
  return true;
}

bool usart_recover(usart_t *port) { return usart_init(port); }

bool usart_faulted(const usart_t *port) {
  return port && (port->tx_result == USART_RESULT_FAILED ||
                  port->rx_result == USART_RESULT_FAILED);
}

/* DMA is the producer; read() is the consumer. Both task and IRQ paths
 * serialize updates, while DMA itself continues writing the byte array. */
static void rx_progress(usart_t *port) {
  if (!port->rx_busy || usart_faulted(port)) return;
  uint32_t flags = dma_status(port, port->rx_dma);
  clear_flags(port, port->rx_dma, flags);
  uint32_t remaining = port->rx_dma->NDTR;
  if ((flags & DMA_ERRORS) || remaining > BUFFER_SIZE ||
      (flags & (DMA_FLAG_HT | DMA_FLAG_TC)) == (DMA_FLAG_HT | DMA_FLAG_TC)) {
    fail(port);
    return;
  }
  uint16_t position = (BUFFER_SIZE - remaining) & (BUFFER_SIZE - 1u);
  uint32_t delta = (position - port->rx_position) & (BUFFER_SIZE - 1u);
  if ((flags & DMA_FLAG_TC) && position >= port->rx_position) delta += BUFFER_SIZE;
  uint32_t count = atomic_load_explicit(&port->rx->count, memory_order_acquire);
  if (count + delta >= BUFFER_SIZE) { fail(port); return; }
  port->rx_position = position;
  atomic_store_explicit(&port->rx->write, position, memory_order_release);
  atomic_fetch_add_explicit(&port->rx->count, delta, memory_order_release);
  if (flags & DMA_FLAG_TC) port->rx_dma_done = true;
}

/* Queue bytes remain counted until the DMA segment and last stop bit finish,
 * so a later write cannot overwrite storage still being transmitted. */
static void tx_kick(usart_t *port) {
  uint32_t count = atomic_load_explicit(&port->tx->count, memory_order_acquire);
  if (!count || port->tx_busy || usart_faulted(port)) return;
  uint32_t read = atomic_load_explicit(&port->tx->read, memory_order_acquire);
  uint32_t n = BUFFER_SIZE - read;
  if (n > count) n = count;
  if (!disable_stream(port->tx_dma)) { fail(port); return; }
  clear_flags(port, port->tx_dma, DMA_FLAGS);
  port->tx_dma->CR = ((uint32_t)port->tx_channel << DMA_SxCR_CHSEL_Pos) |
      DMA_SxCR_MINC | DMA_SxCR_DIR_0 | DMA_SxCR_TCIE | DMA_SxCR_TEIE | DMA_SxCR_DMEIE;
  port->tx_dma->FCR = DMA_SxFCR_FEIE;
  port->tx_dma->PAR = (uint32_t)(uintptr_t)&port->regs->DR;
  port->tx_dma->M0AR = (uint32_t)(uintptr_t)&port->tx->buf[read];
  port->tx_dma->NDTR = n;
  port->regs->SR = ~(uint32_t)USART_SR_TC;
  port->tx_dma_len = (uint16_t)n;
  port->tx_dma_done = false;
  port->tx_busy = true;
  port->tx_result = USART_RESULT_PENDING;
  port->tx_start_ms = now_ms();
  __DMB();
  port->tx_dma->CR |= DMA_SxCR_EN;
  port->regs->CR3 |= USART_CR3_DMAT;
}

void usart_poll(usart_t *port) {
  if (!ready(port)) return;
  taskENTER_CRITICAL();
  if (port->regs->SR & (USART_SR_ORE | USART_SR_FE | USART_SR_NE | USART_SR_PE)) {
    (void)port->regs->DR;
    fail(port);
  }
  if (!usart_faulted(port)) {
    rx_progress(port);
    uint32_t flags = dma_status(port, port->tx_dma);
    clear_flags(port, port->tx_dma, flags);
    if (flags & DMA_ERRORS) fail(port);
    else if ((flags & DMA_FLAG_TC) && port->tx_busy) {
      port->regs->CR3 &= ~USART_CR3_DMAT;
      port->tx_dma_done = true;
    }
    if (!usart_faulted(port) && port->tx_busy && port->tx_dma_done &&
        !(port->tx_dma->CR & DMA_SxCR_EN) && (port->regs->SR & USART_SR_TC)) {
      uint32_t read = atomic_load_explicit(&port->tx->read, memory_order_acquire);
      atomic_store_explicit(&port->tx->read, (read + port->tx_dma_len) &
                            (BUFFER_SIZE - 1u), memory_order_release);
      atomic_fetch_sub_explicit(&port->tx->count, port->tx_dma_len, memory_order_release);
      port->tx_busy = false;
      port->tx_result = USART_RESULT_OK;
      tx_kick(port);
    }
    uint32_t now = now_ms();
    if (!usart_faulted(port) &&
        (port->tx_busy && (uint32_t)(now - port->tx_start_ms) >= USART_TRANSFER_TIMEOUT_MS)) fail(port);
  }
  taskEXIT_CRITICAL();
}

bool usart_tx_busy(usart_t *port) {
  usart_poll(port);
  return port && port->tx_busy;
}

bool usart_write(usart_t *port, const uint8_t *data, uint32_t len) {
  if (!ready(port) || (!data && len)) return false;
  usart_poll(port);
  taskENTER_CRITICAL();
  uint32_t count = atomic_load_explicit(&port->tx->count, memory_order_acquire);
  if (usart_faulted(port) || len > BUFFER_SIZE - count) {
    taskEXIT_CRITICAL();
    return false;
  }
  for (uint32_t i = 0; i < len; ++i) enqueue(port->tx, data[i]);
  tx_kick(port);
  bool ok = !usart_faulted(port);
  taskEXIT_CRITICAL();
  return ok;
}

uint16_t usart_rx_available(usart_t *port) {
  if (!ready(port)) return 0;
  usart_poll(port);
  taskENTER_CRITICAL();
  rx_progress(port);
  uint16_t n = usart_faulted(port) ? 0 :
      (uint16_t)atomic_load_explicit(&port->rx->count, memory_order_acquire);
  taskEXIT_CRITICAL();
  return n;
}

bool usart_read(usart_t *port, uint8_t *data, uint32_t len) {
  if (!ready(port) || (!data && len)) return false;
  taskENTER_CRITICAL();
  rx_progress(port);
  uint32_t count = atomic_load_explicit(&port->rx->count, memory_order_acquire);
  if (usart_faulted(port) || len > count) {
    taskEXIT_CRITICAL();
    return false;
  }
  uint32_t read = atomic_load_explicit(&port->rx->read, memory_order_acquire);
  for (uint32_t i = 0; i < len; ++i)
    data[i] = ((volatile uint8_t *)port->rx->buf)[(read + i) & (BUFFER_SIZE - 1u)];
  /* Reject a copy overtaken by DMA; don't consume any bytes on failure. */
  rx_progress(port);
  bool ok = !usart_faulted(port);
  if (ok) {
    atomic_store_explicit(&port->rx->read, (read + len) & (BUFFER_SIZE - 1u), memory_order_release);
    atomic_fetch_sub_explicit(&port->rx->count, len, memory_order_release);
  }
  taskEXIT_CRITICAL();
  return ok;
}

void usart_rx_reset(usart_t *port) {
  if (!ready(port)) return;
  taskENTER_CRITICAL();
  rx_progress(port);
  atomic_store_explicit(&port->rx->read, port->rx_position, memory_order_release);
  atomic_store_explicit(&port->rx->count, 0, memory_order_release);
  taskEXIT_CRITICAL();
}

void usart_handle_irq(usart_t *port) {
  if (!ready(port)) return;
  if (port->regs->SR & (USART_SR_ORE | USART_SR_FE | USART_SR_NE | USART_SR_PE)) {
    (void)port->regs->DR;
    fail(port);
  }
}

void usart_handle_rx_dma_irq(usart_t *port) {
  if (!ready(port)) return;
  rx_progress(port);
  if (usart_faulted(port)) clear_flags(port, port->rx_dma, DMA_FLAGS);
}

void usart_handle_tx_dma_irq(usart_t *port) {
  if (!ready(port)) return;
  uint32_t flags = dma_status(port, port->tx_dma);
  clear_flags(port, port->tx_dma, flags);
  if (flags & DMA_ERRORS) fail(port);
  else if ((flags & DMA_FLAG_TC) && port->tx_busy && !usart_faulted(port)) {
    port->regs->CR3 &= ~USART_CR3_DMAT;
    port->tx_dma_done = true;
  }
}
