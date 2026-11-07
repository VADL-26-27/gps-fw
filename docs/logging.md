# GPS SPI flash logging

The GPS board uses its W25Q256JV through the existing asynchronous SPI/DMA
driver. `src/log.c` follows the flight board's logger approach: buffered writes,
readback verification, CRC-protected blocks, an explicit serial dump, and no
automatic erase. The log format is specific to GPS; use `scripts/gps_log.py`
rather than the flight board's decoder.

## Commands on USART2

Use 115200 baud, 8N1, PA2 TX / PA3 RX and a common ground. Send these exact,
case-sensitive commands terminated by CR, LF, or CRLF:

| Command | Behavior |
| --- | --- |
| `LOG_STATUS` | Reports CHECKING, READY, RECORDING, FINISHING, COMPLETE, OCCUPIED, FULL, CLEARING, DUMPING, or ERROR. |
| `BEGIN_LOG` | Starts a session when READY; replies `LOGGING_ENABLED`. Repeating while recording is harmless. Otherwise replies `BEGIN_LOG REFUSED`. |
| `LOG_END` | Immediately disables new logging enqueues, then flushes pending records and writes a footer. Replies `LOG FINISHING`; poll status until `LOG COMPLETE`. |
| `LOG_DUMP` | Streams the log without erasing it. Refused while recording, finishing, checking, or clearing. |
| `LOG_CLEAR` | Explicitly erases the logging region. Refused while recording, finishing, checking, dumping, or already clearing. Poll status until `LOG READY`. |

Only `BEGIN_LOG` enables recording. Boot/reset always disables it. Startup
probes the flash and reads the first log sector, without programming or erasing.
An existing nonblank first sector produces `LOG OCCUPIED`, preventing a new
session from overwriting it. Every additional sector is also checked completely
blank before the logger writes it. A completed session requires `LOG_CLEAR`
before another `BEGIN_LOG`; this implementation keeps one session at a time.

Clear checks **all** logging sectors, from the end toward the beginning, and
erases nonblank sectors. It handles data beyond a blank gap and preserves the
log prefix until last if clearing is interrupted. This may take time; it runs
without blocking GNSS/LoRa servicing. Sector zero is reserved and never erased
or logged into. If reset interrupts clearing, send `LOG_CLEAR` again and wait
for READY before starting a new session.

## Sampling, buffering, and power loss

The existing 1 Hz `telemetry_tick()` prepares a record from a fresh usable fix.
Only while `logging_enabled` is true does main copy that record into the
logger's eight-record queue (two groups of four). Radio busy/failure does not
prevent logging. Unusable/stale fixes still produce no logging record under
the existing GNSS policy. GNSS updates and LoRa servicing continue during
logging, clearing, and dumping.
While GNSS is unavailable, unusable or stale, LoRa attempts to send
`NO LOCK FOUND` when ready and idle; these status messages are never logged.

Four records fit in one 256-byte flash page, so normal 1 Hz logging programs
one data page every four seconds. A partial page flushes after five seconds,
or immediately on `LOG_END`. The pending write owns a stable serialized page
until the driver's page readback verification completes. A full RAM queue
drops new samples and increments `logger_stats.dropped_records`.

Power loss can lose records still in RAM or in the active flash program.
Previously completed pages remain extractable; an interrupted page is detected
by its CRC and skipped by the decoder. Send `LOG_END` and wait for COMPLETE
before powering off when a clean finish is possible. Logging stops at capacity
instead of wrapping or erasing. FULL logs can be dumped without a footer.
With full four-record pages, capacity is about **6.1 days at 1 Hz**; partial
pages reduce it.

Debugger watches: `logging_enabled`, `logger_status`, `logger_stats` and
`logging_sample_count`. The latter counts successful RAM enqueues;
`logger_stats.written_records` counts records verified in flash. Accepted minus
written records can remain in RAM following an error/full condition.

## Download and decode

1. Send `LOG_END`, then `LOG_STATUS` until COMPLETE. After an unexpected reset,
   OCCUPIED is also safe to dump. Do not clear first.
2. Close Serial Monitor so the download script can open the COM port.
3. Install the serial dependency once, then download and decode:

```bash
python -m pip install pyserial
python scripts/gps_log.py dump --port COM7 --output gps-log.bin
python scripts/gps_log.py decode gps-log.bin --csv gps-log.csv
```

Both output filenames must be new; the tool refuses to replace an existing
file. A failed download retains its partial raw file. The decoder checks page
and record CRCs, reports missing pages/footer and dropped records, and writes
verified records with coordinates in degrees, altitude/accuracy in meters,
velocity in m/s, UTC milliseconds, GNSS time of week, flags, and sequence.
`unix_time_ms == 0` means UTC was unavailable. A warning-free decode with a
footer is the clean completion case; retain the raw binary alongside the CSV.
Full-flash download at 115200 baud takes roughly 49 minutes.

4. After checking the saved data, reopen Serial Monitor, send `LOG_CLEAR`, and
   wait for READY. Then `BEGIN_LOG` can start the next session without reboot.

## Binary formats

Dump framing: `\r\nGPS LOG DUMP 1\r\n`, then one `D` byte followed by exactly
4096 raw bytes for each nonblank contiguous sector starting at address 4096.
The first completely blank sector ends the stream. Final `E` means successful
transport completion; `X` means flash read failure. Payload bytes can themselves
contain D/E/X, so lengths, rather than byte searches, delimit sector contents.
The saved binary contains just the sectors, without tags/banner. Commands sent
during the binary stream are discarded; no other foreground code should print
to USART2 during a dump. GNSS and LoRa IRQs remain enabled.

Each 256-byte page contains these little-endian fields:

| Offset | Field |
| --- | --- |
| 0 | Magic `GPS1` (`0x31535047`) |
| 4 | Kind: 1 session header, 2 data, 3 session footer |
| 8 | Format version: 1 |
| 12 | Page sequence, starting at zero |
| 16 | Record count: 0 for header/footer, 1..4 for data |
| 20 | Number of records verified before this page |
| 24 | Dropped-record count at page creation |
| 28..251 | Up to four 56-byte records; unused bytes are `0xFF` |
| 252 | CRC-32/ISO-HDLC of bytes 0..251 |

Each record is the canonical mailbox format: uint64 UTC at 0, the 40-byte fix
at 8 (same field order as the RF payload), uint32 sequence at 48, and CRC-32
of record bytes 0..51 at 52. All multibyte fields are explicitly serialized
little endian, independent of C struct padding. Both CRCs use reflected
polynomial `0xEDB88320`, initial/final XOR `0xFFFFFFFF`, matching Python/zlib.

Host tests model delayed flash operations, torn writes, reboot preservation,
copy ownership, UART backpressure, command sequencing/corruption, and decoding
the actual C logger's output. They do not validate the PCB wiring or flash chip;
test a short record/end/dump/clear cycle on the board before deployment.
