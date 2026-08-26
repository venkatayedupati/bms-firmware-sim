CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -g -O0
LDFLAGS ?= -lpthread

BUILD_DIR := build

# Sanitizer builds catch different bug classes than the plain build:
# TSan catches unsynchronized concurrent memory access (data races); ASan
# catches memory-safety errors (out-of-bounds, use-after-free); UBSan catches
# undefined behavior (signed overflow, misaligned access, etc). TSan and ASan
# instrument memory access differently and can't be linked into the same
# binary, hence two separate build trees. -O1 (not -O0) because both
# sanitizers rely on some optimization to produce accurate stack traces.
TSAN_BUILD_DIR := build-tsan
TSAN_FLAGS := -fsanitize=thread -fno-omit-frame-pointer -g -O1
ASAN_BUILD_DIR := build-asan
ASAN_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer -g -O1

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

.PHONY: all sim test clean run sanitize \
	tsan-sim tsan-test tsan-run asan-sim asan-test

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

# --- Sanitizer builds ---
# TSan only observes races on interleavings it actually schedules in a given
# run, same as the manual `make run` looping that caught the app_context
# startup bug -- it's not a static guarantee, so tsan-run repeats the
# simulator several times to raise the odds of hitting a bad interleaving.
# It would NOT have caught that specific bug: latest_reading was always
# read/written under the mutex, so there was no unsynchronized access for
# TSan to flag -- it was a stale-initial-value logic bug, not a data race.

$(TSAN_BUILD_DIR)/bms_sim: $(SIM_SRCS) | $(TSAN_BUILD_DIR)
	$(CC) $(CFLAGS) $(TSAN_FLAGS) $(SIM_SRCS) -o $@ $(LDFLAGS)

$(TSAN_BUILD_DIR)/bms_tests: $(TEST_SRCS) | $(TSAN_BUILD_DIR)
	$(CC) $(CFLAGS) $(TSAN_FLAGS) $(TEST_SRCS) -o $@ $(LDFLAGS)

$(TSAN_BUILD_DIR):
	mkdir -p $(TSAN_BUILD_DIR)

tsan-sim: $(TSAN_BUILD_DIR)/bms_sim

tsan-test: $(TSAN_BUILD_DIR)/bms_tests
	./$(TSAN_BUILD_DIR)/bms_tests

tsan-run: tsan-sim
	for i in 1 2 3 4 5 6 7 8; do ./$(TSAN_BUILD_DIR)/bms_sim || exit 1; done

$(ASAN_BUILD_DIR)/bms_sim: $(SIM_SRCS) | $(ASAN_BUILD_DIR)
	$(CC) $(CFLAGS) $(ASAN_FLAGS) $(SIM_SRCS) -o $@ $(LDFLAGS)

$(ASAN_BUILD_DIR)/bms_tests: $(TEST_SRCS) | $(ASAN_BUILD_DIR)
	$(CC) $(CFLAGS) $(ASAN_FLAGS) $(TEST_SRCS) -o $@ $(LDFLAGS)

$(ASAN_BUILD_DIR):
	mkdir -p $(ASAN_BUILD_DIR)

asan-sim: $(ASAN_BUILD_DIR)/bms_sim
	./$(ASAN_BUILD_DIR)/bms_sim

asan-test: $(ASAN_BUILD_DIR)/bms_tests
	./$(ASAN_BUILD_DIR)/bms_tests

sanitize: tsan-run tsan-test asan-sim asan-test

clean:
	rm -rf $(BUILD_DIR) $(TSAN_BUILD_DIR) $(ASAN_BUILD_DIR)
