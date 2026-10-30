#ifndef TIMER_H_
#define TIMER_H_

#include <stdint.h>
#include <stdbool.h>
#include "stm32f413xx.h"

uint32_t now_ms(void);
void delay_ms(uint32_t duration);

/* 1 Hz TIM2 scheduler for the current 16 MHz APB1 timer clock. */
void timer2_init_1hz(void);
bool timer2_take_tick(void);
extern volatile uint32_t timer2_ticks;
#endif // TIMER_H_
