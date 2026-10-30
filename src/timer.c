#include "timer.h"

static volatile uint32_t ms_ticks = 0;
static volatile bool timer2_pending = false;
volatile uint32_t timer2_ticks = 0;

void timer2_init_1hz(void)
{
    NVIC_DisableIRQ(TIM2_IRQn);
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
    (void)RCC->APB1ENR;

    RCC->APB1RSTR |= RCC_APB1RSTR_TIM2RST;
    RCC->APB1RSTR &= ~RCC_APB1RSTR_TIM2RST;

    /* Current hardwareInit: HSI=16 MHz, APB1 prescaler=1.
     * Update rate = 16000000 / (15999 + 1) / (999 + 1) = 1 Hz.
     * Recalculate these divisors if the timer input clock changes.
     */
    TIM2->CR1 = TIM_CR1_URS | TIM_CR1_ARPE;
    TIM2->PSC = 15999U;
    TIM2->ARR = 999U;
    TIM2->EGR = TIM_EGR_UG; /* Load PSC/ARR before starting. */
    TIM2->SR = 0U;         /* Discard initialization update flags. */
    TIM2->CNT = 0U;

    timer2_pending = false;
    timer2_ticks = 0U;
    TIM2->DIER = TIM_DIER_UIE;
    NVIC_ClearPendingIRQ(TIM2_IRQn);
    NVIC_SetPriority(TIM2_IRQn, 10U);
    NVIC_EnableIRQ(TIM2_IRQn);
    TIM2->CR1 |= TIM_CR1_CEN;
}

void TIM2_IRQHandler(void)
{
    if (TIM2->SR & TIM_SR_UIF) {
        TIM2->SR = 0U; /* TIM2 is dedicated to update interrupts. */
        ++timer2_ticks;
        timer2_pending = true;
    }
}

bool timer2_take_tick(void)
{
    /* Atomically consume the flag so an ISR cannot set it between
     * a foreground read and clear. Multiple pending ticks coalesce.
     */
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    bool pending = timer2_pending;
    timer2_pending = false;
    __set_PRIMASK(primask);
    return pending;
}

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
