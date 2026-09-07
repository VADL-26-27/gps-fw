#ifndef UART_DMA_DRIVER_H
#define UART_DMA_DRIVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"
#include "stm32f4xx.h"

#define UART_DMA_RX_RING_SIZE 1024u

typedef enum {
  UART_DMA_NOTIFY_RX_DATA = (1UL << 0),
  UART_DMA_NOTIFY_TX_DONE = (1UL << 1),
  UART_DMA_NOTIFY_ERROR = (1UL << 2),
} uart_dma_notify_bits_t;

typedef struct {
  USART_TypeDef *usart;
  DMA_Stream_TypeDef *rx_dma;
  DMA_Stream_TypeDef *tx_dma;
  uint8_t rx_chsel;
  uint8_t tx_chsel;
  TaskHandle_t notify_task;

  uint8_t rx_ring[UART_DMA_RX_RING_SIZE] __attribute__((aligned(4)));
  volatile uint16_t rx_producer;
  uint16_t rx_consumer;
  volatile bool tx_busy;
  volatile uint32_t error_count;
} uart_dma_t;

uart_dma_t *uart_dma_lora_get(void);
void uart_dma_lora_init(uart_dma_t *dev);
void uart_dma_set_notify_task(uart_dma_t *dev, TaskHandle_t task);
void uart_dma_start_rx(uart_dma_t *dev);
uint16_t uart_dma_rx_available(const uart_dma_t *dev);
uint16_t uart_dma_rx_read(uart_dma_t *dev, uint8_t *dst, uint16_t max);
void uart_dma_rx_reset(uart_dma_t *dev);
void uart_dma_start_tx(uart_dma_t *dev, const uint8_t *buf, uint16_t len);
bool uart_dma_tx_busy(const uart_dma_t *dev);
uint32_t uart_dma_error_count(const uart_dma_t *dev);

void uart_dma_isr_usart(uart_dma_t *dev);
void uart_dma_isr_rx_dma(uart_dma_t *dev);
void uart_dma_isr_tx_dma(uart_dma_t *dev);

#endif