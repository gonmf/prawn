all:
	gcc fen.c prawn.c -o prawn -std=c99 -O2 -Wall -Wextra -Wformat=2 -Wfatal-errors -Wundef -Wno-unused-result -fno-stack-protector -march=native

debug:
	gcc -g fen.c prawn.c -o prawn-debug -std=c99 -O0 -Wall -Wextra -Wformat=2 -Wfatal-errors -Wundef -Wno-unused-result -fno-stack-protector -march=native
