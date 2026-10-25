.DEFAULT_GOAL := all

CC      := arm-none-eabi-gcc
OBJCOPY := arm-none-eabi-objcopy
SIZE    := arm-none-eabi-size

OUT      := build/firmware
LDSCRIPT := STM32F413xx_FLASH.ld

RTOS_DIR  := rtos
RTOS_PORT := $(RTOS_DIR)/portable/GCC/ARM_CM4F

CPUFLAGS := -mcpu=cortex-m4 -mthumb \
            -mfpu=fpv4-sp-d16 -mfloat-abi=hard

CPPFLAGS := -DSTM32F413xx \
            -Iinc -Icmsis -Isrc \
            -I$(RTOS_DIR)/include \
            -I$(RTOS_PORT)

CFLAGS := $(CPUFLAGS) -std=c11 -O0 -g \
          -ffunction-sections -fdata-sections \
          -Wall -Wextra -MMD -MP

LDFLAGS := $(CPUFLAGS) -T$(LDSCRIPT) \
           -nostartfiles -nostdlib \
           -Wl,--gc-sections \
           -Wl,-Map=$(OUT).map

LDLIBS := -Wl,--start-group -lm -lc -lgcc -Wl,--end-group

APP_SOURCES := $(wildcard src/*.c)

RTOS_SOURCES := \
    $(RTOS_DIR)/tasks.c \
    $(RTOS_DIR)/queue.c \
    $(RTOS_DIR)/list.c \
    $(RTOS_DIR)/timers.c \
    $(RTOS_DIR)/event_groups.c \
    $(RTOS_DIR)/stream_buffer.c \
    $(RTOS_PORT)/port.c \
    $(RTOS_DIR)/portable/MemMang/heap_4.c

C_SOURCES := $(APP_SOURCES) $(RTOS_SOURCES)

# Preserve source directories to avoid object-name collisions.
# Example: rtos/tasks.c -> build/rtos/tasks.o
C_OBJECTS := $(patsubst %.c,build/%.o,$(C_SOURCES))
OBJECTS   := $(C_OBJECTS) build/startup.o
DEPS      := $(C_OBJECTS:.o=.d)

.PHONY: all clean size

all: $(OUT).elf $(OUT).bin $(OUT).hex
	$(SIZE) $(OUT).elf

build/%.o: %.c Makefile
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

build/startup.o: startup_stm32f413xx.s Makefile
	mkdir -p $(@D)
	$(CC) $(CPUFLAGS) -g -c $< -o $@

$(OUT).elf: $(OBJECTS) $(LDSCRIPT) Makefile
	$(CC) $(LDFLAGS) $(OBJECTS) $(LDLIBS) -o $@

$(OUT).bin: $(OUT).elf
	$(OBJCOPY) -O binary $< $@

$(OUT).hex: $(OUT).elf
	$(OBJCOPY) -O ihex $< $@

size: $(OUT).elf
	$(SIZE) $<

clean:
	rm -rf build

-include $(DEPS)