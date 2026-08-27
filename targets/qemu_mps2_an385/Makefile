# Cross-compiles this project's application code (tasks/, bms/, can/,
# unchanged from the host build) plus src/osal/osal_freertos.c and a real
# FreeRTOS kernel (vendored at third_party/FreeRTOS-Kernel) for QEMU's
# mps2-an385 (Cortex-M3) machine.
#
# Needs the official ARM GNU Toolchain (bundles newlib, including
# newlib-nano and its nosys/nano specs): Homebrew's arm-none-eabi-gcc
# formula is a bare compiler with --without-headers, no libc at all, so it
# can't build this. `brew install --cask gcc-arm-embedded` installs the
# real one (needs an interactive sudo password for its .pkg installer).
CC := arm-none-eabi-gcc
SIZE := arm-none-eabi-size
OBJCOPY := arm-none-eabi-objcopy

ROOT := ../..
FREERTOS_DIR := $(ROOT)/third_party/FreeRTOS-Kernel
FREERTOS_PORT_DIR := $(FREERTOS_DIR)/portable/GCC/ARM_CM3

BUILD_DIR := build
IMAGE := $(BUILD_DIR)/bms_sim.elf

INCLUDES := \
	-I. \
	-I$(ROOT)/src \
	-I$(FREERTOS_DIR)/include \
	-I$(FREERTOS_PORT_DIR)

CFLAGS := -std=c11 -Wall -Wextra \
	-mthumb -mcpu=cortex-m3 -ffreestanding \
	-g -Os -ffunction-sections -fdata-sections \
	$(INCLUDES)

LDFLAGS := -T linker.ld \
	-nostartfiles --specs=nano.specs --specs=nosys.specs \
	-Wl,--gc-sections -Wl,-Map=$(BUILD_DIR)/bms_sim.map

# This project's shared, portable application code -- identical to the
# host build's CORE_SRCS in the root Makefile, minus osal_posix.c and
# can_hal_virtual.c (host-only) and main.c (host CLI entry point, replaced
# by board_main.c here).
APP_SRCS := \
	$(ROOT)/src/can/can_hal_virtual.c \
	$(ROOT)/src/can/can_protocol.c \
	$(ROOT)/src/bms/cell_model.c \
	$(ROOT)/src/bms/soc_estimator.c \
	$(ROOT)/src/bms/fault_manager.c \
	$(ROOT)/src/util/logger.c \
	$(ROOT)/src/tasks/app_context.c \
	$(ROOT)/src/tasks/task_sensor.c \
	$(ROOT)/src/tasks/task_soc.c \
	$(ROOT)/src/tasks/task_fault.c \
	$(ROOT)/src/tasks/task_can.c \
	$(ROOT)/src/tasks/task_watchdog.c \
	$(ROOT)/src/osal/osal_freertos.c

# can_hal_virtual.c is reused as-is here: it only depends on the OSAL and
# <string.h>/<stdlib.h>, no POSIX/pthread dependency, so it's just as
# portable as the rest of the application code -- this target has no real
# CAN peripheral to bind to instead.

FREERTOS_SRCS := \
	$(FREERTOS_DIR)/tasks.c \
	$(FREERTOS_DIR)/list.c \
	$(FREERTOS_DIR)/queue.c \
	$(FREERTOS_DIR)/timers.c \
	$(FREERTOS_DIR)/event_groups.c \
	$(FREERTOS_DIR)/portable/MemMang/heap_4.c \
	$(FREERTOS_PORT_DIR)/port.c

BOARD_SRCS := \
	startup.c \
	uart.c \
	syscalls.c \
	freertos_hooks.c \
	stack_report.c \
	board_main.c

SRCS := $(APP_SRCS) $(FREERTOS_SRCS) $(BOARD_SRCS)
OBJS := $(addprefix $(BUILD_DIR)/,$(notdir $(SRCS:.c=.o)))
vpath %.c $(sort $(dir $(SRCS)))

.PHONY: all qemu clean

all: $(IMAGE)

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(IMAGE): $(OBJS) linker.ld | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) -o $@
	$(SIZE) $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# -nographic: no separate graphical window, everything (including the
# UART, via -serial stdio's default) goes to this terminal. Ctrl-A X quits.
qemu: $(IMAGE)
	qemu-system-arm -M mps2-an385 -nographic -kernel $(IMAGE)

clean:
	rm -rf $(BUILD_DIR)
