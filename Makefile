CC=gcc
CFLAGS=-Wall -Wextra -g
LDFLAGS=-lz

all: nbt_explorer

nbt_explorer: main.o nbt.o io.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f *.o nbt_explorer
