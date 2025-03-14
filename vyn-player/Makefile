CC = gcc
CFLAGS = -Wall -Wextra -g `pkg-config --cflags gtk+-3.0 gstreamer-1.0 gstreamer-video-1.0 gdk-pixbuf-2.0`
LDFLAGS = `pkg-config --libs gtk+-3.0 gstreamer-1.0 gstreamer-video-1.0 gdk-pixbuf-2.0`

all: vynplayer

vynplayer: vynplayer.c
	$(CC) $(CFLAGS) -o vynplayer vynplayer.c $(LDFLAGS)

install: vynplayer
	mkdir -p $(DESTDIR)/usr/local/bin
	cp vynplayer $(DESTDIR)/usr/local/bin/
	chmod 755 $(DESTDIR)/usr/local/bin/vynplayer
	mkdir -p $(DESTDIR)/usr/share/applications
	echo "[Desktop Entry]\nName=Vyn Player\nComment=Custom Video Player\nExec=vynplayer\nIcon=multimedia-video-player\nTerminal=false\nType=Application\nCategories=AudioVideo;Player;" > $(DESTDIR)/usr/share/applications/vynplayer.desktop

clean:
	rm -f vynplayer

.PHONY: all install clean
