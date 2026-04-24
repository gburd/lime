# Makefile for Datalog/EDN parser (Lime conversion)
#
# Builds a standalone Datalog parser with EDN data type support.

# Paths
LIME      ?= ../../lime
CC        ?= cc
CFLAGS    ?= -std=c11 -Wall -Wextra -Wno-unused-parameter -g -O2
LDFLAGS   ?=
TEMPLATE  ?= ../../limpar.c

# Source files
GRAMMAR    = datalog.lime
PARSER_C   = datalog.c
PARSER_H   = datalog.h
TOKENIZER  = datalog_tokenize.c
HELPERS    = datalog_helpers.c
DEFS_H     = datalog_defs.h
MAIN       = main.c

# Object files
PARSER_O   = datalog.o
TOKENIZER_O = datalog_tokenize.o
HELPERS_O  = datalog_helpers.o
MAIN_O     = main.o

# Output executable
TARGET     = datalog_parser

# Test / sample files
TEST_DIR   = samples

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

$(TOKENIZER_O): $(TOKENIZER) datalog_tokenize.h $(PARSER_H) $(DEFS_H)
	$(CC) $(CFLAGS) -c -o $@ $(TOKENIZER)

$(HELPERS_O): $(HELPERS) datalog_helpers.h $(DEFS_H)
	$(CC) $(CFLAGS) -c -o $@ $(HELPERS)

$(MAIN_O): $(MAIN) $(PARSER_H) $(DEFS_H) datalog_helpers.h datalog_tokenize.h
	$(CC) $(CFLAGS) -c -o $@ $(MAIN)

# Link the parser executable
$(TARGET): $(PARSER_O) $(TOKENIZER_O) $(HELPERS_O) $(MAIN_O)
	$(CC) $(LDFLAGS) -o $@ $^

# Run tests against sample Datalog programs
test: $(TARGET)
	@echo "=== Running Datalog parser tests ==="
	@passed=0; failed=0; total=0; \
	if [ -d "$(TEST_DIR)" ] && [ -n "$$(ls $(TEST_DIR)/*.dl 2>/dev/null)" ]; then \
		for f in $(TEST_DIR)/*.dl; do \
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
		echo "  Create .dl files in $(TEST_DIR)/ to add tests."; \
	fi

clean:
	rm -f $(PARSER_O) $(TOKENIZER_O) $(HELPERS_O) $(MAIN_O)
	rm -f $(TARGET)

distclean: clean
	rm -f $(PARSER_C) $(PARSER_H)
	rm -f datalog.out
