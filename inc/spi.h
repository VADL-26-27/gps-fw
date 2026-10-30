#ifndef SPI_H_
#define SPI_H_

#define SPI_TX_DMA_CLEAR_FLAGS                                                 \
  (DMA_LIFCR_CFEIF3 | DMA_LIFCR_CDMEIF3 | DMA_LIFCR_CTEIF3 |                   \
   DMA_LIFCR_CHTIF3 | DMA_LIFCR_CTCIF3)

#define SPI_RX_DMA_CLEAR_FLAGS                                                 \
  (DMA_LIFCR_CFEIF0 | DMA_LIFCR_CDMEIF0 | DMA_LIFCR_CTEIF0 |                   \
   DMA_LIFCR_CHTIF0 | DMA_LIFCR_CTCIF0)

#define SPI_TRANSFER_TIMEOUT_MS 300U
#include "stm32f413xx.h"
#include <stdbool.h>
#include <stddef.h>
#include "timer.h"

typedef enum {
  SPI_RESULT_NONE,
  SPI_RESULT_PENDING,
  SPI_RESULT_OK,
  SPI_RESULT_FAILED
} spi_result_t;

typedef struct spi spi_t;

/* This current driver assume that the SPI driver uses a DMA stream for both
   MISO and MOSI, as there is no other device that requires use of the DMA,
   thus, using it for flight logging is good use of periphreals
 */
struct spi {
  SPI_TypeDef *regs;

  DMA_TypeDef *dma;
  DMA_Stream_TypeDef *tx_dma;
  DMA_Stream_TypeDef *rx_dma;

  IRQn_Type tx_dma_irqn;
  IRQn_Type rx_dma_irqn;
  IRQn_Type irqn;

  uint32_t transfer_start_ms;

  uint8_t rx_dummy;
  uint8_t tx_dummy;

  volatile bool rx_dma_done;
  volatile bool tx_dma_done;
  volatile bool error;
  bool busy;
  volatile spi_result_t result;

  void (*enable_clock)(void);
  void (*init_gpio)(void);
  void (*reset_periphreal)(spi_t *port);
};

void spi_init(spi_t *port);

bool spi_transfer_dma(spi_t *port, const uint8_t *tx, uint8_t *rx,
                      uint32_t len);

void spi_handle_irq(spi_t *port);

void spi_handle_tx_dma_irq(spi_t *port);

void spi_handle_rx_dma_irq(spi_t *port);

void spi_poll(spi_t *port);

bool spi_recover(spi_t *port);

spi_result_t spi_take_result(spi_t *port);

void spi1_enable_clock();

void spi1_reset_periphreal(spi_t *port);

#endif // SPI_H_