PREFIX = /usr/local
BINPREFIX = "$(PREFIX)/bin"

# a c compiler
CC = gcc

# a list of packages (pkg-config)
LIBS = gtk+-3.0
CFLAGS=`pkg-config $(LIBS) --cflags`
LDFLAGS=`pkg-config $(LIBS) --libs --cflags`

pmount-gui-ng: main.o
	$(CC) main.o -o pmount-gui-ng $(LDFLAGS)

%.o: %.c
	$(CC) -c main.c -o main.o $(CFLAGS)

PHONY +: clean
clean:
	rm -f main.o
	rm -f pmount-gui-ng

install:
	mkdir -p $(BINPREFIX)
	install -m 0755 pmount-gui-ng $(BINPREFIX)


.PHONY: $(PHONY)
