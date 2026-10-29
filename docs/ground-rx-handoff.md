# Ground receive handoff

The LoRa task decodes each valid `+EVT:RXP2P:` payload and calls
`ground_rx_fifo_try_push()` before reusing its UART line buffer. Records contain
a byte length and up to 255 raw bytes, including embedded zero bytes. They are
not GPS fixes and do not use `gps_structs.h`. No received commands are executed.

`src/ground_rx_fifo.c` remains a placeholder handoff for the SD branch's FIFO.
It returns false and stores nothing until connected to that queue.
`received_packets` counts recognized packets; `rx_queued` counts accepted
handoffs and `rx_dropped` counts rejected handoffs. A rejected handoff does not
prevent the radio from re-arming reception.

The SD branch should replace the placeholder with a nonblocking, copying enqueue
using its existing queue interface. The SD task owns dequeuing and storage I/O.

GPS telemetry uses a separate one-element FreeRTOS mailbox. GPS producers call
`gps_mailbox_publish()` with a fix and its acquisition tick. It overwrites an
unsent value and notifies LoRa. LoRa is the sole consumer and retains a pending
fix across failures, replacing it when a newer mailbox value arrives. The
mailbox must be initialized before starting tasks or publishing data.

No GPS task/FIFO implementation is present in this checkout. The RF
configuration in `main()` is still NULL, so the radio remains offline until
configured.
