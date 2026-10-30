#ifndef SPI_BOARD_H_
#define SPI_BOARD_H_

#include "spi.h"
#include "flash_mem.h"

extern spi_t spi1;
extern flash_t flash;
void board_spi_init(void);
#endif // SPI_BOARD_H_