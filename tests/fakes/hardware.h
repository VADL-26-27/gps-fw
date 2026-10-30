#ifndef TEST_HARDWARE_H
#define TEST_HARDWARE_H
#include "stm32f4xx.h"
/* Use the real device types and bit definitions, with RAM register substitutes.
 * Tests explicitly emulate write-one-to-clear flags between driver calls. */
extern USART_TypeDef test_usart;
extern DMA_Stream_TypeDef test_rx_dma, test_tx_dma;
extern DMA_TypeDef test_dma;
extern RCC_TypeDef test_rcc;
extern GPIO_TypeDef test_gpioa, test_gpioc;
#undef USART1
#undef DMA2_Stream2
#undef DMA2_Stream7
#undef DMA2
#undef RCC
#undef GPIOA
#undef GPIOC
#define USART1 (&test_usart)
#define DMA2_Stream2 (&test_rx_dma)
#define DMA2_Stream7 (&test_tx_dma)
#define DMA2 (&test_dma)
#define RCC (&test_rcc)
#define GPIOA (&test_gpioa)
#define GPIOC (&test_gpioc)
#undef NVIC_SetPriority
#undef NVIC_EnableIRQ
#undef NVIC_DisableIRQ
#undef NVIC_ClearPendingIRQ
#define NVIC_SetPriority(irq, priority) ((void)(irq), (void)(priority))
#define NVIC_EnableIRQ(irq) ((void)(irq))
#define NVIC_DisableIRQ(irq) ((void)(irq))
#define NVIC_ClearPendingIRQ(irq) ((void)(irq))
#define __DMB() ((void)0)
#endif
