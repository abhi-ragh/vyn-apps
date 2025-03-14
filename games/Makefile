.PHONY: all install clean

all: install

install: install-robodump install-tictacwar

install-robodump:
	@echo "Installing RoboDump..."
	# Create the launcher script for RoboDump
	mkdir -p $(DESTDIR)/usr/local/bin
	echo '#!/bin/sh' > robodump.sh
	echo 'firefox --kiosk file:///usr/local/share/games/RoboDump/index.html' >> robodump.sh
	chmod 755 robodump.sh
	cp robodump.sh $(DESTDIR)/usr/local/bin/robodump
	rm -f robodump.sh
	# Copy game files (including the icon) to /usr/local/share/games
	mkdir -p $(DESTDIR)/usr/local/share/games/RoboDump
	cp -r RoboDump/* $(DESTDIR)/usr/local/share/games/RoboDump/
	# Create the desktop entry (icon uses full path)
	mkdir -p $(DESTDIR)/usr/share/applications
	echo -e "[Desktop Entry]\nName=RoboDump\nComment=Play RoboDump\nExec=/usr/local/bin/robodump\nIcon=/usr/local/share/games/RoboDump/icon.png\nTerminal=false\nType=Application\nCategories=Game;" > $(DESTDIR)/usr/share/applications/robodump.desktop
	@echo "RoboDump installed."

install-tictacwar:
	@echo "Installing TicTacWar..."
	# Create the launcher script for TicTacWar
	mkdir -p $(DESTDIR)/usr/local/bin
	echo '#!/bin/sh' > tictacwar.sh
	echo 'firefox --kiosk file:///usr/local/share/games/TicTacWar/index.html' >> tictacwar.sh
	chmod 755 tictacwar.sh
	cp tictacwar.sh $(DESTDIR)/usr/local/bin/tictacwar
	rm -f tictacwar.sh
	# Copy game files (including the icon) to /usr/local/share/games
	mkdir -p $(DESTDIR)/usr/local/share/games/TicTacWar
	cp -r TicTacWar/* $(DESTDIR)/usr/local/share/games/TicTacWar/
	# Create the desktop entry (icon uses full path)
	mkdir -p $(DESTDIR)/usr/share/applications
	echo -e "[Desktop Entry]\nName=TicTacWar\nComment=Play TicTacWar\nExec=/usr/local/bin/tictacwar\nIcon=/usr/local/share/games/TicTacWar/icon.png\nTerminal=false\nType=Application\nCategories=Game;" > $(DESTDIR)/usr/share/applications/tictacwar.desktop
	@echo "TicTacWar installed."

clean:
	@echo "Cleaning launcher scripts (does not remove installed files)..."
	rm -f $(DESTDIR)/usr/local/bin/robodump
	rm -f $(DESTDIR)/usr/local/bin/tictacwar

