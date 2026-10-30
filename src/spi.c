#include "spi.h"

static void clear_dma_flags(spi_t *port, uint32_t mask) {
  port->dma->LIFCR = mask;
}

void spi1_enable_clock() {
  RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
  RCC->APB2LPENR |= RCC_APB2LPENR_SPI1LPEN;
  (void)RCC->APB2ENR;
  (void)RCC->APB2LPENR;
}

static void spi_dma_init(spi_t *port) {
  // Enable DMA2 Clock
  RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN;
  RCC->AHB1LPENR |= RCC_AHB1LPENR_DMA2LPEN;

  (void)RCC->AHB1ENR;
  (void)RCC->AHB1LPENR;

  // Request Disable
  port->rx_dma->CR &= ~DMA_SxCR_EN;
  port->tx_dma->CR &= ~DMA_SxCR_EN;

  while (port->rx_dma->CR & DMA_SxCR_EN) {
  }
  while (port->tx_dma->CR & DMA_SxCR_EN) {
  }

  // Channel 3; byte-size transfers; normal mode
  // Periphreal increment disabled
  // Implied DIR = 00 (periphreal to memory)
  port->rx_dma->CR = (3U << DMA_SxCR_CHSEL_Pos) | DMA_SxCR_MINC |
                     DMA_SxCR_TCIE | DMA_SxCR_TEIE | DMA_SxCR_DMEIE;

  port->tx_dma->CR = (3U << DMA_SxCR_CHSEL_Pos) | DMA_SxCR_MINC |
                     DMA_SxCR_TCIE | DMA_SxCR_TEIE | DMA_SxCR_DMEIE |
                     DMA_SxCR_DIR_0;

  // Direct mode: FIFO disabled.
  port->rx_dma->FCR = 0;
  port->tx_dma->FCR = 0;

  // Both directions use SPI's data register.
  port->rx_dma->PAR = (uint32_t)&port->regs->DR;
  port->tx_dma->PAR = (uint32_t)&port->regs->DR;

  // Clear pending DMA interrupt flags for Streams 0 and 3.
  clear_dma_flags(port, SPI_TX_DMA_CLEAR_FLAGS | SPI_RX_DMA_CLEAR_FLAGS);

  NVIC_SetPriority(port->irqn, 6U);
  NVIC_SetPriority(port->rx_dma_irqn, 6U);
  NVIC_SetPriority(port->tx_dma_irqn, 6U);

  NVIC_ClearPendingIRQ(port->rx_dma_irqn);
  NVIC_ClearPendingIRQ(port->tx_dma_irqn);

  NVIC_EnableIRQ(port->rx_dma_irqn);
  NVIC_EnableIRQ(port->tx_dma_irqn);
}

void spi_init(spi_t *port) {
  port->enable_clock();
  port->init_gpio();

  // Mode 0, MSB-first bit order, 8-bit frames.
  port->regs->CR1 = 0;
  port->regs->CR2 = 0;
  port->regs->I2SCFGR = 0; // Selects SPI rather than I2S

  // Configure Baud Rate for divisor to be /4
  port->regs->CR1 |= SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI | SPI_CR1_BR_0;

  spi_dma_init(port);

  NVIC_ClearPendingIRQ(port->irqn);
  NVIC_EnableIRQ(port->irqn);

  // No RXNEIE or TXEIE since we are using DMA
  port->regs->CR2 |= SPI_CR2_ERRIE;

  port->regs->CR1 |= SPI_CR1_SPE;
}

