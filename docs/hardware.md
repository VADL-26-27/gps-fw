# Hardware Interfaces

The schematic is the electrical source of truth. UART directions below are from the MCU perspective: MCU TX connects to module RX and MCU RX connects to module TX.

## GNSS — MAX-M10S

| Function | MCU pin | Peripheral / mode |
|---|---|---|
| MCU GNSS TX | PC6, package pin 37 | USART6_TX, AF8 |
| MCU GNSS RX | PC7, package pin 38 | USART6_RX, AF8, RX DMA |
| 1PPS | PA5, package pin 21 | Optional timer capture or EXTI |
| GNSS reset | PA8, package pin 41 | GPIO output |

GNSS UART: 9,600 bit/s, 8-N-1.

## RAK3172 telemetry module

| Function | MCU pin | Peripheral / mode |
|---|---|---|
| MCU telemetry TX | PA9, package pin 42 | USART1_TX, AF7, TX DMA |
| MCU telemetry RX | PA10, package pin 43 | USART1_RX, AF7, RX DMA |
| `TELEM_BOOT0` | PC0, package pin 8 | GPIO output |
| `TELEM_RST` | PC1, package pin 9 | GPIO output |

LoRa UART: 115,200 bit/s, 8-N-1.

## MicroSD card

| Signal | MCU pin | Function |
|---|---|---|
| `SD_CLK` | PC12, package pin 53 | SDIO clock |
| `SD_CMD` | PD2, package pin 54 | SDIO command |
| `SD_DAT0` | PC8, package pin 39 | SDIO data 0 |
| `SD_DAT1` | PC9, package pin 40 | SDIO data 1 |
| `SD_DAT2` | PC10, package pin 51 | SDIO data 2 |
| `SD_DAT3` | PC11, package pin 52 | SDIO data 3 |

Initialize the card in 1-bit mode, then transition to 4-bit operation. This is SDIO/SDMMC, not SPI. Ensure the board design provides required `CMD`/`DAT` pull-ups.

## Debug and system pins

| Function | MCU pin |
|---|---|
| SWDIO | PA13, package pin 46 |
| SWCLK | PA14, package pin 49 |
| SWO | PB3, package pin 55 |
| Boot strap | BOOT0, package pin 60 |
| MCU reset | NRST, package pin 7 |
| External oscillator | PH0 / PH1, package pins 5 / 6 |
