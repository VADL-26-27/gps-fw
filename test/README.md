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
  record sequence numbers, and accounting for radio skips/queue failures.
- `test_lora.c`: RAK configuration, command encoding, fragmented UART replies,
  TX completion, errors, timeouts, and recovery.
- `test_receiver.cpp`: the actual Heltec payload decoder and sequence tracker.
- `test_logging_control.c`: disabled-at-boot logging gate, exact `BEGIN_LOG`
  lines, split commands, repeated commands, reset, malformed input, UART faults,
  and nonblocking acknowledgment retries.

`telemetry_fixture.h` is a shared independent wire-format vector. The LoRa test
checks that the transmitter emits it, and the receiver test checks its decoded
fields and units. On-target firmware builds are separate (`make all`).
