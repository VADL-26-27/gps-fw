#include "usart.h"

void uart_5_enable_clock(void) {
  RCC->APB1ENR |= RCC_APB1ENR_UART5EN;
  RCC->APB1LPENR |= RCC_APB1LPENR_UART5LPEN;
}

void usart_2_enable_clock(void) {
  RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
  RCC->APB1LPENR |= RCC_APB1LPENR_USART2LPEN;
}

void usart_init(usart_t *port) {
  init_buf(port->rx);
  init_buf(port->tx);
  port->rx_bytes = 0U;
  port->tx_bytes = 0U;
  port->rx_error_count = 0U;
  port->rx_drop_count = 0U;

  port->enable_clock();
  port->init_gpio();

  port->regs->CR1 = 0;
  port->regs->CR2 = 0;
  port->regs->CR3 = 0;

  port->regs->BRR = (port->clock_hz + port->baud_rate / 2U) / port->baud_rate;

  NVIC_ClearPendingIRQ(port->irqn);
  NVIC_EnableIRQ(port->irqn);

  port->regs->CR1 =
      USART_CR1_TE | USART_CR1_RE | USART_CR1_UE | USART_CR1_RXNEIE;
}

void usart_handle_irq(usart_t *port) {
  uint32_t status = port->regs->SR;

  // Receive data and clear receive errors using the SR/DR sequence.
  if (status & (USART_SR_RXNE | USART_SR_FE | USART_SR_NE | USART_SR_ORE)) {
    uint8_t byte = (uint8_t)port->regs->DR;

    if (status & (USART_SR_FE | USART_SR_NE | USART_SR_ORE)) {
      ++port->rx_error_count;
    }

    if (status & USART_SR_RXNE) {
      ++port->rx_bytes;
      uint32_t count =
          atomic_load_explicit(&port->rx->count, memory_order_acquire);

      if (count < BUFFER_SIZE) {
        enqueue(port->rx, byte);
      } else {
        ++port->rx_drop_count;
      }
      // If full, discard the incoming byte.
      // Optionally record an overflow counter here.
    }
  }

  // Send the next queued byte.
  if ((status & USART_SR_TXE) && (port->regs->CR1 & USART_CR1_TXEIE)) {
    uint32_t count =
        atomic_load_explicit(&port->tx->count, memory_order_acquire);

    if (count != 0U) {
      uint8_t byte;
      read_from_buf(port->tx, &byte, 1U);
      port->regs->DR = byte;
      ++port->tx_bytes;
    }

    if (atomic_load_explicit(&port->tx->count, memory_order_acquire) == 0U) {
      port->regs->CR1 &= ~USART_CR1_TXEIE;
    }
  }
}

bool usart_write(usart_t *port, const uint8_t *data, uint32_t len) {
  uint32_t primask = __get_PRIMASK();
  __disable_irq();

  uint32_t count = atomic_load_explicit(&port->tx->count, memory_order_acquire);

  if (len > BUFFER_SIZE - count) {
    __set_PRIMASK(primask);
    return false;
  }

  for (uint32_t i = 0; i < len; ++i) {
    enqueue(port->tx, data[i]);
  }

  if (len != 0U) {
    port->regs->CR1 |= USART_CR1_TXEIE;
  }

  __set_PRIMASK(primask);
  return true;
}

bool usart_read(usart_t *port, uint8_t *data, uint32_t len) {
  uint32_t primask = __get_PRIMASK();
  __disable_irq();

  uint32_t available =
      atomic_load_explicit(&port->rx->count, memory_order_acquire);

  if (len > available) {
    __set_PRIMASK(primask);
    return false;
  }

  if (len != 0U) {
    read_from_buf(port->rx, data, len);
  }

  __set_PRIMASK(primask);
  return true;
}
