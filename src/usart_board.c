#include "usart_board.h"

/* Same per-port RX/TX buffers as flight-logic; hardware mapping is TBD. */
static ring_buf lora_rx;
static ring_buf lora_tx;

/* TODO: select USART/DMA streams, channels and IRQs; supply clock_hz,
 * baud_rate, clock/GPIO callbacks and an optional peripheral-reset callback.
 * usart_init() computes BRR and sets DMA channels from these fields.
 * Missing hardware configuration fails without touching registers. */
usart_t lora_usart = {
    .rx = &lora_rx,
    .tx = &lora_tx,
};

usart_t *board_lora_usart_get(void) { return &lora_usart; }

void board_lora_control_init(void) {
  /* TODO: configure the selected board's radio boot/reset GPIOs. */
}

void board_lora_reset(bool asserted) {
  /* TODO: drive the selected board's radio reset signal. */
  (void)asserted;
}
