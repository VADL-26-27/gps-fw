#ifndef USART_BOARD_H_
#define USART_BOARD_H_

#include "usart.h"

/* Board mapping is intentionally unset until the target board is selected. */
extern usart_t lora_usart;
usart_t *board_lora_usart_get(void);
void board_lora_control_init(void);
void board_lora_reset(bool asserted);

#endif // USART_BOARD_H_
