CC = gcc
CFLAGS = -Wall `pkg-config --cflags libcurl`
SRC = src/oai_cc.c
OBJ = build/lib/oai_cc.o
LIB = build/lib/liboai_cc.a
HEADER = src/oai_cc.h
PREFIX ?= /usr/local

all: $(LIB) build/include/oai_cc.h

build/lib build/include:
	mkdir -p $@

$(OBJ): $(SRC) | build/lib
	$(CC) $(CFLAGS) -c $< -o $@

$(LIB): $(OBJ)
	ar rcs $@ $<
	rm $(OBJ)

build/include/oai_cc.h: $(HEADER) | build/include
	cp $< $@

install: $(LIB) build/include/oai_cc.h
	mkdir -p $(PREFIX)/lib $(PREFIX)/include
	cp $(LIB) $(PREFIX)/lib/
	cp build/include/oai_cc.h $(PREFIX)/include/

uninstall:
	rm -f $(PREFIX)/lib/liboai_cc.a
	rm -f $(PREFIX)/include/oai_cc.h

clean:
	rm -rf build

.PHONY: all clean install uninstall