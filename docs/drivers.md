# Driver Design

## Board selection

The current build targets STM32F413, but the final board is undecided. No USART,
GPIO pins, DMA controller, streams, channels, or interrupt vectors are selected
by the driver. `src/usart_board.c` contains the empty LoRa instance and radio
control placeholders. An unset instance fails initialization without accessing
hardware, and the LoRa task stays offline.

## USART + DMA driver

The USART driver extends `flight-logic-fw/inc/usart.h` and
`flight-logic-fw/src/usart.c`. It keeps the `usart_t *port` interface, per-port
`ring_buf *rx` / `*tx`, `clock_hz`, `baud_rate`, and clock/GPIO callbacks.
`usart_init()` computes BRR using the same peripheral-clock/baud formula.
It returns a boolean so an unset board mapping can leave LoRa offline.
The optional reset callback only resets the peripheral; it does not supply BRR.

`inc/ring_buf.h` and `src/ring_buf.c` come from flight-logic's ring-buffer
implementation. Buffer capacity is increased from 128 to 1,024 bytes, and
indices/counts and the read length use 32-bit types to support DMA-sized data.
Each USART instance has separate RX/TX buffers supplied in `usart_board.c`.

`usart_write(port, data, len)` copies a whole write into the TX ring when space
is available, matching the original buffered interface. Caller storage can be
reused after the call. DMA transmits contiguous queued segments, including two
segments when the ring wraps. In-flight bytes remain counted until DMA completes
and polling observes USART TC, so later writes cannot overwrite them.
`usart_read(port, data, len)` returns exactly the requested bytes or false without
consuming data. `usart_rx_available()` samples NDTR for short bursts, and
`usart_rx_reset()` discards buffered input.

The extension adds DMA controller/stream pointers, channel selections, DMA IRQ
numbers, TX/RX result and completion state, timing fields, and DMA ring positions.
Stream indices and flag banks are derived from the pointers using the STM32F4
register layout. Streams must be distinct and belong to the supplied controller.
Board code supplies channels, clocks, pins, IRQs and baud rate. It must not reset
a DMA controller shared with other peripherals. Board mapping and IRQ wrappers
remain placeholders until the final board is selected.

RX DMA starts in circular mode during initialization/recovery and writes directly
to the port's RX ring. The driver samples progress from task context and DMA
half/full IRQs, detects overwritten buffers and ambiguous coalesced half/full
flags, and rejects reads overtaken by DMA. Continuous RX has no idle timeout.
TX DMA uses normal mode; each segment has a 300 ms timeout. Transfer failures
set the result fields to FAILED and retain busy state until a successful stop.
Recovery reinitializes both rings, discards pending bytes and restarts RX.

`usart_handle_irq()` handles USART errors only. RXNEIE, TXEIE, TCIE and IDLEIE
remain disabled; DMA handles byte movement. All three IRQs use the same
FreeRTOS-safe priority. The LoRa task uses the generic read/write API directly
and polls data/completion at most 10 ms apart while active. Choose that interval
for the final baud rate and buffer size. Each port has one owning task.
ISRs never parse protocols, log or wait for a peripheral.

## SDIO + FatFs driver

`SD_Task` is the sole owner of SDIO and FatFs. The driver initializes a card in 1-bit mode, selects 4-bit mode, and provides bounded block read/write operations using SDIO DMA. The FatFs `diskio` adapter converts sector operations into those block operations.

```text
SD_Task -> FatFs -> diskio adapter -> SDIO + DMA driver -> microSD
```

The task starts a DMA operation and waits on a completion notification with a finite timeout. It must never spin while a card transfer is in progress.
