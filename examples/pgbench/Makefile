# Makefile for pgbench expression parser conversion to Lime
#
# This builds a standalone parser for the pgbench expression language,
# converted from PostgreSQL's exprparse.y (Bison) to Lime format.

# Paths
LIME      ?= ../../lime
CC        ?= cc
CFLAGS    ?= -std=c11 -Wall -Wextra -g -O2
LDFLAGS   ?=
TEMPLATE  ?= ../../limpar.c

# Source files
GRAMMAR    = pgbench_expr.lime
PARSER_C   = pgbench_expr.c
PARSER_H   = pgbench_expr.h
TOKENIZER  = tokenize.c
TOKENIZER_H = tokenize.h

# Output executables
PARSE_BIN  = pgbench_parse
TOKEN_BIN  = pgbench_tokenize

# Test files
TEST_DIR   = tests
TEST_SRC   = $(wildcard $(TEST_DIR)/*.expr)

.PHONY: all lime generate compile test tokenize clean distclean

all: $(PARSE_BIN)

# Build the lime tool if it doesn't exist
lime: $(LIME)

$(LIME):
	$(MAKE) -C ../.. lime

# Generate parser C source from Lime grammar
generate: $(PARSER_C) $(PARSER_H)

$(PARSER_C) $(PARSER_H): $(GRAMMAR) $(LIME) $(TEMPLATE)
	$(LIME) -T$(TEMPLATE) $(GRAMMAR)
	@test -f $(PARSER_C) || { echo "ERROR: Lime failed to generate $(PARSER_C)"; exit 1; }

# Build the full parser (tokenizer + parser)
$(PARSE_BIN): $(TOKENIZER) $(TOKENIZER_H) $(PARSER_C) $(PARSER_H)
	$(CC) $(CFLAGS) -DPGBENCH_PARSE_MAIN -o $@ $(TOKENIZER) $(LDFLAGS)

# Build standalone tokenizer test tool
tokenize: $(TOKEN_BIN)

$(TOKEN_BIN): $(TOKENIZER) $(TOKENIZER_H) $(PARSER_H)
	$(CC) $(CFLAGS) -DPGBENCH_TOKENIZE_MAIN -o $@ $(TOKENIZER) $(LDFLAGS)

# Run tests
test: $(PARSE_BIN)
	@echo "=== Running pgbench expression parser tests ==="
	@passed=0; failed=0; total=0; \
	if [ -d "$(TEST_DIR)" ] && [ -n "$$(ls $(TEST_DIR)/*.expr 2>/dev/null)" ]; then \
		for f in $(TEST_DIR)/*.expr; do \
			total=$$((total + 1)); \
			if ./$(PARSE_BIN) < "$$f" > /dev/null 2>&1; then \
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
		echo "  Create .expr files in $(TEST_DIR)/ to add tests."; \
	fi

clean:
	rm -f $(PARSE_BIN) $(TOKEN_BIN)

distclean: clean
	rm -f $(PARSER_C) $(PARSER_H)
	rm -f pgbench_expr.out
