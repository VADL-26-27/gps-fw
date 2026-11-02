# GNSS to LoRa telemetry

The foreground loop continually feeds UART1 bytes to `gps_stream_feed()` and
services RAK replies with `lora_poll()`. TIM2 schedules one foreground tick per
second. No mailbox access or radio operation runs in the timer ISR.

On each consumed tick, `telemetry_tick()` prepares `logPacket` and `rfPacket`
from the latest usable fix. A checksum-valid NAV-PVT reporting an unusable fix
stops records immediately. A missing or repeatedly replayed epoch becomes stale
after `GPS_FIX_MAX_AGE_MS` (1500 ms). Invalid/stale fixes produce no record or RF
attempt. Time without usable UTC is represented by `unix_time_ms == 0`.

`currFix.sequence` counts accepted GNSS epochs. `logPacket.sequence` and
`rfPacket.sequence` instead count prepared **1 Hz records** and match each other.
They restart at one after an MCU reset and wrap naturally at 32 bits.

## Adding SPI logging

Logging is **disabled after every boot or MCU reset**. Send the exact command
`BEGIN_LOG` followed by CR, LF, or CRLF on USART2 (115200 baud, 8N1). USART2 RX
is PA3; TX is PA2. Use a connected 3.3 V UART console/VCP; the ST-Link SWD
connection alone does not deliver these UART bytes. The reply is
`LOGGING_ENABLED`. The logging gate stays enabled until power-off or reset.
Repeated `BEGIN_LOG` commands are acknowledged without restarting the session.
There is no stop command or persistent logging-enable setting.

GNSS parsing, snapshots and radio transmission run before and after `BEGIN_LOG`.
Only logging eligibility is gated. The successful `telemetry_tick()` branch in
`src/main.c`, inside `if (logging_enabled)`, is the enqueue point. Copy the
complete `logPacket` into the logging buffer there, once per second.
It already has its record sequence and CRC. This branch executes even when the
radio is unavailable, busy, or cannot queue a command. It does not mean the radio
has finished transmitting. `logPacket` is reused on the next successful tick,
so an asynchronous logger must own a copy rather than retain this pointer.
`logging_sample_count` counts eligible snapshots after the command; it does not
count flash writes. The SPI enqueue/writer is still a placeholder. When logging
is added, startup must continue to preserve existing flash contents while the
gate is disabled; `BEGIN_LOG` itself performs no erase, probe or program.

## Debugger watches

- `gnss`: valid/invalid/duplicate fix counts, framing/checksum errors, availability
  and the local millisecond time of the last new valid epoch.
- `gnss_uart_fault_count`, `uart1.rx_error_count`, `uart1.rx_drop_count`.
- `logging_enabled`, `logging_sample_count`.
- `telemetry_stats`: processed ticks, records, queued transmissions, and mutually
  exclusive skip reasons for each tick (no fix, stale, unavailable, busy, queue).
- `timer2_ticks`, `timer2_coalesced_tick_count`: elapsed timer interrupts and ticks
  merged because foreground processing had not yet consumed the previous flag.
  Missed ticks are counted, not replayed as a burst of transmissions.
- `rak_tx_status`, `rak_tx_confirmed_count`, `rak_error_count`,
  `rak_timeout_count`, `rak_last_response`.

## Receiver

Use `examples/lora_receiver/lora_receiver.ino` with `telemetry_decode.h` in the
same sketch folder, a Heltec WiFi LoRa 32 V4, and RadioLib 7.4.0 or newer. The
sketch was built with Heltec-esp32 3.0.3 and RadioLib 7.8.1. Open the serial
monitor at 115200. Both radios use 915 MHz, SF12, 125 kHz bandwidth, CR4/8,
eight-symbol preamble, and the private sync word.
These SF12 settings take about three seconds per 44-byte transmission, so RF
sends run slower than the 1 Hz logging schedule and busy skips are expected.

The sketch rejects radio CRC errors, validates payload length and position,
prints position, accuracy, velocity, status and sequence, and reports sequence
gaps/duplicates/backward changes. Gaps count records not received; they include
transmitter-side skipped sends, so they do not by themselves measure RF loss.
A backward sequence may mean reboot or reordering; the receiver starts a new
baseline. Sequence numbers alone cannot identify a reboot unambiguously.

The RF payload is exactly **44 bytes**, with little-endian multibyte fields:

| Offset | Field | Type / units |
| --- | --- | --- |
| 0 | GNSS time of week | uint32, ms |
| 4 | Latitude | int32, degrees x 10^7 |
| 8 | Longitude | int32, degrees x 10^7 |
| 12 | Altitude MSL | int32, mm |
| 16, 20, 24 | Velocity north, east, down | int32 each, mm/s |
| 28, 32 | Horizontal, vertical accuracy | uint32 each, mm |
| 36, 37, 38, 39 | Fix type, satellites, fix flags, time-valid flags | uint8 each |
| 40 | Record sequence | uint32 |

The flash record's CRC is not included in this RF payload. Local RAK
`+EVT:TXP2P DONE` confirms TX completion; it is not a receiving-radio ACK.

For hardware verification, compare `rfPacket` with the receiver's printed
coordinates/units, verify records prepare at 1 Hz (RF sequences may have gaps),
send `BEGIN_LOG` to enable logging eligibility, disconnect GNSS to check
stale suppression, and temporarily remove radio availability to check that
records continue preparing while RF skip counters increase. No hardware test is
performed by the host tests in `test/`.
