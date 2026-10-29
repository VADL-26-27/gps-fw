#ifndef USART_H_
#define USART_H_

#include <stdbool.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f4xx.h"
#include "ring_buf.h"

#define USART_RX_RING_SIZE BUFFER_SIZE
#define USART_RX_READ_MAX 64u

typedef enum {
    USART_RESULT_NONE,
    USART_RESULT_PENDING,
    USART_RESULT_OK,
    USART_RESULT_FAILED
} usart_result_t;

typedef struct usart usart_t;

struct usart {
    USART_TypeDef *regs;
    ring_buf *rx;
    ring_buf *tx;
    uint32_t clock_hz;
    uint32_t baud_rate;

    DMA_TypeDef *dma;
    DMA_Stream_TypeDef *tx_dma;
    DMA_Stream_TypeDef *rx_dma;

    IRQn_Type tx_dma_irqn;
    IRQn_Type rx_dma_irqn;
    IRQn_Type irqn;
    uint8_t tx_channel;
    uint8_t rx_channel;
    uint16_t rx_position;
    uint16_t tx_dma_len;

    uint32_t tx_start_ms;
    uint32_t rx_start_ms;

    volatile bool tx_dma_done;
    volatile bool rx_dma_done;

    bool tx_busy;
    bool rx_busy;

    volatile usart_result_t tx_result;
    volatile usart_result_t rx_result;

    void (*enable_clock)(void);
    void (*init_gpio)(void);
    void (*reset_peripheral)(usart_t *port);
};

/* Extends flight-logic's buffered USART interface with a DMA backend.
 * write copies all bytes or returns false without enqueuing any.
 * read copies exactly len bytes or returns false without consuming any.
 * Caller buffers may be reused immediately after a successful write. */
bool usart_init(usart_t *port);
bool usart_write(usart_t *port, const uint8_t *data, uint32_t len);
bool usart_read(usart_t *port, uint8_t *data, uint32_t len);
uint16_t usart_rx_available(usart_t *port);
void usart_rx_reset(usart_t *port);
void usart_poll(usart_t *port);
bool usart_tx_busy(usart_t *port);
bool usart_faulted(const usart_t *port);
bool usart_stop(usart_t *port);
bool usart_recover(usart_t *port);
void usart_handle_irq(usart_t *port);
void usart_handle_rx_dma_irq(usart_t *port);
void usart_handle_tx_dma_irq(usart_t *port);

#define USART_TRANSFER_TIMEOUT_MS 300u
#endif // USART_H_
