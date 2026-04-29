# Makefile for the EDN Literals Extension
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
#   grammar        - Generate parser from edn_grammar.lime
#   test           - Run test queries
#   clean          - Remove generated files

CC       ?= cc
CFLAGS   ?= -Wall -Wextra -O2 -g
LIME     := ../../lime

INCLUDES := -I. -I../../include -I../../src
LDFLAGS  := -L../../builddir/src
LIBS     := -llime_parser -lpthread

# Source files
GRAMMAR_SRC   := edn_grammar.lime
GRAMMAR_C     := edn_grammar.c
GRAMMAR_H     := edn_grammar.h
GRAMMAR_OUT   := edn_grammar.out

SRCS          := edn_semantics.c edn_literals.c
OBJS          := edn_semantics.o edn_literals.o
GRAMMAR_OBJ   := edn_grammar.o

# Output targets
STATIC_LIB    := libedn_literals.a
SHARED_LIB    := libedn_literals.so
TEST_BIN      := test_edn

# ====================================================================

.PHONY: all grammar test clean

all: grammar $(STATIC_LIB) $(SHARED_LIB)

# Generate parser from Lime grammar
grammar: $(GRAMMAR_C)

$(GRAMMAR_C) $(GRAMMAR_H): $(GRAMMAR_SRC) $(LIME)
	$(LIME) $(GRAMMAR_SRC)

$(GRAMMAR_OBJ): $(GRAMMAR_C) $(GRAMMAR_H) edn_semantics.h
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(GRAMMAR_C)

# Compile source files
edn_semantics.o: edn_semantics.c edn_semantics.h
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

edn_literals.o: edn_literals.c edn_semantics.h
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

# Static library
$(STATIC_LIB): $(OBJS) $(GRAMMAR_OBJ)
	ar rcs $@ $^

# Shared library (for dynamic loading via ParserManager)
$(SHARED_LIB): $(SRCS) $(GRAMMAR_C) edn_semantics.h
	$(CC) $(CFLAGS) $(INCLUDES) -shared -fPIC -o $@ $(SRCS) $(GRAMMAR_C) $(LIBS)

# Test binary
$(TEST_BIN): test_edn.c $(STATIC_LIB) edn_semantics.h
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ test_edn.c -L. -ledn_literals $(LDFLAGS) $(LIBS)

test: $(TEST_BIN)
	./$(TEST_BIN)

clean:
	rm -f $(OBJS) $(GRAMMAR_OBJ)
	rm -f $(GRAMMAR_C) $(GRAMMAR_H) $(GRAMMAR_OUT)
	rm -f $(STATIC_LIB) $(SHARED_LIB)
	rm -f $(TEST_BIN)
