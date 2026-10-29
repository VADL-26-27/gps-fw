#!/bin/bash
set -euo pipefail

# Run from the directory containing this script and the Makefile.
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"

DO_FLASH=false

for arg in "$@"; do
    case "$arg" in
        --flash|-f)
            DO_FLASH=true
            ;;
        --help|-h)
            echo "Usage: $0 [--flash]"
            echo "  no option    Build firmware only"
            echo "  --flash, -f  Build and flash firmware"
            exit 0
            ;;
        *)
            echo "Unknown option: $arg" >&2
            exit 2
            ;;
    esac
done

make all

if "$DO_FLASH"; then
    st-flash --reset write build/firmware.bin 0x08000000
fi
