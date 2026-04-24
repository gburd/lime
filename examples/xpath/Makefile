# Makefile for XPath 1.0 grammar -- Lime parser generator
#
# Builds a standalone XPath parser from the grammar converted to Lime format.

# Paths
LIME      ?= ../../lime
CC        ?= cc
CFLAGS    ?= -std=c11 -Wall -Wextra -g -O2
LDFLAGS   ?=
TEMPLATE  ?= ../../limpar.c

# Source files
GRAMMAR    = xpath.lime
PARSER_C   = xpath.c
PARSER_H   = xpath.h
TOKENIZER  = tokenize.c
HELPERS    = xpath_helpers.c
MAIN       = main.c

# Object files
PARSER_O   = xpath.o
TOKENIZER_O = tokenize.o
HELPERS_O  = xpath_helpers.o
MAIN_O     = main.o

# Output executable
TARGET     = xpath_parser

# Test files
TEST_DIR   = tests
TEST_FILES = $(wildcard $(TEST_DIR)/*.txt)

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
$(PARSER_O): $(PARSER_C) $(PARSER_H) xpath_internal.h
	$(CC) $(CFLAGS) -c -o $@ $(PARSER_C)

$(TOKENIZER_O): $(TOKENIZER) tokenize.h xpath_internal.h $(PARSER_H)
	$(CC) $(CFLAGS) -c -o $@ $(TOKENIZER)

$(HELPERS_O): $(HELPERS) xpath_helpers.h xpath_internal.h
	$(CC) $(CFLAGS) -c -o $@ $(HELPERS)

$(MAIN_O): $(MAIN) xpath_internal.h $(PARSER_H) xpath_helpers.h tokenize.h
	$(CC) $(CFLAGS) -c -o $@ $(MAIN)

# Link the parser executable
$(TARGET): $(PARSER_O) $(TOKENIZER_O) $(HELPERS_O) $(MAIN_O)
	$(CC) $(LDFLAGS) -o $@ $^ -lm

# Run tests
test: $(TARGET)
	@echo "=== Running XPath parser tests ==="
	@passed=0; failed=0; total=0; \
	if [ -d "$(TEST_DIR)" ] && [ -n "$$(ls $(TEST_DIR)/*.txt 2>/dev/null)" ]; then \
		for f in $(TEST_DIR)/*.txt; do \
			while IFS= read -r line || [ -n "$$line" ]; do \
				case "$$line" in \
					''|'#'*) continue ;; \
				esac; \
				total=$$((total + 1)); \
				if ./$(TARGET) "$$line" > /dev/null 2>&1; then \
					echo "  PASS: $$line"; \
					passed=$$((passed + 1)); \
				else \
					echo "  FAIL: $$line"; \
					failed=$$((failed + 1)); \
				fi; \
			done < "$$f"; \
		done; \
		echo "=== Results: $$passed/$$total passed, $$failed failed ==="; \
		if [ $$failed -gt 0 ]; then exit 1; fi; \
	else \
		echo "  No test files found in $(TEST_DIR)/"; \
		echo "  Create .txt files in $(TEST_DIR)/ to add tests."; \
	fi

clean:
	rm -f $(PARSER_O) $(TOKENIZER_O) $(HELPERS_O) $(MAIN_O)
	rm -f $(TARGET)

distclean: clean
	rm -f $(PARSER_C) $(PARSER_H)
	rm -f xpath.out
