all: prawn two-uci

prawn:
	gcc fen.c prawn.c -o prawn -std=c99 -O2 -Wall -Wextra -Wformat=2 -Wfatal-errors -Wundef -Wno-unused-result -fno-stack-protector -march=native

two-uci:
	gcc two-uci-src/main.c -o two-uci -std=c99 -O2 -Wall -Wextra -Wformat=2 -Wfatal-errors -Wundef -Wno-unused-result -fno-stack-protector -march=native

debug:
	gcc -g fen.c prawn.c -o prawn-debug -std=c99 -O0 -Wall -Wextra -Wformat=2 -Wfatal-errors -Wundef -Wno-unused-result -fno-stack-protector -march=native

clean:
	rm -f prawn *.log prawn-debug two-uci
