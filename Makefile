CC = gcc
CFLAGS = -std=c99 -O2 -Wall -Wextra -Wformat=2 -Wfatal-errors -Wundef -Wno-unused-result -fno-stack-protector -march=native

.PHONY: all debug test clean

all: prawn bait perft

prawn: *.c common.h
	$(CC) $(CFLAGS) *.c -o prawn

bait: bait-src/main.c
	$(CC) $(CFLAGS) bait-src/main.c -o bait

zobrist-gen: zobrist-src/main.c common.h
	$(CC) $(CFLAGS) zobrist-src/main.c -o zobrist-gen

perft: perft-src/main.c prawn.c fen.c common.h
	$(CC) $(CFLAGS) perft-src/main.c fen.c -o perft

test: perft
	./perft

debug: *.c common.h
	$(CC) -g -O0 $(CFLAGS) *.c -o prawn-debug

clean:
	rm -rf prawn prawn-debug bait perft zobrist-gen *.log prawn-*.dSYM
