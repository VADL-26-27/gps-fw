#!/usr/bin/env bash
set -euo pipefail

cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
mkdir -p build

exe_suffix=""
case "${OSTYPE:-}" in
    msys*|cygwin*|win32*) exe_suffix=".exe" ;;
esac

run_test() {
    local name="$1" compiler="$2"
    shift 2
    local executable="build/${name}${exe_suffix}"
    "$compiler" -Wall -Wextra -Werror "$@" -o "$executable"
    "$executable"
}

run_test test_gps_validation gcc -std=c11 -Iinc \
    src/gps.c test/test_gps_validation.c

run_test test_pipeline gcc -std=c11 -Iinc \
    src/gps.c src/gps_stream.c src/telemetry.c test/test_pipeline.c

run_test test_lora gcc -std=c11 -Itest/lora-test-include -Iinc \
    src/lora.c test/test_lora.c

run_test test_receiver g++ -std=c++11 test/test_receiver.cpp

run_test test_logging_control gcc -std=c11 -Itest/lora-test-include -Iinc \
    src/logging_control.c test/test_logging_control.c
