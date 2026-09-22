#!/bin/bash
set -e

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
            echo "Usage: $0 [--flash]" >&2
            exit 2
            ;;
    esac
done

echo "============================== Building STM32F411 firmware ================================"

# Toolchain

CC=arm-none-eabi-gcc
OBJCOPY=arm-none-eabi-objcopy
SIZE=arm-none-eabi-size

# Flags

CFLAGS="
-mcpu=cortex-m4
-mthumb
-O0 
-g 
-ffunction-sections 
-fdata-sections 
-Wall
-Wextra
-mfpu=fpv4-sp-d16
-mfloat-abi=hard
"

DEFINES="
-DSTM32F411xE
"

INCLUDES="
-Iinc 
-Icmsis 
-Isrc
-Irtos/include
-Irtos/portable/GCC/ARM_CM4F
"

# Linker

LDSCRIPT=STM32F411xx_FLASH.ld

# Output

OUT=build/firmware

mkdir -p build

echo "======================================= Compiling ========================================="

$CC $CFLAGS $DEFINES $INCLUDES -c src/main.c -o build/main.o
$CC $CFLAGS $DEFINES $INCLUDES -c src/system_stm32f4xx.c -o build/system.o
$CC $CFLAGS $DEFINES $INCLUDES -c src/syscalls.c -o build/syscalls.o
$CC $CFLAGS $DEFINES $INCLUDES -c rtos/list.c -o build/freertos_list.o
$CC $CFLAGS $DEFINES $INCLUDES -c rtos/queue.c -o build/freertos_queue.o
$CC $CFLAGS $DEFINES $INCLUDES -c rtos/tasks.c -o build/freertos_tasks.o
$CC $CFLAGS $DEFINES $INCLUDES -c rtos/portable/GCC/ARM_CM4F/port.c -o build/freertos_port.o
$CC $CFLAGS $DEFINES $INCLUDES -c rtos/portable/MemMang/heap_4.c -o build/freertos_heap.o

$CC $CFLAGS \
-c src/startup_stm32f411xe.s \
-o build/startup.o

echo "======================================== Linking =========================================="

$CC $CFLAGS \
build/main.o \
build/system.o \
build/syscalls.o \
build/startup.o \
build/freertos_list.o \
build/freertos_queue.o \
build/freertos_tasks.o \
build/freertos_port.o \
build/freertos_heap.o \
-T $LDSCRIPT \
-nostartfiles \
-nostdlib \
-Wl,--gc-sections \
-Wl,-Map=build/firmware.map \
-o $OUT.elf

echo "========================================= Size ============================================"

$SIZE $OUT.elf

echo "================================= Generating binary/hex ==================================="

$OBJCOPY -O binary $OUT.elf $OUT.bin
$OBJCOPY -O ihex   $OUT.elf $OUT.hex

if "$DO_FLASH"; then
echo "======================================= Flashing =========================================="

# dfu-util -a 0 -s 0x08000000:force:leave -D $OUT.bin
st-flash --reset write "$OUT.bin" 0x08000000
fi

echo "========================================= Done ============================================"
