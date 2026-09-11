CC = gcc
CFLAGS = -std=c99 -O2 -Wall -Wextra -Wformat=2 -Wfatal-errors -Wundef -Wno-unused-result -fno-stack-protector -march=native

all: prawn bait

prawn: *.c
	$(CC) $(CFLAGS) *.c -o prawn

bait: bait-src/main.c
	$(CC) $(CFLAGS) bait-src/main.c -o bait

zobrist-gen: zobrist-src/main.c common.h
	$(CC) $(CFLAGS) zobrist-src/main.c -o zobrist-gen

debug: *.c
	$(CC) -g -O0 $(CFLAGS) *.c -o prawn-debug

clean:
	rm -rf prawn prawn-debug bait zobrist-gen *.log prawn-*.dSYM
