CC = gcc
CFLAGS = -std=c99 -O2 -Wall -Wextra -Wformat=2 -Wfatal-errors -Wundef -Wno-unused-result -fno-stack-protector -march=native

all: prawn two-uci

prawn: fen.c prawn.c
	$(CC) $(CFLAGS) fen.c prawn.c -o prawn

two-uci: two-uci-src/main.c
	$(CC) $(CFLAGS) two-uci-src/main.c -o two-uci

debug: fen.c prawn.c
	$(CC) -g -O0 $(CFLAGS) fen.c prawn.c -o prawn-debug

clean:
	rm -f prawn prawn-debug two-uci *.log
