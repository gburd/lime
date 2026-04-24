# Makefile for PostgreSQL BKI bootstrap parser (Lime conversion)
#
# Converted from: src/backend/bootstrap/bootparse.y + bootscanner.l
#
# This builds a standalone parser that validates BKI syntax.

# Paths
LIME      ?= ../../lime
CC        ?= cc
CFLAGS    ?= -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wno-unused-parameter -g -O2
LDFLAGS   ?=
TEMPLATE  ?= ../../limpar.c

# Source files
GRAMMAR    = boot_grammar.lime
PARSER_C   = boot_grammar.c
PARSER_H   = boot_grammar.h
TOKENIZER  = boot_tokenize.c
ACTIONS    = boot_actions.c
DEFS_H     = boot_gram_defs.h
MAIN       = main.c

# Object files
PARSER_O   = boot_grammar.o
TOKENIZER_O = boot_tokenize.o
ACTIONS_O  = boot_actions.o
MAIN_O     = main.o

# Output executable
TARGET     = boot_parser

# Test files
TEST_DIR   = tests

.PHONY: all lime generate compile test clean distclean

all: $(TARGET)

# Build the lime tool if it doesn't exist
lime: $(LIME)

$(LIME):
	$(MAKE) -C ../.. lime

# Generate parser C source from Lime grammar
generate: $(PARSER_C) $(PARSER_H)

$(PARSER_C) $(PARSER_H): $(GRAMMAR) $(LIME) $(TEMPLATE)
	$(LIME) -T$(TEMPLATE) $(GRAMMAR)
	@test -f $(PARSER_C) || { echo "ERROR: Lime failed to generate $(PARSER_C)"; exit 1; }

# Compile individual object files
$(PARSER_O): $(PARSER_C) $(PARSER_H) $(DEFS_H)
	$(CC) $(CFLAGS) -c -o $@ $(PARSER_C)

$(TOKENIZER_O): $(TOKENIZER) $(PARSER_H) $(DEFS_H)
	$(CC) $(CFLAGS) -c -o $@ $(TOKENIZER)

$(ACTIONS_O): $(ACTIONS) $(DEFS_H)
	$(CC) $(CFLAGS) -c -o $@ $(ACTIONS)

$(MAIN_O): $(MAIN) $(PARSER_H) $(DEFS_H)
	$(CC) $(CFLAGS) -c -o $@ $(MAIN)

# Link the parser executable
$(TARGET): $(PARSER_O) $(TOKENIZER_O) $(ACTIONS_O) $(MAIN_O)
	$(CC) $(LDFLAGS) -o $@ $^

# Run tests
test: $(TARGET)
	@echo "=== Running BKI bootstrap parser tests ==="
	@passed=0; failed=0; total=0; \
	if [ -d "$(TEST_DIR)" ] && [ -n "$$(ls $(TEST_DIR)/*.bki 2>/dev/null)" ]; then \
		for f in $(TEST_DIR)/*.bki; do \
			total=$$((total + 1)); \
			if ./$(TARGET) -q "$$f" 2>/dev/null; then \
				echo "  PASS: $$f"; \
				passed=$$((passed + 1)); \
			else \
				echo "  FAIL: $$f"; \
				failed=$$((failed + 1)); \
			fi; \
		done; \
		echo "=== Results: $$passed/$$total passed, $$failed failed ==="; \
		if [ $$failed -gt 0 ]; then exit 1; fi; \
	else \
		echo "  No test files found in $(TEST_DIR)/"; \
		echo "  Create .bki files in $(TEST_DIR)/ to add tests."; \
	fi

clean:
	rm -f $(PARSER_O) $(TOKENIZER_O) $(ACTIONS_O) $(MAIN_O)
	rm -f $(TARGET)

distclean: clean
	rm -f $(PARSER_C) $(PARSER_H)
	rm -f boot_grammar.out
