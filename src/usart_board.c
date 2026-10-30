#include "usart_board.h"

// Each UART gets separate RX and TX buffers.
static ring_buf uart5_rx;
static ring_buf uart5_tx;
static ring_buf uart2_rx;
static ring_buf uart2_tx;

static void hardwareUsart2Init(void) {
  // Clear PA2/PA3, Make PA2/PA3 Alternate Function Mode
  GPIOA->MODER &= ~((3U << 2 * 2) | (3U << 3 * 2));
  GPIOA->MODER |= ((2U << 2 * 2) | (2U << 3 * 2));

  // Clear PA2/PA3 Alternate Functions, Make PA2/PA3 Alternate Functions USART2
  // (AF7)
  GPIOA->AFR[0] &= ~((0xFU << 2 * 4) | (0xFU << 3 * 4));
  GPIOA->AFR[0] |= ((7U << 2 * 4) | (7U << 3 * 4));

  // High Speed for PA2/PA3
  GPIOA->OSPEEDR |= ((3U << 2 * 2) | (3U << 3 * 2));

  // Push PULL TX (PA2)
  GPIOA->OTYPER &= ~(1U << 2);

  // No pull up, no pull down
  GPIOA->PUPDR &= ~((3U << 2 * 2) | (3U << 3 * 2));
}

static void hardwareUart5Init(void) {
  // STM32F413 UART5 RAK: PB13 TX and PB12 RX, alternate function 11.
  GPIOB->MODER &= ~((3U << (12 * 2)) | (3U << (13 * 2)));
  GPIOB->MODER |= (2U << (12 * 2)) | (2U << (13 * 2));

  GPIOB->AFR[1] &= ~((0xFU << (4 * 4)) | (0xFU << (4 * 5)));
  GPIOB->AFR[1] |= (11U << (4 * 4)) | (11U << (4 * 5));

  GPIOB->OSPEEDR |= (3U << (13 * 2)) | (3U << (12 * 2));

  GPIOB->OTYPER &= ~(1U << 13);

  GPIOB->PUPDR &= ~((3U << (12 * 2)) | (3U << (13 * 2)));
}

// The callback functions must be declared before these initializers.
usart_t uart5 = {
    .regs = UART5,
    .irqn = UART5_IRQn,
    .clock_hz = 16000000U,
    .baud_rate = 115200U,
    .rx = &uart5_rx,
    .tx = &uart5_tx,
    .enable_clock = uart_5_enable_clock,
    .init_gpio = hardwareUart5Init,
};

usart_t uart2 = {
    .regs = USART2,
    .irqn = USART2_IRQn,
    .clock_hz = 16000000U,
    .baud_rate = 115200U,
    .rx = &uart2_rx,
    .tx = &uart2_tx,
    .enable_clock = usart_2_enable_clock,
    .init_gpio = hardwareUsart2Init,
};

void board_usart_init(void) {
  usart_init(&uart5);
  usart_init(&uart2);
}
