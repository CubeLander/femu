CC ?= gcc
AR ?= ar

CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2
CPPFLAGS ?= -Iinclude -D_POSIX_C_SOURCE=200809L
THREAD_FLAGS ?= -pthread

BUILD_DIR := build
SRCS := $(wildcard src/*.c)
OBJS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)
LIB := $(BUILD_DIR)/librv32emu.a
EMU_BIN := $(BUILD_DIR)/rv32emu
TEST_BINS := $(BUILD_DIR)/test_platform $(BUILD_DIR)/test_run $(BUILD_DIR)/test_system $(BUILD_DIR)/test_loader

all: $(LIB)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(THREAD_FLAGS) -MMD -MP -c $< -o $@

$(LIB): $(OBJS)
	rm -f $@
	$(AR) rcs $@ $^

$(BUILD_DIR)/test_platform: tests/test_platform.c $(LIB) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(THREAD_FLAGS) $< $(LIB) -o $@

$(BUILD_DIR)/test_run: tests/test_run.c $(LIB) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(THREAD_FLAGS) $< $(LIB) -o $@

$(BUILD_DIR)/test_system: tests/test_system.c $(LIB) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(THREAD_FLAGS) $< $(LIB) -o $@

$(BUILD_DIR)/test_loader: tests/test_loader.c $(LIB) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(THREAD_FLAGS) $< $(LIB) -o $@

$(EMU_BIN): tools/rv32emu_main.c $(LIB) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(THREAD_FLAGS) $< $(LIB) -o $@

rv32emu: $(EMU_BIN)

test: $(TEST_BINS)
	./$(BUILD_DIR)/test_platform
	./$(BUILD_DIR)/test_run
	./$(BUILD_DIR)/test_system
	./$(BUILD_DIR)/test_loader

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)

.PHONY: all rv32emu test clean
