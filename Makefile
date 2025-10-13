CC = gcc
CFLAGS = -std=c99 -O2 -Wall -Wextra -Wformat=2 -Wfatal-errors -Wundef -Wno-unused-result -fno-stack-protector -march=native

all: prawn two-uci

prawn: *.c
	$(CC) $(CFLAGS) *.c -o prawn

two-uci: two-uci-src/main.c
	$(CC) $(CFLAGS) two-uci-src/main.c -o two-uci

debug: *.c
	$(CC) -g -O0 $(CFLAGS) *.c -o prawn-debug

clean:
	rm -rf prawn prawn-debug two-uci *.log prawn-debug.dSYM
