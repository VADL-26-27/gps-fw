#!/bin/sh
set -eu
# No heap allocations are used by these tests. LeakSanitizer cannot run under
# the ptrace-based workspace runner; address/undefined-behavior checks remain.
ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=0}
export ASAN_OPTIONS
cd "$(dirname "$0")/.."
mkdir -p build/tests
cc=${HOST_CC:-cc}
flags='-std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -Itests/fakes'
$cc $flags -Iinc tests/test_protocol.c src/rak3172_protocol.c src/telemetry_frame.c src/ground_rx_fifo.c -lm -o build/tests/protocol
build/tests/protocol
for name in task driver; do
  extra='src/usart_board.c src/usart_irq.c'
  if [ "$name" = task ]; then extra='src/gps_mailbox.c tests/fakes/queue.c'; fi
  $cc $flags -Wno-int-to-pointer-cast -DSTM32F413xx -Itests/fakes -Iinc -Icmsis \
    -include tests/fakes/hardware.h tests/test_$name.c \
    src/rak3172_protocol.c src/telemetry_frame.c src/ring_buf.c $extra -lm -o build/tests/$name
  build/tests/$name
done
