#include "spi.h"
#include "spi_board.h"

void SPI1_IRQHandler(void) { spi_handle_irq(&spi1); }
void DMA2_Stream3_IRQHandler(void) { spi_handle_tx_dma_irq(&spi1); }
void DMA2_Stream0_IRQHandler(void) { spi_handle_rx_dma_irq(&spi1); }
