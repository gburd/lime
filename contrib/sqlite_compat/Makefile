# Makefile for the SQLite Compatibility Grammar Extension
#
# Builds:
#   - sqlite_compat.so   : Shared library for dynamic loading
#   - test_sqlite_compat : Test runner for the extension
#
# Usage:
#   make                 - Build shared library
#   make test            - Build and run tests (semantic tests only)
#   make clean           - Remove build artifacts
#
# Prerequisites:
#   - Lime parser generator built in ../../
#   - Extension system headers in ../../include and ../../src

CC      ?= cc
CFLAGS  ?= -Wall -Wextra -std=c11 -g -O2
LDFLAGS ?=

# Source files
SRCS    := sqlite_compat.c sqlite_semantics.c
OBJS    := $(SRCS:.c=.o)

# Targets
LIB     := sqlite_compat.so
TEST    := test_sqlite_compat

.PHONY: all clean test

all: $(LIB)

# Build shared library (needs full extension system headers)
$(LIB): $(SRCS)
	$(CC) $(CFLAGS) -shared -fPIC -I. -o $@ $(SRCS) $(LDFLAGS) -lpthread

# Build object files (for static linking into a larger project)
%.o: %.c
	$(CC) $(CFLAGS) -I. -c -o $@ $<

# Build and run tests (standalone, does not require the extension library)
test: $(TEST)
	./$(TEST)

$(TEST): test_sqlite_compat.c sqlite_semantics.c sqlite_semantics.h
	$(CC) $(CFLAGS) -I. -o $@ test_sqlite_compat.c sqlite_semantics.c

clean:
	rm -f $(LIB) $(TEST) $(OBJS) *.o
