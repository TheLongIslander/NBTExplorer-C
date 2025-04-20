CC = gcc
CFLAGS = -Wall -Wextra -g -Ih
LDFLAGS = -lz

SRC_DIR = src
BIN_DIR = bin
INC_DIR = h

SRC = $(wildcard $(SRC_DIR)/*.c)
OBJ = $(SRC:$(SRC_DIR)/%.c=$(BIN_DIR)/%.o)

EXE = $(BIN_DIR)/nbt_explorer

all: $(EXE)

$(EXE): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Object file rule (bin/main.o from src/main.c)
$(BIN_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Generate assembly (optional)
asm:
	@for file in $(SRC); do \
		$(CC) $(CFLAGS) -S $$file -o $${file%.c}.s; \
	done

clean:
	rm -f $(BIN_DIR)/*.o $(SRC_DIR)/*.s $(EXE)

.PHONY: all clean asm
