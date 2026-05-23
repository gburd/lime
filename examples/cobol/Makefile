# COBOL example -- builds a Lime-generated COBOL parser.
#
# Run from this directory:
#   make            -- generate the parser, build cobol_parser
#   make test       -- parse every tests/*.cob through it
#   make clean

LIME      ?= ../../bin/lime
LIME_TPL  ?= ../../limpar.c
RUNTIME   ?= ../../lib/liblime_parser.a
LLVM_LIBS ?= $(shell llvm-config --ldflags --libs core executionengine \
                                  mcjit native analysis passes orcjit 2>/dev/null)

CC      ?= cc
CFLAGS  ?= -O2 -g -Wall -Wextra -Wno-unused-parameter -std=c11 \
           -I. -I../../include -I../../src

GENERATED = cobol_grammar.c cobol_grammar.h
SOURCES   = main.c tokenize.c cobol_grammar.c
OBJECTS   = $(SOURCES:.c=.o)
TARGET    = cobol_parser

.PHONY: all test clean help

all: $(TARGET)

$(GENERATED): cobol_grammar.lime $(LIME) $(LIME_TPL)
	$(LIME) -T$(LIME_TPL) $<

# Force main.o and tokenize.o to wait for the generated header.
main.o: main.c cobol_grammar.h
tokenize.o: tokenize.c tokenize.h cobol_grammar.h

# liblime_parser.a is built by the top-level meson or make build.
# Reach back and trigger it if missing.
$(RUNTIME):
	$(MAKE) -C ../.. $(notdir $(RUNTIME))

# `lime` itself is similarly built at the top level.
$(LIME):
	$(MAKE) -C ../.. bin/lime

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJECTS) $(RUNTIME)
	$(CC) $(CFLAGS) -o $@ $(OBJECTS) $(RUNTIME) $(LLVM_LIBS) -lpthread

test: $(TARGET)
	@set -e; for f in tests/*.cob; do \
	  echo "=== $$f ==="; \
	  ./$(TARGET) $$f; \
	done

clean:
	rm -f $(TARGET) $(GENERATED) *.o cobol_grammar.out

help:
	@echo "Targets:"
	@echo "  all (default)  -- generate parser, build cobol_parser"
	@echo "  test           -- parse every tests/*.cob"
	@echo "  clean          -- remove generated files"
