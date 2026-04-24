# Makefile for PostgreSQL synchronous replication config parser (Lime conversion)
#
# Converted from: src/backend/replication/syncrep_gram.y + syncrep_scanner.l
#
# Parses the synchronous_standby_names GUC parameter.

# Paths
LIME      ?= ../../lime
CC        ?= cc
CFLAGS    ?= -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wno-unused-parameter -g -O2
LDFLAGS   ?=
TEMPLATE  ?= ../../limpar.c

# Source files
GRAMMAR    = syncrep_gram.lime
PARSER_C   = syncrep_gram.c
PARSER_H   = syncrep_gram.h
TOKENIZER  = syncrep_tokenize.c
ACTIONS    = syncrep_actions.c
DEFS_H     = syncrep_defs.h
MAIN       = main.c

# Object files
PARSER_O   = syncrep_gram.o
TOKENIZER_O = syncrep_tokenize.o
ACTIONS_O  = syncrep_actions.o
MAIN_O     = main.o

# Output executable
TARGET     = syncrep_parser

# Test directory
TEST_DIR   = tests

.PHONY: all lime generate compile test clean distclean

all: $(TARGET)

# Build lime if needed
lime: $(LIME)

$(LIME):
	$(MAKE) -C ../.. lime

# Generate parser from Lime grammar
generate: $(PARSER_C) $(PARSER_H)

$(PARSER_C) $(PARSER_H): $(GRAMMAR) $(LIME) $(TEMPLATE)
	$(LIME) -T$(TEMPLATE) $(GRAMMAR)
	@test -f $(PARSER_C) || { echo "ERROR: Lime failed to generate $(PARSER_C)"; exit 1; }

# Compile
$(PARSER_O): $(PARSER_C) $(PARSER_H) $(DEFS_H)
	$(CC) $(CFLAGS) -c -o $@ $(PARSER_C)

$(TOKENIZER_O): $(TOKENIZER) $(PARSER_H) $(DEFS_H)
	$(CC) $(CFLAGS) -c -o $@ $(TOKENIZER)

$(ACTIONS_O): $(ACTIONS) $(DEFS_H)
	$(CC) $(CFLAGS) -c -o $@ $(ACTIONS)

$(MAIN_O): $(MAIN) $(PARSER_H) $(DEFS_H)
	$(CC) $(CFLAGS) -c -o $@ $(MAIN)

# Link
$(TARGET): $(PARSER_O) $(TOKENIZER_O) $(ACTIONS_O) $(MAIN_O)
	$(CC) $(LDFLAGS) -o $@ $^

# Run tests
test: $(TARGET)
	@echo "=== Running syncrep parser tests ==="
	@passed=0; failed=0; total=0; \
	if [ -d "$(TEST_DIR)" ] && [ -n "$$(ls $(TEST_DIR)/*.conf 2>/dev/null)" ]; then \
		for f in $(TEST_DIR)/*.conf; do \
			total=$$((total + 1)); \
			input=$$(cat "$$f"); \
			if ./$(TARGET) -q "$$input" 2>/dev/null; then \
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
		echo "  Create .conf files in $(TEST_DIR)/ to add tests."; \
	fi

clean:
	rm -f $(PARSER_O) $(TOKENIZER_O) $(ACTIONS_O) $(MAIN_O)
	rm -f $(TARGET)

distclean: clean
	rm -f $(PARSER_C) $(PARSER_H)
	rm -f syncrep_gram.out
