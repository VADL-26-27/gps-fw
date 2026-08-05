# Nose Cone GNSS & LoRa Telemetry Firmware

Firmware architecture for the nose-cone GNSS receiver and LoRa telemetry interface.

## Design summary

The firmware uses **FreeRTOS for application scheduling** and **bare-metal (self-configured) peripheral drivers**. Here, “bare metal” means the firmware owns the MCU UART, DMA/DMAMUX, GPIO, and NVIC configuration rather than depending on a high-level blocking UART driver. UART data movement uses DMA; interrupts only record progress and wake the appropriate task. No protocol parsing, logging, or blocking transmit is performed inside an interrupt service routine (ISR).

The GNSS receiver and LoRa radio have independent antennas and independent UART lines. Consequently, GNSS reception and LoRa transmission do not require antenna arbitration or a shared RF timing lock.

```mermaid
flowchart TD
    GNSS["MAX-M10S GNSS receiver\nDedicated GNSS antenna"] -->|"NMEA / UBX bytes"| GPDMA["GNSS UART RX DMA\nCircular buffer"]
    GPDMA -->|"UART IDLE or DMA event"| GPISR["Short GNSS UART ISR\nSnapshot producer position"]
    GPISR --> GPS["gps_process()\nConsume bytes and parse fixes"]
    GPS --> Q["Telemetry queue\nSoftware-only handoff"]
    Q --> TELEM["telemetry_process()\nBuild radio payload"]
    TELEM --> LTDMA["LoRa UART TX DMA"]
    LTDMA --> LORA["RAK3172 LoRa module\nDedicated LoRa antenna"]
```

## Execution model

FreeRTOS schedules the application work. The drivers remain non-blocking: DMA performs byte movement, an ISR records progress, and the ISR wakes or notifies the waiting task using the ISR-safe FreeRTOS API.

| Task | Responsibility | Inputs |
|---|---|---|
| `GPS_Task` | Consume GNSS DMA bytes, parse NMEA/UBX, publish valid fixes | GNSS RX notification |
| `Telem_Task` | Dequeue fixes, build packets, manage LoRa TX DMA | telemetry queue; LoRa completion/response notification |
| `SD_Task` | Drain the shared log queue and write records | log queue |
| `USB_Task` | Debug console and diagnostics | USB RX notification |

The idle task may enter a low-power mode when all application tasks are blocked.

### Proposed task priorities

These are initial relative priorities, not final numeric values. Confirm them with measured GNSS throughput, LoRa response deadlines, SD-card worst-case write latency, and the project's `configMAX_PRIORITIES` setting.

| Task | Relative priority | Rationale |
|---|---:|---|
| `GPS_Task` | High | Must promptly drain the GNSS DMA ring so continuous serial data cannot overrun it. |
| `Telem_Task` | Medium-high | Services radio responses and sends queued telemetry without delaying GNSS receive processing. |
| `SD_Task` | Low | SD-card writes can be slow and must not interfere with GNSS or radio servicing. |
| `USB_Task` | Low | Debug/console traffic is non-critical and should yield to flight telemetry. |
| FreeRTOS idle task | Lowest | Reclaims deleted-task memory if enabled and is the natural low-power idle point. |

Tasks should block on queues, direct-to-task notifications, or event groups rather than poll. In particular, `GPS_Task` should be notified whenever the RX DMA producer index advances.

### Interrupt ownership and behavior

| Interrupt source | Typical event | ISR responsibility | Task notified |
|---|---|---|---|
| GNSS UART | IDLE, error | Clear/report the event; snapshot the GNSS RX DMA producer position | `GPS_Task` |
| GNSS RX DMA | Half transfer, transfer complete, DMA error | Record buffer progress/error; optionally notify when no IDLE event is expected | `GPS_Task` |
| LoRa UART | IDLE, error | Clear/report the event; snapshot the LoRa RX DMA producer position | `Telem_Task` |
| LoRa RX DMA | Half transfer, transfer complete, DMA error | Record buffer progress/error; notify the radio receive state machine | `Telem_Task` |
| LoRa TX DMA / UART TC | DMA transfer complete; UART transmission complete | Mark the TX buffer available and advance the non-blocking TX state | `Telem_Task` |
| GPS 1PPS (optional) | Rising edge | Capture a timer timestamp; do no serial parsing | Optional timing/diagnostic task |

UART IDLE is useful because it identifies a gap between received messages; DMA half/full-transfer interrupts are still useful as a safety mechanism when a stream has no idle gap. The precise set of enabled events depends on the MCU UART/DMA peripheral and configured data rate.

