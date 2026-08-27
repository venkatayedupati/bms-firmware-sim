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

# Static analysis: clang-tidy needs an LLVM install (macOS's system clang
# doesn't ship it; Homebrew's is keg-only, hence CLANG_TIDY defaulting to the
# full path rather than assuming it's on PATH). cppcheck's inline
# `// cppcheck-suppress` comments are silently ignored without
# --inline-suppr -- a common gotcha, not optional here.
CLANG_TIDY ?= /opt/homebrew/opt/llvm/bin/clang-tidy
CPPCHECK ?= cppcheck

# Coverage needs real per-file .o compilation (unlike the single-command
# builds above) so gcov's per-translation-unit .gcno files land somewhere
# predictable. VPATH lets the pattern rule below find each source
# regardless of which src/ subdirectory it actually lives in -- safe only
# because every source file in this project has a unique basename.
COVERAGE_BUILD_DIR := build-coverage
COVERAGE_FLAGS := --coverage -O0
COVERAGE_OBJS := $(addprefix $(COVERAGE_BUILD_DIR)/,$(notdir $(TEST_SRCS:.c=.o)))
vpath %.c $(sort $(dir $(TEST_SRCS)))

# SocketCAN backend: Linux-only (linux/can.h doesn't exist on macOS), not
# part of the default build. Swaps the in-process virtual bus for a real
# vcan0/can0 kernel interface -- see src/can/can_hal_socketcan.c and
# docs/ARCHITECTURE.md "Portability".
SOCKETCAN_BUILD_DIR := build-socketcan
SOCKETCAN_SIM_SRCS := $(filter-out src/can/can_hal_virtual.c,$(SIM_SRCS)) src/can/can_hal_socketcan.c

.PHONY: all sim test clean run sanitize tidy cppcheck-run coverage sim-socketcan run-socketcan \
	freertos-build freertos-run freertos-clean \
	tsan-sim tsan-test tsan-run asan-sim asan-test \
	fuzz-build fuzz-run

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

# --- Static analysis ---
# Bug-finding checks only (bugprone/cert/clang-analyzer/performance/
# portability/misc); most of readability-* too, minus a few checks that
# fight this codebase's established conventions rather than finding
# anything -- see .clang-tidy for the reasoning behind each suppression.
tidy:
	$(CLANG_TIDY) $(SIM_SRCS) -- -std=c11 -Isrc

