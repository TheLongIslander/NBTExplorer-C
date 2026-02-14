CC = gcc
CFLAGS = -Wall -Wextra -g -Ih
LDFLAGS = -lz

SRC_DIR = src
BIN_DIR = bin
INC_DIR = h

SRC = $(wildcard $(SRC_DIR)/*.c)
OBJ = $(SRC:$(SRC_DIR)/%.c=$(BIN_DIR)/%.o)
ASM = $(SRC:$(SRC_DIR)/%.c=$(BIN_DIR)/%.s)

EXE = $(BIN_DIR)/nbt_explorer

all: $(EXE)

$(EXE): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Object file rule
$(BIN_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Assembly generation
asm: $(ASM)

$(BIN_DIR)/%.s: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -S $< -o $@

test: $(EXE)
	bash ./tests/run_edit_tests.sh
	bash ./tests/run_corruption_tests.sh

fuzz: $(EXE)
	bash ./tests/fuzz_malformed_nbt.sh

clean:
	rm -f $(BIN_DIR)/*.o $(BIN_DIR)/*.s $(EXE)

.PHONY: all clean asm test fuzz