On Cortex-M, a **smaller numeric NVIC value means a higher hardware interrupt priority**. Any ISR that calls `xTaskNotifyFromISR()`, `xQueueSendFromISR()`, or another `...FromISR()` FreeRTOS API must be assigned an NVIC priority permitted by `configMAX_SYSCALL_INTERRUPT_PRIORITY`. Do not choose raw numeric NVIC priorities until that FreeRTOS configuration and the MCU's implemented priority bits are known.

## UART and DMA policy

### GNSS UART receive

GNSS output is continuous, so reception uses a DMA circular buffer.

1. Configure the UART receiver and its DMA request to write into a fixed RAM buffer.
2. Start DMA once in circular mode; do **not** restart it for every NMEA sentence.
3. On UART IDLE and/or DMA half-transfer/transfer-complete events, the ISR snapshots the DMA write position and sends an ISR-safe notification to `GPS_Task`.
4. `GPS_Task` consumes only the newly received byte range, handling circular-buffer wrap, and parses NMEA or UBX in task context.

The ISR must not parse NMEA, allocate memory, write an SD card, or enqueue a potentially blocking LoRa transmission.

### LoRa UART transmit

LoRa commands and telemetry payloads are sent with UART TX DMA.

1. `Telem_Task` dequeues a completed telemetry item and formats its UART frame in a stable TX buffer.
2. It starts TX DMA only when the UART and TX buffer are idle.
3. The DMA-complete/UART-TC ISR marks the buffer available and advances the non-blocking transmit state machine.

The TX buffer must remain unchanged until transmit completion. A queue of buffers, or ownership flag per buffer, prevents a new payload from overwriting an active DMA transfer.

### LoRa UART receive

Use RX DMA for the LoRa UART when the RAK3172 can emit responses or unsolicited events. The same circular-buffer and short-ISR pattern as the GNSS receiver applies. If the protocol is strictly request/response, a bounded DMA receive transaction may also be used, but it must have a timeout and recovery path.

## ISR rules

ISRs should be short and deterministic:

- acknowledge the peripheral/DMA event;
- snapshot the DMA producer index or completion status;
- update a `volatile` flag or ring-buffer index; and
- return.

Tasks own parsing, queue operations that are not ISR-safe, packet construction, SD writes, retry policies, and error reporting. Shared ISR/task state must be accessed atomically or inside a short critical section.

## Telemetry queue

The telemetry queue is a software boundary, not a physical connection:

```text
GNSS fix -> telemetry_queue_push(fix) -> telemetry_process() -> LoRa UART TX DMA
```

Use a fixed-size queue with explicit overflow behavior. Recommended policy: preserve the newest valid navigation fix, increment a dropped-message counter, and expose that counter through the debug console and log.

## DMA and UART configuration checklist

- Enable peripheral, GPIO, DMA/DMAMUX, and interrupt-controller clocks.
- Configure UART pins, baud rate, word length, parity, stop bits, and RX/TX enable.
- Select the correct UART-to-DMA request mapping.
- Place DMA buffers in memory accessible to DMA and aligned as required by the MCU.
- Configure GNSS RX DMA as circular; configure LoRa TX DMA as normal mode per frame.
- Enable and prioritize UART IDLE/error and DMA completion interrupts.
- Clear stale UART error conditions and define recovery for overrun, framing, DMA, and radio-response timeouts.
- If the MCU has a data cache, perform the required cache maintenance around DMA buffers.

Using DMA does **not** mean the CPU does nothing: firmware still configures these peripherals, manages buffer ownership, and handles completion/error events. DMA simply moves UART bytes without a CPU interrupt for every byte.

## Logging and USB

GNSS and telemetry work may publish log records into a separate log queue. `SD_Task` drains that queue using non-blocking or bounded operations. USB debug output must likewise be buffered or rate-limited so it cannot delay GNSS parsing or radio service.

## Non-goals and assumptions

- There is no shared GNSS/LoRa antenna and therefore no antenna switching logic.
- `GPS_TIMEPULSE` / 1PPS is optional and currently unassigned. It may later feed a timer capture input for precise timestamping; it is not required for UART GNSS parsing.
- RTOS queues and task notifications are used for application handoff; low-level drivers do not block waiting for UART bytes or transmit completion.

## Acceptance checks

- GNSS data remains loss-free at the configured output rate while LoRa transmissions and SD logging occur.
- UART ISRs perform no parsing or blocking I/O.
- DMA buffers are never modified before their transfer has completed.
- Queue overflow, UART errors, and LoRa timeouts are observable and recover without a reset.
- ISR-to-task notifications use only the ISR-safe FreeRTOS APIs and request a context switch when required.