# --enable=style catches things like a dead variable initializer or an
# always-true loop condition; --inline-suppr honors the
# `// cppcheck-suppress` comments already in the source (without it they're
# silently ignored, not an error -- easy to miss).
cppcheck-run:
	$(CPPCHECK) --enable=warning,performance,portability,style --std=c11 -Isrc \
		--suppress=missingIncludeSystem --inline-suppr --error-exitcode=1 \
		$(SIM_SRCS) test/*.c

# --- Coverage ---
# gcov must be the exact version paired with whatever compiled the .gcno
# files, or lcov silently reports garbage (percentages over 100%, not an
# error) instead of failing loudly. Hosted CI images commonly ship several
# gcc versions side by side with a single generic `gcov` on PATH that can
# mismatch the one `cc`/`gcc` resolves to; a single-toolchain dev machine or
# container doesn't have this ambiguity, which is exactly why this can pass
# clean locally and still be wrong in CI. Prefer the version-suffixed
# binary matching $(CC)'s reported version; fall back to plain gcov where
# that's all that exists (e.g. Xcode's toolchain).
GCOV_TOOL := $(shell v=$$($(CC) -dumpversion 2>/dev/null | cut -d. -f1); \
	if command -v gcov-$$v >/dev/null 2>&1; then echo gcov-$$v; else echo gcov; fi)

$(COVERAGE_BUILD_DIR)/%.o: %.c | $(COVERAGE_BUILD_DIR)
	$(CC) $(CFLAGS) $(COVERAGE_FLAGS) -c $< -o $@

$(COVERAGE_BUILD_DIR):
	mkdir -p $(COVERAGE_BUILD_DIR)

$(COVERAGE_BUILD_DIR)/bms_tests: $(COVERAGE_OBJS)
	$(CC) $(COVERAGE_FLAGS) $(COVERAGE_OBJS) -o $@ $(LDFLAGS)

# Coverage of src/ only -- test/test.h's macro-shim "code" and the test
# suites themselves aren't what this measures the coverage of.
coverage: $(COVERAGE_BUILD_DIR)/bms_tests
	./$(COVERAGE_BUILD_DIR)/bms_tests
	lcov --capture --directory $(COVERAGE_BUILD_DIR) --output-file $(COVERAGE_BUILD_DIR)/coverage.info \
		--rc branch_coverage=1 --quiet --ignore-errors unsupported --gcov-tool "$(GCOV_TOOL)"
	lcov --remove $(COVERAGE_BUILD_DIR)/coverage.info '*/test/*' \
		--output-file $(COVERAGE_BUILD_DIR)/coverage.info --rc branch_coverage=1 --quiet
	lcov --list $(COVERAGE_BUILD_DIR)/coverage.info --rc branch_coverage=1

# --- SocketCAN backend (Linux only) ---
sim-socketcan: $(SOCKETCAN_BUILD_DIR)/bms_sim

$(SOCKETCAN_BUILD_DIR)/bms_sim: $(SOCKETCAN_SIM_SRCS) | $(SOCKETCAN_BUILD_DIR)
	$(CC) $(CFLAGS) $(SOCKETCAN_SIM_SRCS) -o $@ $(LDFLAGS)

# Needs a real vcan0/can0 already set up (see docs/ARCHITECTURE.md
# "Portability"); BMS_CAN_IFACE picks the interface, default vcan0.
run-socketcan: sim-socketcan
	./$(SOCKETCAN_BUILD_DIR)/bms_sim

$(SOCKETCAN_BUILD_DIR):
	mkdir -p $(SOCKETCAN_BUILD_DIR)

# --- FreeRTOS port (QEMU mps2-an385, Cortex-M3) ---
# Needs the full ARM GNU Toolchain (bundles newlib) and qemu-system-arm --
# see targets/qemu_mps2_an385/Makefile and docs/ARCHITECTURE.md
# "Portability" for exactly why Homebrew's arm-none-eabi-gcc formula alone
# can't build this.
freertos-build:
	$(MAKE) -C targets/qemu_mps2_an385

freertos-run:
	$(MAKE) -C targets/qemu_mps2_an385 qemu

freertos-clean:
	$(MAKE) -C targets/qemu_mps2_an385 clean

# --- Fuzzing (libFuzzer) ---
# can_protocol.c's unpack_* functions are the one place in this codebase
# that parses attacker/peer-controlled bytes (a real CAN_RAW socket hands
# over whatever id/dlc/data a bus frame carried); see fuzz/fuzz_can_protocol.c.
# libFuzzer needs clang's compiler-rt fuzzer runtime -- Apple's Command Line
# Tools clang doesn't bundle it (verified directly: link fails with
# "libclang_rt.fuzzer_osx.a not found"), so this defaults to Homebrew's LLVM,
# same reasoning as CLANG_TIDY above. Ubuntu's plain `clang` apt package
# bundles it fine, so CI overrides this to `clang`.
FUZZ_CC ?= /opt/homebrew/opt/llvm/bin/clang
FUZZ_BUILD_DIR := build-fuzz
FUZZ_SRCS := src/can/can_protocol.c fuzz/fuzz_can_protocol.c
FUZZ_FLAGS := -fsanitize=fuzzer,address,undefined -g -O1 -std=c11 -Isrc
FUZZ_TIME ?= 60

$(FUZZ_BUILD_DIR):
	mkdir -p $(FUZZ_BUILD_DIR)

$(FUZZ_BUILD_DIR)/fuzz_can_protocol: $(FUZZ_SRCS) | $(FUZZ_BUILD_DIR)
	$(FUZZ_CC) $(FUZZ_FLAGS) $(FUZZ_SRCS) -o $@

fuzz-build: $(FUZZ_BUILD_DIR)/fuzz_can_protocol

# -max_total_time bounds this to a fixed budget (default 60s) instead of
# running forever like a real long-haul fuzzing campaign would; corpus
# persists locally across runs in fuzz/corpus (gitignored) so coverage
# built up in one run isn't thrown away on the next.
fuzz-run: fuzz-build
	mkdir -p fuzz/corpus
	./$(FUZZ_BUILD_DIR)/fuzz_can_protocol -max_total_time=$(FUZZ_TIME) fuzz/corpus

clean:
	rm -rf $(BUILD_DIR) $(TSAN_BUILD_DIR) $(ASAN_BUILD_DIR) $(COVERAGE_BUILD_DIR) $(SOCKETCAN_BUILD_DIR) $(FUZZ_BUILD_DIR)
