# Makefile for the XML Query extension (XQuery/XPath embedded in SQL)
#
# Targets:
#   all      - Build the demo and compile grammars
#   grammars - Generate parsers from .lime grammar files
#   demo     - Build the standalone demo program
#   test     - Run the demo (basic smoke test)
#   clean    - Remove generated files
#
# Prerequisites:
#   - lime binary built at ../../lime
#   - gcc or compatible C compiler

CC       ?= gcc
CFLAGS   ?= -std=c11 -Wall -Wextra -D_GNU_SOURCE -DLIME_NO_JIT
LIME     ?= ../../lime
INCFLAGS  = -I ../../include -I ../../src
LDFLAGS   = -lpthread

# Extension sources
DEMO_SRC  = xml_query.c xml_semantics.c

# Library sources needed for standalone demo
LIB_SRC   = ../../src/extension_registry.c \
            ../../src/grammar_context.c \
            ../../src/context_switch.c \
            ../../src/snapshot.c \
            ../../src/token_table.c \
            ../../src/merkle_tree.c \
            ../../src/jit_context.c \
            ../../src/jit_codegen.c \
            ../../src/jit_policy.c

.PHONY: all grammars demo test clean

all: grammars demo

# Generate parsers from .lime files
grammars: xquery_grammar.c xpath_grammar.c

xquery_grammar.c: xquery_grammar.lime
	$(LIME) -T../../limpar.c xquery_grammar.lime

xpath_grammar.c: xpath_grammar.lime
	$(LIME) -T../../limpar.c xpath_grammar.lime

# Build the standalone demo
demo: xml_query_demo

xml_query_demo: $(DEMO_SRC) $(LIB_SRC)
	$(CC) $(CFLAGS) $(INCFLAGS) -DXML_QUERY_STANDALONE \
		-o $@ $(DEMO_SRC) $(LIB_SRC) $(LDFLAGS)

# Run the demo as a basic smoke test
test: xml_query_demo
	./xml_query_demo

clean:
	rm -f xquery_grammar.c xquery_grammar.h xquery_grammar.out
	rm -f xpath_grammar.c xpath_grammar.h xpath_grammar.out
	rm -f xml_query_demo
	rm -f *.o
