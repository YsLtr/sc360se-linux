CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -Wpedantic -std=c11 -D_DEFAULT_SOURCE
PREFIX  ?= /usr/local

OBJ = src/device.o src/protocol.o src/profile.o src/main.o
BIN = sc360se

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

src/%.o: src/%.c src/sc360se.h
	$(CC) $(CFLAGS) -c -o $@ $<

test: tests/selftest
	./tests/selftest

tests/selftest: tests/selftest.c src/protocol.c src/device.c src/profile.c src/sc360se.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/selftest.c src/protocol.c src/device.c src/profile.c

install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)$(PREFIX)/bin/$(BIN)
	install -Dm644 udev/60-aula-sc360se.rules \
		$(DESTDIR)/usr/lib/udev/rules.d/60-aula-sc360se.rules

install-gui: install
	install -Dm755 gui/sc360se-gui $(DESTDIR)$(PREFIX)/bin/sc360se-gui
	install -Dm644 gui/sc360se-gui.desktop \
		$(DESTDIR)$(PREFIX)/share/applications/sc360se-gui.desktop
	install -d $(DESTDIR)$(PREFIX)/share/sc360se/profiles
	install -m644 profiles/*.cfg \
		$(DESTDIR)$(PREFIX)/share/sc360se/profiles/

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(BIN)
	rm -f $(DESTDIR)$(PREFIX)/bin/sc360se-gui
	rm -f $(DESTDIR)$(PREFIX)/share/applications/sc360se-gui.desktop
	rm -rf $(DESTDIR)$(PREFIX)/share/sc360se
	rm -f $(DESTDIR)/usr/lib/udev/rules.d/60-aula-sc360se.rules

clean:
	rm -f $(BIN) $(OBJ) tests/selftest

.PHONY: all test install install-gui uninstall clean
