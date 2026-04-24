# Makefile for PostgreSQL isolation test spec parser (Lime conversion)
#
# Converted from: src/test/isolation/specparse.y + specscanner.l

# Paths
LIME      ?= ../../lime
CC        ?= cc
CFLAGS    ?= -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wno-unused-parameter -g -O2
LDFLAGS   ?=
TEMPLATE  ?= ../../limpar.c

# Source files
GRAMMAR    = isolation_gram.lime
PARSER_C   = isolation_gram.c
PARSER_H   = isolation_gram.h
TOKENIZER  = isolation_tokenize.c
ACTIONS    = isolation_actions.c
DEFS_H     = isolation_defs.h
MAIN       = main.c

# Object files
PARSER_O   = isolation_gram.o
TOKENIZER_O = isolation_tokenize.o
ACTIONS_O  = isolation_actions.o
MAIN_O     = main.o

# Output executable
TARGET     = isol_parser

# Test directory
TEST_DIR   = tests

.PHONY: all lime generate compile test clean distclean

all: $(TARGET)

lime: $(LIME)

$(LIME):
	$(MAKE) -C ../.. lime

generate: $(PARSER_C) $(PARSER_H)

$(PARSER_C) $(PARSER_H): $(GRAMMAR) $(LIME) $(TEMPLATE)
	$(LIME) -T$(TEMPLATE) $(GRAMMAR)
	@test -f $(PARSER_C) || { echo "ERROR: Lime failed to generate $(PARSER_C)"; exit 1; }

$(PARSER_O): $(PARSER_C) $(PARSER_H) $(DEFS_H)
	$(CC) $(CFLAGS) -c -o $@ $(PARSER_C)

$(TOKENIZER_O): $(TOKENIZER) $(PARSER_H) $(DEFS_H)
	$(CC) $(CFLAGS) -c -o $@ $(TOKENIZER)

$(ACTIONS_O): $(ACTIONS) $(DEFS_H)
	$(CC) $(CFLAGS) -c -o $@ $(ACTIONS)

$(MAIN_O): $(MAIN) $(PARSER_H) $(DEFS_H)
	$(CC) $(CFLAGS) -c -o $@ $(MAIN)

$(TARGET): $(PARSER_O) $(TOKENIZER_O) $(ACTIONS_O) $(MAIN_O)
	$(CC) $(LDFLAGS) -o $@ $^

test: $(TARGET)
	@echo "=== Running isolation spec parser tests ==="
	@passed=0; failed=0; total=0; \
	if [ -d "$(TEST_DIR)" ] && [ -n "$$(ls $(TEST_DIR)/*.spec 2>/dev/null)" ]; then \
		for f in $(TEST_DIR)/*.spec; do \
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
		echo "  Create .spec files in $(TEST_DIR)/ to add tests."; \
	fi

clean:
	rm -f $(PARSER_O) $(TOKENIZER_O) $(ACTIONS_O) $(MAIN_O)
	rm -f $(TARGET)

distclean: clean
	rm -f $(PARSER_C) $(PARSER_H)
	rm -f isolation_gram.out
