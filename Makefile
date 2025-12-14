CC=gcc
CFLAGS=-std=c11 -Wall -Wextra -pedantic
SRC=$(wildcard src/*.c)
OBJ=$(SRC:.c=.o)

musika: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ)

clean:
	rm -f $(OBJ) musika

.PHONY: clean
