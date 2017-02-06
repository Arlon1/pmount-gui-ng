# a path your package manager does not use
PREFIX = /usr/local
# a subfolder of PREFIX where binaries are saved
BINPREFIX = "$(PREFIX)/bin"
# a path where your .desktop files are saved
STARTER_PATH = /usr/share/applications

NAME = pmount-gui-ng
# a c compiler
CC = gcc

# a list of packages (pkg-config)
LIBS = gtk+-3.0
CFLAGS=`pkg-config $(LIBS) --cflags`
LDFLAGS=`pkg-config $(LIBS) --libs --cflags`

$(NAME): main.o
	$(CC) main.o -o $(NAME) $(LDFLAGS)

%.o: %.c
	$(CC) -c $< -o $@ $(CFLAGS)

PHONY += clean
clean:
	rm -f *.o
	rm -f $(NAME)

PHONY += install
# TODO check for root privileges
# install into ~/.local/bin/ and ~/.local/share/applications/
install: $(NAME)
	mkdir -p $(BINPREFIX)
	install -m 0755 $(NAME) $(BINPREFIX)
	install -m 0644 $(NAME).desktop $(STARTER_PATH)
	sed -i 's/\/usr\/local\/bin/$(subst /,\/,$(BINPREFIX))/' $(STARTER_PATH)/$(NAME).desktop

PHONY += uninstall
uninstall:
	rm -f $(BINPREFIX)/$(NAME)
	rm -f $(STARTER_PATH)/$(NAME).desktop
.PHONY: $(PHONY)
