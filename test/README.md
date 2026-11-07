# Host tests

From Bash (including MSYS2 or Git Bash on Windows), with native `gcc` and `g++`
on PATH:

```bash
bash test/run.sh
```

Sources and UART/timer mocks live here. Generated executables go into `build/`.
These are host simulations; they do not flash hardware.

- `test_gps_validation.c`: packet validation, field decoding, UTC conversion, CRC.
- `test_pipeline.c`: split headers, damaged frames, other UBX messages, frame
  timeout, duplicate epochs, fix loss, freshness/wraparound, 1 Hz snapshots,
  record sequence numbers, radio skips/queue failures, no-lock transmission
  without a log record, and recovery when fresh fixes return.
- `test_lora.c`: RAK configuration, command encoding, fragmented UART replies,
  TX completion, errors, timeouts, recovery, and no-lock text encoding.
- `test_receiver.cpp`: the actual Heltec payload decoder, sequence tracker,
  and exact no-lock status recognition.
- `test_logging_control.c`: begin/end/dump/clear/status commands, disabled boot,
  split/malformed commands, command order, UART faults, dump exclusivity, and
  nonblocking acknowledgment retries.
- `test_log.c`: asynchronous flash model, owned record copies, queue overflow,
  partial flush/end, dump backpressure, reboot and torn-write protection, and
  clearing the entire log region while preserving sector zero.
- `test_gps_log.py`: decode the C logger's actual dump using Python/zlib; verify
  CSV values, CRCs, corrupt-page recovery, and interrupted sessions.

`telemetry_fixture.h` is a shared independent wire-format vector. The LoRa test
checks that the transmitter emits it, and the receiver test checks its decoded
fields and units. On-target firmware builds are separate (`make all`).
