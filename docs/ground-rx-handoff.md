# Ground receive handoff

The LoRa task decodes each valid `+EVT:RXP2P:` payload and calls
`ground_rx_fifo_try_push()` before reusing its UART line buffer. Records contain
a byte length and up to 255 raw bytes, including embedded zero bytes. They are
not GPS fixes and do not use `gps_structs.h`. No received commands are executed.

`src/ground_rx_fifo.c` is a placeholder: it returns false and stores nothing.
`received_packets` counts recognized packets; `rx_queued` counts accepted
handoffs and `rx_dropped` counts rejected handoffs (including the placeholder).
A rejected handoff does not prevent the radio from re-arming reception.

To connect the eventual FIFO, replace the body of `ground_rx_fifo_try_push()`:

1. Initialize a bounded queue before reception starts, or return false while
   it is unavailable.
2. Copy the entire record into the queue before returning. The passed pointer
   refers to temporary task storage and must not be retained.
3. Return immediately: true on enqueue, false when unavailable/full. For a
   FreeRTOS queue whose item size is `sizeof(ground_rx_record_t)`, use
   `xQueueSend(queue, record, 0) == pdPASS`.
4. Have the future SD task dequeue records and write exactly `length` payload
   bytes using the chosen log format. SD I/O must not run in this callback.

The LoRa state machine needs no changes when replacing this placeholder. The
SD task and queue allocation are still unimplemented. The RF configuration in
`main()` is still NULL, so the radio remains offline until configured.
