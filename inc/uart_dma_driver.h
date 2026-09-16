#ifndef UART_DMA_DRIVER_H
#define UART_DMA_DRIVER_H

#include <stdbool.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f4xx.h"

#define UART_DMA_RX_RING_SIZE 1024u
#define UART_DMA_RX_READ_MAX 64u

typedef enum {
  UART_DMA_NOTIFY_RX_DATA = (1UL << 0),
  UART_DMA_NOTIFY_TX_DONE = (1UL << 1),
  UART_DMA_NOTIFY_ERROR = (1UL << 2),
} uart_dma_notify_bits_t;

/* USART1/DMA2 streams 2 and 7 only. Task APIs have one owner: LoRa_Task.
 * RX totals count bytes, not modulo positions. ISR blackout spanning both DMA
 * half boundaries is conservatively treated as data loss. */
typedef struct {
  USART_TypeDef *usart;
  DMA_Stream_TypeDef *rx_dma;
  DMA_Stream_TypeDef *tx_dma;
  TaskHandle_t notify_task;
  uint8_t rx_ring[UART_DMA_RX_RING_SIZE] __attribute__((aligned(4)));
  volatile uint32_t rx_producer;
  uint32_t rx_consumer;
  uint16_t rx_position;
  volatile bool tx_busy;
  volatile bool fault;
  bool rx_running;
  volatile uint32_t error_count;
  volatile uint32_t overflow_count;
} uart_dma_t;

uart_dma_t *uart_dma_lora_get(void);
bool uart_dma_lora_init(uart_dma_t *dev);
void uart_dma_set_notify_task(uart_dma_t *dev, TaskHandle_t task);
bool uart_dma_start_rx(uart_dma_t *dev);
/* These calls sample hardware even without a new RX notification. Reads are
 * bounded to UART_DMA_RX_READ_MAX bytes; callers drain in bounded batches. */
uint16_t uart_dma_rx_available(uart_dma_t *dev);
uint16_t uart_dma_rx_read(uart_dma_t *dev, uint8_t *dst, uint16_t max);
void uart_dma_rx_reset(uart_dma_t *dev);
/* True means DMA accepted the buffer. Keep it unchanged until !tx_busy. */
bool uart_dma_start_tx(uart_dma_t *dev, const uint8_t *buf, uint16_t len);
bool uart_dma_tx_busy(const uart_dma_t *dev);
bool uart_dma_faulted(const uart_dma_t *dev);
uint32_t uart_dma_error_count(const uart_dma_t *dev);
/* Stop only this UART's streams, never reset the shared DMA2 controller. */
bool uart_dma_stop(uart_dma_t *dev);
bool uart_dma_recover(uart_dma_t *dev);
void uart_dma_radio_control_init(void);
void uart_dma_radio_reset(bool asserted);

void uart_dma_isr_usart(uart_dma_t *dev);
void uart_dma_isr_rx_dma(uart_dma_t *dev);
void uart_dma_isr_tx_dma(uart_dma_t *dev);
#endif
