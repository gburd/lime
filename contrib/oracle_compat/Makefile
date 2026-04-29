# Makefile for the Oracle SQL Compatibility Extension
#
# Builds the extension as both a static library and a shared library
# that can be loaded by the ParserManager plugin system.
#
# Prerequisites:
#   - The Lime parser generator (../../lime) must be built
#   - The extension registry headers must be available in ../../include
#
# Targets:
#   all            - Build grammar, static lib, and shared lib
#   grammar        - Generate parser from oracle_grammar.lime
#   test           - Run test queries
#   clean          - Remove generated files

CC       ?= cc
CFLAGS   ?= -Wall -Wextra -O2 -g
LIME     := ../../lime

INCLUDES := -I. -I../../include -I../../src
LDFLAGS  := -L../../builddir/src
LIBS     := -llime_parser -lpthread

# Source files
GRAMMAR_SRC   := oracle_grammar.lime
GRAMMAR_C     := oracle_grammar.c
GRAMMAR_H     := oracle_grammar.h
GRAMMAR_OUT   := oracle_grammar.out

SRCS          := oracle_semantics.c oracle_compat.c
OBJS          := oracle_semantics.o oracle_compat.o
GRAMMAR_OBJ   := oracle_grammar.o

# Output targets
STATIC_LIB    := liboracle_compat.a
SHARED_LIB    := liboracle_compat.so
TEST_BIN      := test_oracle

# ====================================================================

.PHONY: all grammar test clean

all: grammar $(STATIC_LIB) $(SHARED_LIB)

# Generate parser from Lime grammar
grammar: $(GRAMMAR_C)

$(GRAMMAR_C) $(GRAMMAR_H): $(GRAMMAR_SRC) $(LIME)
	$(LIME) $(GRAMMAR_SRC)

$(GRAMMAR_OBJ): $(GRAMMAR_C) $(GRAMMAR_H) oracle_semantics.h
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(GRAMMAR_C)

# Compile source files
oracle_semantics.o: oracle_semantics.c oracle_semantics.h
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

oracle_compat.o: oracle_compat.c oracle_compat.h oracle_semantics.h
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

# Static library
$(STATIC_LIB): $(OBJS) $(GRAMMAR_OBJ)
	ar rcs $@ $^

# Shared library (for dynamic loading via ParserManager)
$(SHARED_LIB): $(SRCS) $(GRAMMAR_C) oracle_semantics.h oracle_compat.h
	$(CC) $(CFLAGS) $(INCLUDES) -shared -fPIC -o $@ $(SRCS) $(GRAMMAR_C) $(LIBS)

# Test binary
$(TEST_BIN): test_oracle.c $(STATIC_LIB) oracle_compat.h oracle_semantics.h
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ test_oracle.c -L. -loracle_compat $(LDFLAGS) $(LIBS)

test: $(TEST_BIN)
	./$(TEST_BIN)

clean:
	rm -f $(OBJS) $(GRAMMAR_OBJ)
	rm -f $(GRAMMAR_C) $(GRAMMAR_H) $(GRAMMAR_OUT)
	rm -f $(STATIC_LIB) $(SHARED_LIB)
	rm -f $(TEST_BIN)
