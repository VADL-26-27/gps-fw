#!/bin/sh
set -eu
# No heap allocations are used by these tests. LeakSanitizer cannot run under
# the ptrace-based workspace runner; address/undefined-behavior checks remain.
ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=0}
export ASAN_OPTIONS
cd "$(dirname "$0")/.."
mkdir -p build/tests
cc=${HOST_CC:-cc}
flags='-std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer'
$cc $flags -Iinc tests/test_protocol.c src/rak3172_protocol.c src/telemetry_frame.c src/ground_rx_fifo.c -o build/tests/protocol
build/tests/protocol
for name in task driver; do
  $cc $flags -Wno-int-to-pointer-cast -DSTM32F411xE -Itests/fakes -Iinc -Icmsis \
    -include tests/fakes/hardware.h tests/test_$name.c \
    src/rak3172_protocol.c src/telemetry_frame.c -o build/tests/$name
  build/tests/$name
done
