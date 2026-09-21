# Driver Design

## STM32F411 DMA facts

STM32F411RET6 does not have a DMAMUX. Select each supported peripheral request with `CHSEL[2:0]` in the selected DMA stream's `DMA_SxCR` register. Allocate non-conflicting streams for USART6 RX, USART1 RX, USART1 TX, and SDIO before implementation.

The Cortex-M4 in this MCU does not have a data cache, so DMA cache-maintenance operations are not required.

## UART + DMA driver

Use a shared UART/DMA driver with per-instance configuration for USART6 and USART1.

1. Start RX DMA once in circular mode with a RAM ring buffer.
2. On UART IDLE, DMA half/full, or error, ISR acknowledges the event and snapshots the DMA producer index.
3. ISR uses an ISR-safe notification to wake the owning task.
4. The task consumes newly written bytes and handles wraparound.
5. For TX, the owner builds an immutable command in a private buffer, starts normal-mode TX DMA, and releases that buffer only after completion.

ISRs never parse protocols, perform logging, or wait for a peripheral.

## SDIO + FatFs driver

`SD_Task` is the sole owner of SDIO and FatFs. The driver initializes a card in 1-bit mode, selects 4-bit mode, and provides bounded block read/write operations using SDIO DMA. The FatFs `diskio` adapter converts sector operations into those block operations.

```text
SD_Task -> FatFs -> diskio adapter -> SDIO + DMA driver -> microSD
```

The task starts a DMA operation and waits on a completion notification with a finite timeout. It must never spin while a card transfer is in progress.
