CC=gcc
CFLAGS=-std=c11 -Wall -Wextra -pedantic -pthread -Ithird_party/miniaudio -Iaudio -Isrc
SRC=$(wildcard src/*.c) $(wildcard audio/*.c)
OBJ=$(SRC:.c=.o)

UNAME_S := $(shell uname -s)
LIBS = -lm
ifeq ($(UNAME_S),Linux)
LIBS += -ldl -lcurl
else ifeq ($(UNAME_S),Darwin)
LIBS += -framework AudioToolbox -framework AudioUnit -framework CoreAudio -lcurl
endif

musika: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LIBS)

clean:
	rm -f $(OBJ) musika

.PHONY: clean