bool spi_transfer_dma(spi_t *port, const uint8_t *tx, uint8_t *rx,
                      uint32_t len) {

  if (port == NULL || len == 0U || len > 65535U) {
    return false;
  }

  if (port->busy || port->error || port->result != SPI_RESULT_NONE) {
    return false;
  }

  if ((port->tx_dma->CR & DMA_SxCR_EN) || (port->rx_dma->CR & DMA_SxCR_EN)) {
    return false;
  }

  uint32_t saved = __get_PRIMASK();

  __disable_irq();

  if (port->error || port->result != SPI_RESULT_NONE ||
      (port->regs->SR & (SPI_SR_BSY | SPI_SR_OVR | SPI_SR_MODF | SPI_SR_FRE)) ||
      (port->tx_dma->CR & DMA_SxCR_EN) || (port->rx_dma->CR & DMA_SxCR_EN)) {
    __set_PRIMASK(saved);
    return false;
  }

  port->tx_dma->NDTR = len;
  if (tx != NULL) {
    // Trasmit len bytes
    port->tx_dma->M0AR = (uint32_t)tx;
    port->tx_dma->CR |= DMA_SxCR_MINC; // Advance through the buffer
  } else if (tx == NULL) {
    port->tx_dma->M0AR = (uint32_t)&port->tx_dummy;
    port->tx_dma->CR &= ~DMA_SxCR_MINC;
  }

  port->rx_dma->NDTR = len;
  if (rx != NULL) {
    port->rx_dma->M0AR = (uint32_t)rx;
    port->rx_dma->CR |= DMA_SxCR_MINC;
  } else if (rx == NULL) {
    // Discard Recieved bytes into persistent dummy
    port->rx_dma->M0AR = (uint32_t)&port->rx_dummy;
    port->rx_dma->CR &= ~DMA_SxCR_MINC;
  }

  // Clear old flags
  clear_dma_flags(port, SPI_TX_DMA_CLEAR_FLAGS | SPI_RX_DMA_CLEAR_FLAGS);

  port->busy = true;
  port->tx_dma_done = false;
  port->rx_dma_done = false;
  port->transfer_start_ms = now_ms();
  port->result = SPI_RESULT_PENDING;

  __DMB();
  port->rx_dma->CR |= DMA_SxCR_EN;
  port->regs->CR2 |= SPI_CR2_RXDMAEN;

  port->tx_dma->CR |= DMA_SxCR_EN;
  port->regs->CR2 |= SPI_CR2_TXDMAEN;
  __set_PRIMASK(saved);
  return true;
}

void spi_handle_irq(spi_t *port) {
  uint32_t status = port->regs->SR;
  if (status & (SPI_SR_FRE | SPI_SR_OVR | SPI_SR_MODF)) {
    // Disable error interrupts and disable further dma transfer
    port->regs->CR2 &= ~(SPI_CR2_ERRIE | SPI_CR2_RXDMAEN | SPI_CR2_TXDMAEN);
    port->error = true;
    port->result = SPI_RESULT_FAILED;
  }
}

void spi1_reset_periphreal(spi_t *port) {
  RCC->APB2RSTR |= RCC_APB2RSTR_SPI1RST;
  (void)RCC->APB2RSTR;

  RCC->APB2RSTR &= ~RCC_APB2RSTR_SPI1RST;
  (void)RCC->APB2RSTR;
  port->regs->I2SCFGR = 0;
  port->regs->CR2 = 0;

  port->regs->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI |
                    SPI_CR1_BR_0; // /4: 6.25 MHz with PCLK2 = 25 MHz
}

bool spi_recover(spi_t *port) {
  if (port == NULL) {
    return false;
  }

  port->error = true;
  port->busy = true;
  port->result = SPI_RESULT_FAILED;

  port->regs->CR2 &= ~(SPI_CR2_TXDMAEN | SPI_CR2_RXDMAEN | SPI_CR2_ERRIE);

  // Stop both DMA streams before accessing DR ourselves.
  port->rx_dma->CR &= ~DMA_SxCR_EN;
  port->tx_dma->CR &= ~DMA_SxCR_EN;

  uint32_t start = now_ms();

  while ((port->rx_dma->CR & DMA_SxCR_EN) || (port->tx_dma->CR & DMA_SxCR_EN)) {
    if ((uint32_t)(now_ms() - start) >= 10U) {
      return false;
    }
  }

  // Let any in-progress SPI transmission finish.
  start = now_ms();
  bool spi_timed_out = false;

  while (port->regs->SR & SPI_SR_BSY) {
    if ((uint32_t)(now_ms() - start) >= 10U) {
      spi_timed_out = true;
      break;
    }
  }

  if (spi_timed_out) {
    // Fallback: reset/configure SPI, leaving SPE cleared.
    port->reset_periphreal(port);
    GPIOA->BSRR = (1U << 4);
  } else {
    GPIOA->BSRR = (1U << 4);

    /*
     * OVR: read DR, then SR.
     * FRE: reading SR clears it.
     * MODF: read SR, then write CR1.
     *
     * Perform the combined sequence even if a flag was
     * already observed/cleared by the ISR.
     */
    (void)port->regs->DR;
    (void)port->regs->SR;

    // Restore master settings; MODF may have cleared MSTR/SPE.
    // Preserve baud rate and other configured CR1 fields.
    port->regs->CR1 = (port->regs->CR1 & ~SPI_CR1_SPE) | SPI_CR1_MSTR |
                      SPI_CR1_SSM | SPI_CR1_SSI;
  }

  // DMA2 Stream 0 RX and Stream 3 TX.
  clear_dma_flags(port, SPI_TX_DMA_CLEAR_FLAGS | SPI_RX_DMA_CLEAR_FLAGS);

  NVIC_ClearPendingIRQ(port->rx_dma_irqn);
  NVIC_ClearPendingIRQ(port->tx_dma_irqn);

  NVIC_ClearPendingIRQ(port->irqn);

  port->tx_dma_done = false;
  port->rx_dma_done = false;

  port->regs->CR1 |= SPI_CR1_SPE;

  // Confirm the peripheral isn't still faulted.
  if (port->regs->SR & (SPI_SR_OVR | SPI_SR_MODF | SPI_SR_FRE)) {
    return false;
  }

  port->error = false;
  port->busy = false;
  port->regs->CR2 |= SPI_CR2_ERRIE;

  return true;
}

