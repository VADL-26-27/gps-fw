#include "usart.h"
#include "usart_board.h"

void UART5_IRQHandler(void)
{
    usart_handle_irq(&uart5);
}

void USART2_IRQHandler(void)
{
    usart_handle_irq(&uart2);
}
