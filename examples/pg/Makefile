# Makefile for PostgreSQL grammar conversion to Lime parser
#
# This builds a parser from the PostgreSQL grammar converted to Lime format.

# Paths
LIME      ?= ../../lime
CC        ?= cc
CFLAGS    ?= -std=c11 -Wall -Wextra -g -O2
LDFLAGS   ?=
PYTHON3   ?= python3

# Source files
TOKENS     = tokens.lime
GRAMMAR    = gram.lime
COMBINED   = pg_grammar.lime
TEMPLATE   = ../../limpar.c
PARSER_C   = pg_grammar.c
PARSER_H   = pg_grammar.h
KWLIST_H   = pg_kwlist.h
TOKENIZER  = tokenize.c
TOKENIZER_H = tokenize.h
HELPERS    = pg_gram_helpers.c

# Object files
PARSER_O   = pg_grammar.o
TOKENIZER_O = tokenize.o
HELPERS_O  = pg_gram_helpers.o

# Output executables
TARGET     = pg_parser
TOKENIZE_BIN = pg_tokenize

# Test files
TEST_DIR   = tests
TEST_SRC   = $(wildcard $(TEST_DIR)/*.sql)

.PHONY: all lime generate compile test tokenize clean distclean

all: $(TARGET)

# Build the lime tool if it doesn't exist
lime: $(LIME)

$(LIME):
	$(MAKE) -C ../.. lime

# Combine token definitions and grammar into a single file
$(COMBINED): $(TOKENS) $(GRAMMAR)
	cat $(TOKENS) $(GRAMMAR) > $(COMBINED)

# Generate parser C source from combined Lime grammar
generate: $(PARSER_C) $(PARSER_H)

$(PARSER_C) $(PARSER_H): $(COMBINED) $(LIME) $(TEMPLATE)
	-$(LIME) -T$(TEMPLATE) $(COMBINED)
	@test -f $(PARSER_C) || { echo "ERROR: Lime failed to generate $(PARSER_C)"; exit 1; }

# Generate keyword lookup table from Lime header + grammar
$(KWLIST_H): $(PARSER_H) $(GRAMMAR) gen_kwlist.py
	$(PYTHON3) gen_kwlist.py

# Compile individual object files
$(PARSER_O): $(PARSER_C) $(PARSER_H)
	$(CC) $(CFLAGS) -c -o $@ $(PARSER_C)

$(TOKENIZER_O): $(TOKENIZER) $(TOKENIZER_H) $(PARSER_H) $(KWLIST_H)
	$(CC) $(CFLAGS) -c -o $@ $(TOKENIZER)

$(HELPERS_O): $(HELPERS) pg_gram_helpers.h $(PARSER_H)
	$(CC) $(CFLAGS) -c -o $@ $(HELPERS)

# Link the parser executable
$(TARGET): $(PARSER_O) $(TOKENIZER_O) $(HELPERS_O)
	$(CC) $(LDFLAGS) -o $@ $^

# Build standalone tokenizer test tool
tokenize: $(TOKENIZE_BIN)

$(TOKENIZE_BIN): $(TOKENIZER) $(TOKENIZER_H) $(PARSER_H) $(KWLIST_H)
	$(CC) $(CFLAGS) -DPG_TOKENIZE_MAIN -o $@ $(TOKENIZER)

# Run tests
test: $(TARGET)
	@echo "=== Running PostgreSQL parser tests ==="
	@passed=0; failed=0; total=0; \
	if [ -d "$(TEST_DIR)" ] && [ -n "$$(ls $(TEST_DIR)/*.sql 2>/dev/null)" ]; then \
		for f in $(TEST_DIR)/*.sql; do \
			total=$$((total + 1)); \
			if ./$(TARGET) < "$$f" > /dev/null 2>&1; then \
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
		echo "  Create .sql files in $(TEST_DIR)/ to add tests."; \
	fi

clean:
	rm -f $(PARSER_O) $(TOKENIZER_O) $(HELPERS_O)
	rm -f $(TARGET) $(TOKENIZE_BIN)

distclean: clean
	rm -f $(PARSER_C) $(PARSER_H) $(COMBINED) $(KWLIST_H)
	rm -f pg_grammar.out
