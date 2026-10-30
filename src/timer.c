#include "timer.h"

static volatile uint32_t ms_ticks = 0;

void SysTick_Handler(void)
{
    ++ms_ticks;
}

uint32_t now_ms(void)
{
    return ms_ticks;
}

void delay_ms(uint32_t duration)
{
    uint32_t start = now_ms();

    while ((uint32_t)(now_ms() - start) < duration) {
        // SysTick and UART interrupts continue running.
    }
}