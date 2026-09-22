# Bring-up and Acceptance Tests

## Bring-up order

1. Verify clocks, SWD, and basic GPIO.
2. Verify USART6 DMA ring with recorded/simulated NMEA at 9,600 bit/s.
3. Verify parser produces correct `gps_fix_t` values from GGA/VTG test data.
4. Verify mailbox atomicity under repeated producer/consumer updates.
5. Verify USART1 DMA and RAK3172 AT synchronization at 115,200 bit/s.
6. Verify P2P TX, receive-window rearming, and ground packet parsing.
7. Verify SDIO initialization, FatFs mount, DMA write completion, card removal, and recovery.
8. Verify IWDG reset behavior for deliberately stalled GPS and LoRa tasks.

## Acceptance criteria

- GNSS data is not lost at the configured output rate during LoRa and SD activity.
- A valid, fresh telemetry frame is attempted at the configured 1 Hz deadline.
- No ISR parses NMEA, writes FatFs, or waits for UART/DMA completion.
- Active DMA buffers are never modified before completion.
- CRC and sequence handling reject malformed telemetry/commands.
- Ground commands are ordered, bounded, validated, logged, and rejected safely when unauthorized or malformed.
- SD failure does not stop telemetry; radio/GNSS failures recover or enter observable offline states.
- DMA, UART, queue-overflow, and command-rejection counters are observable in logs/diagnostics.
