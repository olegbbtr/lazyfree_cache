all: run


main: main.c
	gcc -g  -o main main.c

run: main
	./main
