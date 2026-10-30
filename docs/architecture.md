# Architecture

## Execution model

FreeRTOS separates asynchronous GNSS acquisition, bidirectional radio service, and variable-latency SD logging. The firmware self-configures GPIO, UART, DMA stream/channel selection, and NVIC priorities.

The system intentionally does **not** queue every GNSS fix for radio transmission. The safety goal is fresh position telemetry, so a new valid fix replaces a stale unsent one.

```text
GNSS UART DMA ring -> GPS_Task -> latest_fix mailbox -> LoRa_Task -> RAK3172 -> ground station
                                               \-> sd_log_queue -> SD_Task -> microSD
RAK3172 UART DMA ring -> LoRa_Task -> ground_cmd_fifo -> command handler
                                               \-> sd_log_queue
```

## Task behavior

### `GPS_Task` — highest priority

1. Wait for GNSS UART/DMA notification.
2. Consume only bytes newly written to the circular DMA ring.
3. Parse GGA and VTG records; assemble and validate a coherent `gps_fix_t`.
4. Atomically publish `latest_fix` and notify `LoRa_Task`.
5. Submit a bounded log record to `sd_log_queue` without waiting for SD space.

### `LoRa_Task` — second-highest priority

1. Receive UART/DMA and mailbox notifications plus timer deadlines.
2. At the 1 Hz telemetry deadline, copy the newest valid fix if it meets the configured freshness limit.
3. Serialize the 32-byte telemetry frame, convert it to `AT+PSEND=<hex>`, and start UART TX DMA.
4. Run the RAK3172 P2P state machine, including bounded waits for AT responses, receive windows, retries, and recovery.
5. Parse incoming RAK events. Queue complete candidate ground commands in `ground_cmd_fifo`, validate them, execute only allowed actions, and log outcomes.

### `SD_Task` — medium priority

1. Wait for log FIFO data or a card retry deadline.
2. Use SDIO DMA and bounded batches to write logs through FatFs.
3. Block on DMA completion with a timeout; do not spin or hold a lock while waiting.

## Scheduling policy

`LoRa_Task` has no fixed execution frequency. It is event-driven. The **telemetry transmit deadline** is 1 Hz; UART receive events and state-machine deadlines may wake it more often.

Initial safety requirement to refine with the systems team:

> While a valid GNSS fix is available, attempt to transmit the newest valid position once per second. Never intentionally transmit a fix older than the defined freshness limit. Count and log failed transmissions.

Choose the freshness limit and ground-station stale timeout from mission requirements; initial engineering values could be 2 s and 5 s respectively.

## Buffer policy

| Buffer | Initial size | Overflow rule |
|---|---:|---|
| GNSS RX DMA ring | 1,024 bytes | Size covers roughly 1 s at 9,600 bit/s; parser must drain before wrap. |
| LoRa RX DMA ring | 1,024 bytes | Tune for longest RAK response/event at 115,200 bit/s. |
| `latest_fix` | one double-buffered fix | Newest valid fix replaces older unsent fix. |
| LoRa active TX buffer | 544 bytes | Private to `LoRa_Task`; never overwrite while TX DMA/UART is active. |
| `ground_cmd_fifo` | 4–8 complete commands | Reject/count new commands when full; do not overwrite an existing command. |
| `sd_log_queue` | 32 × 128-byte records | Preserve FIFO order; count dropped records when full. |

## Architecture assessment

The separation matches hardware ownership: GNSS, radio, and SD each have one task owner. The mailbox prevents stale location backlog, the FIFO preserves forensic logs, and the radio is responsive to both telemetry and ground traffic.

The main optimization is not more tasks; it is defining a measurable telemetry-freshness requirement and proving that RAK3172 P2P airtime, receive windows, and retries can sustain it. Add another task only if payload building becomes computationally significant or an independent command-processing workload emerges.
