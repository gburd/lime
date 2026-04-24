# Makefile for MongoDB query language parser using Lime
#
# Builds a standalone MongoDB query parser from the grammar.

# Paths
LIME      ?= ../../lime
CC        ?= cc
CFLAGS    ?= -std=c11 -Wall -Wextra -g -O2
LDFLAGS   ?=
TEMPLATE  ?= ../../limpar.c

# Source files
GRAMMAR    = mongodb.lime
PARSER_C   = mongodb.c
PARSER_H   = mongodb.h
TOKENIZER  = tokenize.c
HELPERS    = mongodb_helpers.c
MAIN       = main.c

# Object files
PARSER_O   = mongodb.o
TOKENIZER_O = tokenize.o
HELPERS_O  = mongodb_helpers.o
MAIN_O     = main.o

# Output executable
TARGET     = mongodb_parser

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
$(PARSER_O): $(PARSER_C) $(PARSER_H) mongodb_internal.h
	$(CC) $(CFLAGS) -c -o $@ $(PARSER_C)

$(TOKENIZER_O): $(TOKENIZER) tokenize.h mongodb_internal.h $(PARSER_H)
	$(CC) $(CFLAGS) -c -o $@ $(TOKENIZER)

$(HELPERS_O): $(HELPERS) mongodb_helpers.h mongodb_internal.h
	$(CC) $(CFLAGS) -c -o $@ $(HELPERS)

$(MAIN_O): $(MAIN) mongodb_internal.h $(PARSER_H) tokenize.h
	$(CC) $(CFLAGS) -c -o $@ $(MAIN)

# Link the parser executable
$(TARGET): $(PARSER_O) $(TOKENIZER_O) $(HELPERS_O) $(MAIN_O)
	$(CC) $(LDFLAGS) -o $@ $^

# Run tests
test: $(TARGET)
	@echo "=== Running MongoDB parser tests ==="
	@passed=0; failed=0; total=0; \
	if [ -d "$(TEST_DIR)" ] && [ -n "$$(ls $(TEST_DIR)/*.txt 2>/dev/null)" ]; then \
		for f in $(TEST_DIR)/*.txt; do \
			echo "  File: $$f"; \
			while IFS= read -r line || [ -n "$$line" ]; do \
				case "$$line" in \
					''|'#'*) continue ;; \
				esac; \
				total=$$((total + 1)); \
				if ./$(TARGET) "$$line" > /dev/null 2>&1; then \
					echo "    PASS: $$line"; \
					passed=$$((passed + 1)); \
				else \
					echo "    FAIL: $$line"; \
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
	rm -f mongodb.out
