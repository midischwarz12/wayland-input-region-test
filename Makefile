CC ?= cc
PKG_CONFIG ?= pkg-config
WAYLAND_SCANNER ?= wayland-scanner
PREFIX ?= /usr/local
CFLAGS ?= -O2 -g
CPPFLAGS += -Ibuild $(shell $(PKG_CONFIG) --cflags wayland-client)
LDLIBS += $(shell $(PKG_CONFIG) --libs wayland-client)
PROTOCOL = $(shell $(PKG_CONFIG) --variable=pkgdatadir wayland-protocols)/stable/xdg-shell/xdg-shell.xml

.PHONY: all check install clean
all: build/wayland-input-region-test

build:
	mkdir -p $@

build/xdg-shell-client-protocol.h: $(PROTOCOL) | build
	$(WAYLAND_SCANNER) client-header $< $@

build/xdg-shell-protocol.c: $(PROTOCOL) | build
	$(WAYLAND_SCANNER) private-code $< $@

build/wayland-input-region-test: main.c build/xdg-shell-client-protocol.h build/xdg-shell-protocol.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c11 -Wall -Wextra -Werror main.c build/xdg-shell-protocol.c $(LDFLAGS) $(LDLIBS) -o $@

build/unit-tests: tests/unit.c main.c build/xdg-shell-client-protocol.h build/xdg-shell-protocol.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c11 -Wall -Wextra -Werror tests/unit.c build/xdg-shell-protocol.c $(LDFLAGS) $(LDLIBS) -o $@

check: all build/unit-tests
	./build/unit-tests
	./build/wayland-input-region-test --help
	! ./build/wayland-input-region-test --invalid-option
	! ./build/wayland-input-region-test --empty-input --full-input

install: all
	install -Dm755 build/wayland-input-region-test $(DESTDIR)$(PREFIX)/bin/wayland-input-region-test
	install -Dm644 LICENSE $(DESTDIR)$(PREFIX)/share/licenses/wayland-input-region-test/LICENSE

clean:
	rm -rf build
