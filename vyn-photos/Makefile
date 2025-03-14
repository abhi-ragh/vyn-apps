CC = gcc
CFLAGS = -Wall -Wextra -g `pkg-config --cflags gtk+-3.0 gdk-pixbuf-2.0`
LDFLAGS = `pkg-config --libs gtk+-3.0 gdk-pixbuf-2.0`

all: vyn-photos

vyn-photos: vyn-photos.c
	$(CC) $(CFLAGS) -o vyn-photos vyn-photos.c $(LDFLAGS)

install: vyn-photos
	mkdir -p $(DESTDIR)/usr/local/bin
	cp vyn-photos $(DESTDIR)/usr/local/bin/
	chmod 755 $(DESTDIR)/usr/local/bin/vyn-photos
	mkdir -p $(DESTDIR)/usr/share/applications
	echo "[Desktop Entry]\nName=Vyn Photos\nComment=Simple Image Viewer\nExec=vyn-photos\nIcon=multimedia-photo-viewer\nTerminal=false\nType=Application\nCategories=Graphics;Viewer;" > $(DESTDIR)/usr/share/applications/vyn-photos.desktop

clean:
	rm -f vyn-photos

.PHONY: all install clean

