CC = gcc
CFLAGS = -Wall -Wextra -g `pkg-config --cflags gtk+-3.0 vte-2.91`
LDFLAGS = `pkg-config --libs gtk+-3.0 vte-2.91`

all: vyn-terminal

vyn-terminal: vyn-terminal.c
	$(CC) $(CFLAGS) -o vyn-terminal vyn-terminal.c $(LDFLAGS)

install: vyn-terminal
	mkdir -p $(DESTDIR)/usr/local/bin
	cp vyn-terminal $(DESTDIR)/usr/local/bin/
	chmod 755 $(DESTDIR)/usr/local/bin/vyn-terminal
	mkdir -p $(DESTDIR)/usr/share/applications
	echo "[Desktop Entry]\nName=Vyn Terminal\nComment=Simple VTE-based Terminal\nExec=vyn-terminal\nIcon=utilities-terminal\nTerminal=false\nType=Application\nCategories=System;TerminalEmulator;" > $(DESTDIR)/usr/share/applications/vyn-terminal.desktop

clean:
	rm -f vyn-terminal

.PHONY: all install clean
