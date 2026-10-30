#ifndef USART_H_
#define USART_H_

#include "stm32f413xx.h"
#include "ring_buf.h"
#include <stdbool.h>

typedef struct usart usart_t;

struct usart {
  USART_TypeDef *regs;
  IRQn_Type irqn;
  ring_buf *rx;
  ring_buf *tx;

  uint32_t clock_hz;
  uint32_t baud_rate;

  /* Debug counters since initialization; updated by the UART ISR. */
  volatile uint32_t rx_bytes;
  volatile uint32_t tx_bytes;
  volatile uint32_t rx_error_count;
  volatile uint32_t rx_drop_count;

  void (*enable_clock)(void);
  void (*init_gpio)(void);
};

void usart_init(usart_t *port);
bool usart_write(usart_t *port, const uint8_t *data, uint32_t len);
bool usart_read(usart_t *port, uint8_t *data, uint32_t len);
void usart_handle_irq(usart_t *port);

void uart_5_enable_clock(void);
void usart_2_enable_clock(void);

#endif // USART_H_
