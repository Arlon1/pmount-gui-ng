PREFIX = /usr/local
BINPREFIX = "$(PREFIX)/bin"

# a c compiler
CC = gcc
FLAGS=`pkg-config gtk+-3.0 --cflags`
LIBS=`pkg-config gtk+-3.0 --libs`

pmount-gui-ng: main.o
	$(CC) main.o -o pmount-gui-ng $(LIBS)

%.o: %.c
	$(CC) -c main.c -o main.o $(FLAGS)

PHONY +: clean
clean:
	rm -f main.o
	rm -f pmount-gui-ng

install:
	mkdir -p $(BINPREFIX)
	install -m 0755 $(LIB) $(BINPREFIX)


.PHONY: $(PHONY)
