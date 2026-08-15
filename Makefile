CC ?= cc
CPPFLAGS ?= -Ih
PROJECT_VERSION := $(shell sed -nE 's/^project\(c_nbt_explorer VERSION ([0-9]+\.[0-9]+\.[0-9]+) LANGUAGES C\)$$/\1/p' CMakeLists.txt)
ifeq ($(strip $(PROJECT_VERSION)),)
$(error Could not read the project version from CMakeLists.txt)
endif
CPPFLAGS += -DCNBT_VERSION=\"$(PROJECT_VERSION)\"
CFLAGS ?= -O2 -g
CFLAGS += -Wall -Wextra -Wpedantic
LDFLAGS ?=
LDLIBS ?= -lz

# dlopen() is part of libSystem on macOS and the Windows API on Windows, but
# older glibc-based Linux systems still require an explicit libdl dependency.
ifeq ($(shell uname -s 2>/dev/null),Linux)
LDLIBS += -ldl
endif
ifeq ($(shell uname -s 2>/dev/null),Darwin)
MACOS_SDK_PATH := $(shell xcrun --sdk macosx --show-sdk-path 2>/dev/null)
ifneq ($(MACOS_SDK_PATH),)
# Keep an Intel Homebrew zlib in /usr/local from shadowing the active SDK's
# system zlib when producing a native Apple silicon binary (and vice versa).
LDFLAGS += -L$(MACOS_SDK_PATH)/usr/lib
endif
endif

SRC_DIR = src
BIN_DIR = bin
INC_DIR = h

SRC = $(wildcard $(SRC_DIR)/*.c)
OBJ = $(SRC:$(SRC_DIR)/%.c=$(BIN_DIR)/%.o)
ASM = $(SRC:$(SRC_DIR)/%.c=$(BIN_DIR)/%.s)

EXEEXT :=
ifeq ($(OS),Windows_NT)
EXEEXT := .exe
endif

EXE = $(BIN_DIR)/nbt_explorer$(EXEEXT)

all: $(EXE)

$(EXE): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# Object file rule
$(BIN_DIR)/%.o: $(SRC_DIR)/%.c | $(BIN_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BIN_DIR):
	mkdir -p $@

# Assembly generation
asm: $(ASM)

$(BIN_DIR)/%.s: $(SRC_DIR)/%.c | $(BIN_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -S $< -o $@

test: $(EXE)
	bash ./tests/run_edit_tests.sh
	bash ./tests/run_format_tests.sh
	bash ./tests/run_region_tests.sh
	bash ./tests/run_modern_region_tests.sh
	bash ./tests/run_cubic_region_tests.sh
	sh ./tests/run_extended_formats_tests.sh
	bash ./tests/run_corruption_tests.sh

fuzz: $(EXE)
	bash ./tests/fuzz_malformed_nbt.sh

clean:
	rm -f $(BIN_DIR)/*.o $(BIN_DIR)/*.s $(EXE)

.PHONY: all clean asm test fuzz