void spi_handle_tx_dma_irq(spi_t *port) {
  uint32_t status = port->dma->LISR;

  clear_dma_flags(port, SPI_TX_DMA_CLEAR_FLAGS);

  if (status & (DMA_LISR_DMEIF3 | DMA_LISR_TEIF3)) {
    // Disable error interupts and TX/RX DMA
    port->regs->CR2 &= ~(SPI_CR2_TXDMAEN | SPI_CR2_RXDMAEN | SPI_CR2_ERRIE);
    port->error = true;
    port->result = SPI_RESULT_FAILED;
  } else if (status & (DMA_LISR_TCIF3)) {
    // Disable TX DMA requests
    port->regs->CR2 &= ~SPI_CR2_TXDMAEN;
    port->tx_dma_done = true;
  }
}

void spi_handle_rx_dma_irq(spi_t *port) {
  uint32_t status = port->dma->LISR;

  clear_dma_flags(port, SPI_RX_DMA_CLEAR_FLAGS);

  if (status & (DMA_LISR_DMEIF0 | DMA_LISR_TEIF0)) {
    // Disable error interupts and TX/RX DMA
    port->regs->CR2 &= ~(SPI_CR2_TXDMAEN | SPI_CR2_RXDMAEN | SPI_CR2_ERRIE);
    port->error = true;
    port->result = SPI_RESULT_FAILED;
  } else if (status & (DMA_LISR_TCIF0)) {
    // Disable RX DMA requests
    port->regs->CR2 &= ~SPI_CR2_RXDMAEN;
    port->rx_dma_done = true;
  }
}

void spi_poll(spi_t *port) {
  if (port == NULL)
    return;
  if (port->error) {
    (void)spi_recover(port);
    return;
  }
  if (!port->busy)
    return;

  uint32_t saved = __get_PRIMASK();
  __disable_irq();
  uint32_t sr = port->regs->SR;

  /* ISR events and hardware status are rechecked before publishing success. */
  if (port->error || (sr & (SPI_SR_OVR | SPI_SR_MODF | SPI_SR_FRE))) {
    port->regs->CR2 &= ~(SPI_CR2_TXDMAEN | SPI_CR2_RXDMAEN | SPI_CR2_ERRIE);
    port->error = true;
    port->result = SPI_RESULT_FAILED;
  } else if (port->tx_dma_done && port->rx_dma_done &&
             !(port->tx_dma->CR & DMA_SxCR_EN) &&
             !(port->rx_dma->CR & DMA_SxCR_EN) && (sr & SPI_SR_TXE) &&
             !(sr & SPI_SR_BSY)) {
    port->regs->CR2 &= ~(SPI_CR2_TXDMAEN | SPI_CR2_RXDMAEN);
    __DMB();
    port->result = SPI_RESULT_OK;
    port->busy = false;
  } else if ((uint32_t)(now_ms() - port->transfer_start_ms) >=
             SPI_TRANSFER_TIMEOUT_MS) {
    port->regs->CR2 &= ~(SPI_CR2_TXDMAEN | SPI_CR2_RXDMAEN | SPI_CR2_ERRIE);
    port->error = true;
    port->result = SPI_RESULT_FAILED;
  }
  __set_PRIMASK(saved);
}

spi_result_t spi_take_result(spi_t *port) {
  if (port == NULL)
    return SPI_RESULT_NONE;

  uint32_t saved = __get_PRIMASK();
  __disable_irq();
  spi_result_t result = SPI_RESULT_NONE;

  if (!port->busy && !port->error &&
      (port->result == SPI_RESULT_OK || port->result == SPI_RESULT_FAILED)) {
    result = port->result;
    port->result = SPI_RESULT_NONE;
  }
  __set_PRIMASK(saved);
  return result;
}