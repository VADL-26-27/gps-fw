# Driver Design

## Board selection

The current build targets STM32F413, but the final board is undecided. No USART,
GPIO pins, DMA controller, streams, channels, or interrupt vectors are selected
by the driver. `src/usart_board.c` contains the empty LoRa instance and radio
control placeholders. An unset instance fails initialization without accessing
hardware, and the LoRa task stays offline.

## USART + DMA driver

`inc/usart.h` and `src/usart.c` follow the `spi_t *port` pattern used in
`flight-logic-fw`. Each `usart_t` supplies:

- USART registers, DMA controller, RX and TX DMA stream pointers.
- USART error IRQ number and RX/TX DMA IRQ numbers.
- RX/TX stream indices (0..7) and DMA channels (0..7).
- Peripheral clock, baud rate, and a FreeRTOS-safe IRQ priority.
- Clock/GPIO initialization and an optional USART reset callback.
- Its own RX ring, transfer state, counters, and task notification target.

Board code must select a valid hardware mapping: stream pointers, indices,
channels, and vectors must agree. RX and TX use distinct streams on the supplied
DMA controller. Clock callbacks enable both the USART and DMA clocks; reset
callbacks must never reset a DMA controller shared with another peripheral.
All three IRQs use the supplied, identical priority.

RX uses circular DMA, with half/full and error interrupts. TX uses normal DMA,
with completion and error interrupts. `usart_handle_irq()` handles USART errors
only; RXNEIE, TXEIE, TCIE, and IDLEIE remain disabled. Board vector wrappers in
`src/usart_irq.c` must forward to `usart_handle_irq()`,
`usart_handle_rx_dma_irq()`, and `usart_handle_tx_dma_irq()` once chosen.

Short RX bursts are discovered by sampling NDTR in task context. The LoRa task
caps active waits at 10 ms so AT replies below a DMA half-buffer still get
processed. A DMA TX-completion notification wakes the task, but buffer ownership
is released only when polling observes USART TC (the last stop bit transmitted).
Keep TX data unchanged until `usart_tx_busy()` returns false. A failed transfer
retains ownership until a successful stop/recovery confirms the DMA is stopped.

Each instance has one owning task. DMA keeps running during reads; ring overflow
or an IRQ blackout spanning both half boundaries is reported as a fault. Choose
a polling interval appropriate for the configured baud rate and ring size.
ISRs never parse protocols, perform logging, or wait for a peripheral.

## SDIO + FatFs driver

`SD_Task` is the sole owner of SDIO and FatFs. The driver initializes a card in 1-bit mode, selects 4-bit mode, and provides bounded block read/write operations using SDIO DMA. The FatFs `diskio` adapter converts sector operations into those block operations.

```text
SD_Task -> FatFs -> diskio adapter -> SDIO + DMA driver -> microSD
```

The task starts a DMA operation and waits on a completion notification with a finite timeout. It must never spin while a card transfer is in progress.
