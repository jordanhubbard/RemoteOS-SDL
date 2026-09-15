CC ?= cc
PREFIX ?= /usr/local
BIN ?= remoteos-sdl

.DEFAULT_GOAL := all
.SUFFIXES:
.DELETE_ON_ERROR:

CFLAGS ?= -O2 -g -Wall -Wextra -Wno-unused-parameter -std=c11
CFLAGS += -D_DEFAULT_SOURCE -Isrc -Ivendor
CFLAGS += $(shell pkg-config --cflags sdl2 SDL2_ttf SDL2_image)
LDFLAGS ?=
LDLIBS := $(shell pkg-config --libs sdl2 SDL2_ttf SDL2_image)

OBJECTS := build/main.o build/cJSON.o
DEPENDENCIES := $(OBJECTS:.o=.d)

.PHONY: all clean install package test

all: $(BIN)

$(BIN): $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJECTS) $(LDLIBS)

build/main.o: src/main.c src/font.h src/protocol.h vendor/cJSON.h
	mkdir -p build
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

build/cJSON.o: vendor/cJSON.c vendor/cJSON.h
	mkdir -p build
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

-include $(DEPENDENCIES)

test: $(BIN)
	REMOTEOS_SDL_MODE=headless SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
		python3 test/protocol_smoke.py ./$(BIN)

package: test
	./scripts/package.sh

install: $(BIN)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(BIN) $(DESTDIR)$(PREFIX)/bin/remoteos-sdl

clean:
	rm -rf build $(BIN)
