# Makefile for Lime parser management tools
#
# Build:   make              (with stubs for unimplemented library functions)
#          make USE_STUBS=0  (link against real library - requires all deps)
# Install: make install PREFIX=/usr/local
# Clean:   make clean

CC       ?= cc
CFLAGS   ?= -std=c11 -D_XOPEN_SOURCE=500 -D_POSIX_C_SOURCE=200809L \
            -Wall -Wextra -g -O2
LDFLAGS  ?= -ldl -lpthread
PREFIX   ?= /usr/local

INCDIR   = ../include
SRCDIR   = ../src

# Use stubs by default (set USE_STUBS=0 when the full library is ready)
USE_STUBS ?= 1

# Source files for parse-manager
PM_SRCS  = parse-manager.c
PM_DEPS  = $(SRCDIR)/parser_manager.c $(SRCDIR)/parser_plugin.c \
           $(SRCDIR)/snapshot.c $(SRCDIR)/version.c

ifeq ($(USE_STUBS),1)
PM_DEPS += stubs.c
endif

PM_BIN   = parse-manager

.PHONY: all clean install uninstall test

all: $(PM_BIN)

$(PM_BIN): $(PM_SRCS) $(PM_DEPS) $(INCDIR)/parser_manager.h $(INCDIR)/parser.h
	$(CC) $(CFLAGS) -I$(INCDIR) -I$(SRCDIR) \
	    -o $@ $(PM_SRCS) $(PM_DEPS) $(LDFLAGS)

# Basic smoke tests for the CLI tool
test: $(PM_BIN)
	@echo "=== parse-manager CLI smoke tests ==="
	@echo "--- version ---"
	./$(PM_BIN) version
	@echo "--- help ---"
	./$(PM_BIN) help > /dev/null
	@echo "--- help list ---"
	./$(PM_BIN) help list > /dev/null
	@echo "--- list (empty) ---"
	./$(PM_BIN) list
	@echo "--- All smoke tests passed ---"

clean:
	rm -f $(PM_BIN)

install: $(PM_BIN)
	install -d $(PREFIX)/bin
	install -m 755 $(PM_BIN) $(PREFIX)/bin/

uninstall:
	rm -f $(PREFIX)/bin/$(PM_BIN)
