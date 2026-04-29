# Makefile for the MySQL SQL Compatibility Extension
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
#   grammar        - Generate parser from mysql_grammar.lime
#   test           - Run unit tests
#   clean          - Remove generated files

CC       ?= cc
CFLAGS   ?= -Wall -Wextra -O2 -g
LIME     := ../../lime

INCLUDES := -I. -I../../include -I../../src
LDFLAGS  := -L../../builddir/src
LIBS     := -llime_parser -lpthread

# Source files
GRAMMAR_SRC   := mysql_grammar.lime
GRAMMAR_C     := mysql_grammar.c
GRAMMAR_H     := mysql_grammar.h
GRAMMAR_OUT   := mysql_grammar.out

SRCS          := mysql_semantics.c mysql_compat.c
OBJS          := mysql_semantics.o mysql_compat.o
GRAMMAR_OBJ   := mysql_grammar.o

# Output targets
STATIC_LIB    := libmysql_compat.a
SHARED_LIB    := libmysql_compat.so
TEST_BIN      := test_mysql

# ====================================================================

.PHONY: all grammar test clean

all: grammar $(STATIC_LIB) $(SHARED_LIB)

# Generate parser from Lime grammar
grammar: $(GRAMMAR_C)

$(GRAMMAR_C) $(GRAMMAR_H): $(GRAMMAR_SRC) $(LIME)
	$(LIME) $(GRAMMAR_SRC)

$(GRAMMAR_OBJ): $(GRAMMAR_C) $(GRAMMAR_H) mysql_semantics.h
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $(GRAMMAR_C)

# Compile source files
mysql_semantics.o: mysql_semantics.c mysql_semantics.h
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

mysql_compat.o: mysql_compat.c mysql_semantics.h
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

# Static library
$(STATIC_LIB): $(OBJS) $(GRAMMAR_OBJ)
	ar rcs $@ $^

# Shared library (for dynamic loading via ParserManager)
$(SHARED_LIB): $(SRCS) $(GRAMMAR_C) mysql_semantics.h
	$(CC) $(CFLAGS) $(INCLUDES) -shared -fPIC -o $@ $(SRCS) $(GRAMMAR_C) $(LIBS)

# Test binary
$(TEST_BIN): test_mysql_compat.c $(STATIC_LIB) mysql_semantics.h
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ test_mysql_compat.c -L. -lmysql_compat $(LDFLAGS) $(LIBS)

test: $(TEST_BIN)
	./$(TEST_BIN)

clean:
	rm -f $(OBJS) $(GRAMMAR_OBJ)
	rm -f $(GRAMMAR_C) $(GRAMMAR_H) $(GRAMMAR_OUT)
	rm -f $(STATIC_LIB) $(SHARED_LIB)
	rm -f $(TEST_BIN)
