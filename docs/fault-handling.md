# Startup, Recovery, and Watchdog

## Startup

1. Configure clocks, GPIO, NVIC, DMA streams/channels, and IWDG.
2. Create `GPS_Task`, `LoRa_Task`, `SD_Task`, their mailbox/FIFOs, and notifications.
3. Start GNSS RX DMA; GNSS acquisition proceeds independently.
4. Start LoRa RX DMA; synchronize/configure the RAK3172 with bounded timeouts.
5. Initialize SDIO/FatFs. A missing card must not prevent GPS telemetry.

## Degraded-operation rules

| Failure | Behavior | Recovery |
|---|---|---|
| SD unavailable | Continue GNSS and LoRa; count dropped logs. | Retry mount periodically; record recovery when available. |
| GNSS no fix | Continue parsing; mark telemetry invalid or omit position. | Report timeout; limited module reset/reconfiguration attempts. |
| Radio unavailable | Retain newest `latest_fix`; do not block other tasks. | Bounded retry, then reset/power-cycle if supported; back off. |
| UART error | Count error and discard partial record. | Clear state and restart RX DMA if needed. |
| DMA error/timeout | Count error. | Disable stream, confirm disabled, clear flags, reset indices, configure, restart. |

## Independent watchdog

Only a small supervisor refreshes the STM32 IWDG. It does so only after recent health signals from the scheduler, `GPS_Task`, and `LoRa_Task`. `SD_Task` is required only during an active SD operation; absent/unmounted SD must not cause a reset.

Each task signals health after bounded work or a normal wake. If a critical task stops executing, the watchdog is not refreshed and the MCU restarts through the startup sequence. Preserve reset cause/counters for later log or telemetry reporting.
