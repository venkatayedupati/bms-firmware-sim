CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -g -O0
LDFLAGS ?= -lpthread

BUILD_DIR := build

CORE_SRCS := \
	src/osal/osal_posix.c \
	src/can/can_hal_virtual.c \
	src/can/can_protocol.c \
	src/bms/cell_model.c \
	src/bms/soc_estimator.c \
	src/bms/fault_manager.c \
	src/util/logger.c \
	src/tasks/app_context.c \
	src/tasks/task_sensor.c \
	src/tasks/task_soc.c \
	src/tasks/task_fault.c \
	src/tasks/task_can.c \
	src/tasks/task_watchdog.c

SIM_SRCS := $(CORE_SRCS) src/main.c
TEST_SRCS := $(CORE_SRCS) test/test_main.c test/test_soc_estimator.c \
	test/test_fault_manager.c test/test_can_protocol.c

.PHONY: all sim test clean run

all: sim test

sim: $(BUILD_DIR)/bms_sim

test: $(BUILD_DIR)/bms_tests
	./$(BUILD_DIR)/bms_tests

run: sim
	./$(BUILD_DIR)/bms_sim $(SCENARIO)

$(BUILD_DIR)/bms_sim: $(SIM_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SIM_SRCS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/bms_tests: $(TEST_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(TEST_SRCS) -o $@ $(LDFLAGS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)
