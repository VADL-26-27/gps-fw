#ifndef USART_H_
#define USART_H_

#include <stdbool.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f4xx.h"

#define USART_RX_RING_SIZE 1024u
#define USART_RX_READ_MAX 64u

typedef enum {
  USART_NOTIFY_RX_DATA = (1UL << 0),
  /* DMA completed; task polling still waits for USART TC. */
  USART_NOTIFY_TX_DONE = (1UL << 1),
  USART_NOTIFY_ERROR = (1UL << 2),
} usart_notify_bits_t;


typedef struct usart usart_t;

struct usart {
  USART_TypeDef *regs;
  DMA_TypeDef *dma;
  DMA_Stream_TypeDef *tx_dma;
  DMA_Stream_TypeDef *rx_dma;
  IRQn_Type tx_dma_irqn;
  IRQn_Type rx_dma_irqn;
  IRQn_Type irqn;
  uint8_t tx_stream; /* DMA stream index 0..7, for status/clear registers. */
  uint8_t rx_stream;
  uint8_t tx_channel;
  uint8_t rx_channel;
  uint32_t clock_hz; /* USART peripheral clock, supplied by board code. */
  uint32_t baud_rate;
  uint32_t irq_priority;
  void (*enable_clock)(void); /* USART and DMA clocks. */
  void (*init_gpio)(void);
  void (*reset_peripheral)(usart_t *port); /* Optional, USART only. */
  bool initialized;
  volatile bool tx_dma_done;
  TaskHandle_t notify_task;
  uint8_t rx_ring[USART_RX_RING_SIZE] __attribute__((aligned(4)));
  volatile uint32_t rx_producer;
  uint32_t rx_consumer;
  uint16_t rx_position;
  volatile bool tx_busy;
  volatile bool fault;
  bool rx_running;
  volatile uint32_t error_count;
  volatile uint32_t overflow_count;
};

bool usart_init(usart_t *port);
void usart_set_notify_task(usart_t *port, TaskHandle_t task);
bool usart_start_rx(usart_t *port);
/* These calls sample hardware even without a new RX notification. Reads are
 * bounded to USART_RX_READ_MAX bytes; callers drain in bounded batches. */
uint16_t usart_rx_available(usart_t *port);
uint16_t usart_rx_read(usart_t *port, uint8_t *dst, uint16_t max);
void usart_rx_reset(usart_t *port);
/* True means DMA accepted the buffer. Keep it unchanged until
 * usart_tx_busy() returns false; failures require stop/recovery first. */
bool usart_start_tx(usart_t *port, const uint8_t *buf, uint16_t len);
bool usart_tx_busy(usart_t *port);
/* Polls short RX bursts and the final USART TC flag without USART data IRQs. */
void usart_poll(usart_t *port);
bool usart_faulted(const usart_t *port);
uint32_t usart_error_count(const usart_t *port);
/* Stop only this port's streams; never reset the shared DMA controller. */
bool usart_stop(usart_t *port);
bool usart_recover(usart_t *port);

void usart_handle_irq(usart_t *port);
void usart_handle_rx_dma_irq(usart_t *port);
void usart_handle_tx_dma_irq(usart_t *port);
#endif
