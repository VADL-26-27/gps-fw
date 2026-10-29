#include "usart_board.h"

/* TODO: select USART, DMA controller/streams/channels and IRQs, provide the
 * peripheral clock and baud rate, and implement clock/GPIO/reset callbacks.
 * A zero configuration makes usart_init() return false without touching HW.
 * Do not assign board pins or assume a USART/DMA mapping here yet. */
usart_t lora_usart = {0};

usart_t *board_lora_usart_get(void) { return &lora_usart; }

void board_lora_control_init(void) {
  /* TODO: configure the selected board's radio boot/reset GPIOs. */
}

void board_lora_reset(bool asserted) {
  /* TODO: drive the selected board's radio reset signal. */
  (void)asserted;
}
