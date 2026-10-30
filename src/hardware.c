#include "hardware.h"
#include "timer.h"

void ledInit(void) {
  // configure PA5 for output push/pull
  GPIOA->MODER |= (1U << (0 * 2));

  // set push/pull
  GPIOA->OTYPER &= ~(1U << 0);

  // set PU/PD to off
  GPIOA->PUPDR &= ~(3U << (0 * 2));

  GPIOA->BSRR = (1U << (16 + 0));
  GPIOA->BSRR |= (1U << 0);
}

void timer_init(void) {
  SystemCoreClockUpdate();

  if (SysTick_Config(SystemCoreClock / 1000U) != 0U) {
    while (1) {
    }
  }

  NVIC_SetPriority(SysTick_IRQn, 15U);
}

void clock_init_hse(void) {
  RCC->CR &= ~RCC_CR_HSEBYP;
  RCC->CR |= RCC_CR_HSEON;

  uint32_t timeout = 1000000U;

  while (!(RCC->CR & RCC_CR_HSERDY)) {
    if (--timeout == 0U) {
      return; // SYSCLK Remains HSI
    }
  }

  // Select HSE as system clock
  RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_HSE;

  timeout = 1000000U;

  while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSE) {
    if (--timeout == 0U) {
      return;
    }
  }

  SystemCoreClockUpdate();
}

static void clock_init_hsi(void) {
  /* Explicitly select HSI, including when entered after a debug restart
   * that left a different system clock selected.
   */
  RCC->CR |= RCC_CR_HSION;
  while (!(RCC->CR & RCC_CR_HSIRDY)) {
  }

  RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_HSI;
  while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSI) {
  }

  /* HCLK, PCLK1 and PCLK2 are all 16 MHz for this UART test. */
  RCC->CFGR &= ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2);
  SystemCoreClockUpdate();
}

void hardwareInit(void) {
  // Enable clock
  RCC->AHB1ENR |=
      RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN;
  RCC->AHB1LPENR |= RCC_AHB1LPENR_GPIOALPEN | RCC_AHB1LPENR_GPIOBLPEN |
                    RCC_AHB1LPENR_GPIOCLPEN;

  (void)RCC->AHB1ENR;
  (void)RCC->AHB1LPENR;

  clock_init_hsi();

  timer_init();
  timer2_init_1hz();
  // -- LED --
  ledInit();
}
