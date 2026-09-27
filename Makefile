CC = gcc
CFLAGS = -std=c99 -O2 -Wall -Wextra -Wformat=2 -Wfatal-errors -Wundef -Wno-unused-result -fno-stack-protector -march=native -flto -Iincludes

.PHONY: all debug test clean

all: prawn bait zobrist-gen perft

prawn: prawn-src/main.c shared-src/*.c includes/*.h
	$(CC) $(CFLAGS) prawn-src/main.c shared-src/*.c -o prawn

bait: bait-src/main.c shared-src/*.c includes/*.h
	$(CC) $(CFLAGS) bait-src/main.c shared-src/*.c  -o bait

zobrist-gen: zobrist-src/main.c shared-src/*.c includes/*.h
	$(CC) $(CFLAGS) zobrist-src/main.c shared-src/*.c  -o zobrist-gen

perft: perft-src/main.c shared-src/*.c includes/*.h
	$(CC) $(CFLAGS) perft-src/main.c shared-src/*.c -o perft

test: perft
	./perft

debug: prawn-src/main.c shared-src/*.c includes/*.h
	$(CC) -g -O0 $(CFLAGS) prawn-src/main.c shared-src/*.c -o prawn-debug

clean:
	rm -rf prawn prawn-debug bait perft zobrist-gen *.log prawn-*.dSYM
