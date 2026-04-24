# Makefile for PostgreSQL replication protocol parser (Lime conversion)
#
# Converted from: src/backend/replication/repl_gram.y + repl_scanner.l
#
# This builds a standalone parser that validates replication protocol commands.

# Paths
LIME      ?= ../../lime
CC        ?= cc
CFLAGS    ?= -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wno-unused-parameter -g -O2
LDFLAGS   ?=
TEMPLATE  ?= ../../limpar.c

# Source files
GRAMMAR    = repl_gram.lime
PARSER_C   = repl_gram.c
PARSER_H   = repl_gram.h
TOKENIZER  = repl_tokenize.c
ACTIONS    = repl_actions.c
DEFS_H     = repl_defs.h
MAIN       = main.c

# Object files
PARSER_O   = repl_gram.o
TOKENIZER_O = repl_tokenize.o
ACTIONS_O  = repl_actions.o
MAIN_O     = main.o

# Output executable
TARGET     = repl_parser

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
	@echo "=== Running replication protocol parser tests ==="
	@passed=0; failed=0; total=0; \
	if [ -d "$(TEST_DIR)" ] && [ -n "$$(ls $(TEST_DIR)/*.repl 2>/dev/null)" ]; then \
		for f in $(TEST_DIR)/*.repl; do \
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
		echo "  Create .repl files in $(TEST_DIR)/ to add tests."; \
	fi

clean:
	rm -f $(PARSER_O) $(TOKENIZER_O) $(ACTIONS_O) $(MAIN_O)
	rm -f $(TARGET)

distclean: clean
	rm -f $(PARSER_C) $(PARSER_H)
	rm -f repl_gram.out
