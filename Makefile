# Lime parser generator -- hand-written GNU Makefile alternative.
#
# This file is the meson-free build path.  It builds:
#
#   bin/lime              -- parser generator (single-file build by
#                            default; pass LEX=1 to bundle the .lex
#                            compiler into the same binary)
#   lib/liblime_parser.a  -- runtime library (snapshot, parse_engine,
#                            extension framework, conflict detection,
#                            disambiguation, JIT stubs)
#   build/test/*          -- the runtime-parse, destructor and
#                            claims-proof tests
#
# Targets
#   make           -- build lime + the library + tests
#   make test      -- run the tests
#   make clean     -- remove build artifacts
#   make format    -- run clang-format across formatted files
#   make install   -- install lime, liblime_parser.a, and the headers
#                     under $(PREFIX) (default /usr/local)
#
# Configuration knobs
#   CC           -- C compiler, default cc
#   CFLAGS       -- extra C flags appended to the default set
#   LDFLAGS      -- extra link flags
#   LEX=1        -- include the .lex compiler in the lime binary
#   JIT=0        -- disable LLVM JIT and build with stubs (default
#                   auto-detects llvm-config)
#   PREFIX       -- install prefix, default /usr/local
#
# This Makefile coexists with meson.build / ninja; pick whichever
# build system you prefer.  meson is recommended for development
# (faster incremental builds, sanitizer presets, test isolation).

# -------------------------------------------------------------------
# Configuration
# -------------------------------------------------------------------

CC      ?= cc
AR      ?= ar
PREFIX  ?= /usr/local

# Pull in user CFLAGS at the end so they win.
CSTD     := -std=c11 -D_GNU_SOURCE
CWARN    := -Wall -Wextra -Wno-unused-parameter -Wno-comment
COPT     := -O2 -g
CINCS    := -Iinclude -Isrc -Isrc/lex
CFLAGS_ALL := $(CSTD) $(CWARN) $(COPT) $(CINCS) $(CFLAGS)
LDFLAGS_ALL := -lpthread $(LDFLAGS)

# JIT: try llvm-config unless explicitly disabled.
JIT ?= auto
ifeq ($(JIT),0)
  CFLAGS_JIT  := -DLIME_NO_JIT
  LDFLAGS_JIT :=
else
  ifeq ($(shell command -v llvm-config 2>/dev/null),)
    # No llvm-config -- silently fall back to stubs.
    CFLAGS_JIT  := -DLIME_NO_JIT
    LDFLAGS_JIT :=
  else
    CFLAGS_JIT  := $(shell llvm-config --cflags)
    LDFLAGS_JIT := $(shell llvm-config --ldflags --libs core executionengine \
                    mcjit native analysis passes orcjit)
  endif
endif

# -------------------------------------------------------------------
# Layout
# -------------------------------------------------------------------

BUILD     := build
BIN       := bin
LIB       := lib

LIME_BIN  := $(BIN)/lime
LIME_LIB  := $(LIB)/liblime_parser.a

# Library translation units.
LIB_SRC := \
  src/version.c            \
  src/snapshot.c           \
  src/snapshot_build.c     \
  src/snapshot_modify.c    \
  src/token_table.c        \
  src/tokenize.c           \
  src/tokenize_simd.c      \
  src/parse_context.c      \
  src/parse_engine.c       \
  src/extension.c          \
  src/conflict.c           \
  src/conflict_detector.c  \
  src/disambiguation.c     \
  src/strategy_priority.c  \
  src/strategy_fork_resolve.c \
  src/mod_serialize.c      \
  src/dependency_resolver.c \
  src/parser_manager.c     \
  src/parser_plugin.c      \
  src/parser_operations.c  \
  src/merkle_tree.c        \
  src/parser_composition.c \
  src/parser_fork.c        \
  src/utf8.c               \
  src/lime_ast.c           \
  src/glr.c                \
  src/execution_policy.c   \
  src/extension_registry.c \
  src/grammar_context.c    \
  src/context_switch.c     \
  src/lime_error.c         \
  src/jit_context.c        \
  src/jit_codegen.c        \
  src/jit_policy.c         \
  src/jit_tokenizer.c      \

LIB_OBJ := $(LIB_SRC:%.c=$(BUILD)/%.o)

LEX_SRC := $(wildcard src/lex/*.c)
LEX_OBJ := $(LEX_SRC:%.c=$(BUILD)/%.o)

# Tests
TEST_LIST := \
  test_claims_proof    \
  test_runtime_parse   \
  test_destructor      \
  test_extension_e2e   \

TEST_BINS := $(addprefix $(BUILD)/test/,$(TEST_LIST))

# -------------------------------------------------------------------
# Default goal
# -------------------------------------------------------------------

.PHONY: all
all: $(LIME_BIN) $(LIME_LIB) $(TEST_BINS)

# -------------------------------------------------------------------
# lime generator
# -------------------------------------------------------------------

LIME_CFLAGS := $(CSTD) $(CWARN) $(COPT) $(CINCS) $(CFLAGS)
ifeq ($(LEX),1)
  LIME_OBJ := $(BUILD)/lime.o $(LEX_OBJ)
  LIME_CFLAGS += -DLIME_HAS_LEX_COMPILER
else
  LIME_OBJ := $(BUILD)/lime.o
endif

$(LIME_BIN): $(LIME_OBJ) | $(BIN)
	$(CC) -o $@ $^ $(LDFLAGS)

$(BUILD)/lime.o: lime.c | $(BUILD)
	$(CC) $(LIME_CFLAGS) -c $< -o $@

$(BUILD)/src/lex/%.o: src/lex/%.c | $(BUILD)/src/lex
	$(CC) $(LIME_CFLAGS) -c $< -o $@

# -------------------------------------------------------------------
# Runtime library
# -------------------------------------------------------------------

$(LIME_LIB): $(LIB_OBJ) | $(LIB)
	$(AR) rcs $@ $^

$(BUILD)/src/%.o: src/%.c | $(BUILD)/src
	$(CC) $(CFLAGS_ALL) $(CFLAGS_JIT) -c $< -o $@

# -------------------------------------------------------------------
# Tests
# -------------------------------------------------------------------
#
# Each test that needs a generated parser depends on the .y file
# being run through lime first.  The arithmetic grammar is shared
# by test_runtime_parse and test_extension_e2e; %_grammar.c/h are
# emitted into $(BUILD)/test/.
# -------------------------------------------------------------------

$(BUILD)/test/test_claims_proof: tests/test_claims_proof.c $(LIME_LIB) | $(BUILD)/test
	$(CC) $(CFLAGS_ALL) $(CFLAGS_JIT) -o $@ $< $(LIME_LIB) \
	  $(LDFLAGS_ALL) $(LDFLAGS_JIT)

$(BUILD)/test/bench_arith_grammar.c \
$(BUILD)/test/bench_arith_grammar.h \
$(BUILD)/test/bench_arith_grammar_snapshot.c: bench/bench_arith_grammar.y $(LIME_BIN) | $(BUILD)/test
	$(LIME_BIN) -T./limpar.c -n -d$(BUILD)/test $<

$(BUILD)/test/test_runtime_parse: tests/test_runtime_parse.c \
    $(BUILD)/test/bench_arith_grammar.c \
    $(BUILD)/test/bench_arith_grammar_snapshot.c \
    $(LIME_LIB) | $(BUILD)/test
	$(CC) $(CFLAGS_ALL) $(CFLAGS_JIT) -I$(BUILD)/test -o $@ \
	  tests/test_runtime_parse.c \
	  $(BUILD)/test/bench_arith_grammar.c \
	  $(BUILD)/test/bench_arith_grammar_snapshot.c \
	  $(LIME_LIB) $(LDFLAGS_ALL) $(LDFLAGS_JIT)

$(BUILD)/test/test_extension_e2e: tests/test_extension_e2e.c \
    $(BUILD)/test/bench_arith_grammar.c \
    $(BUILD)/test/bench_arith_grammar_snapshot.c \
    $(LIME_LIB) | $(BUILD)/test
	$(CC) $(CFLAGS_ALL) $(CFLAGS_JIT) -I$(BUILD)/test -o $@ \
	  tests/test_extension_e2e.c \
	  $(BUILD)/test/bench_arith_grammar.c \
	  $(BUILD)/test/bench_arith_grammar_snapshot.c \
	  $(LIME_LIB) $(LDFLAGS_ALL) $(LDFLAGS_JIT)

$(BUILD)/test/test_destructor_grammar.c \
$(BUILD)/test/test_destructor_grammar.h: tests/test_destructor_grammar.y $(LIME_BIN) | $(BUILD)/test
	$(LIME_BIN) -T./limpar.c -d$(BUILD)/test $<

$(BUILD)/test/test_destructor: tests/test_destructor.c \
    $(BUILD)/test/test_destructor_grammar.c \
    $(LIME_LIB) | $(BUILD)/test
	$(CC) $(CFLAGS_ALL) $(CFLAGS_JIT) -I$(BUILD)/test -Itests -o $@ \
	  tests/test_destructor.c \
	  $(BUILD)/test/test_destructor_grammar.c \
	  $(LIME_LIB) $(LDFLAGS_ALL) $(LDFLAGS_JIT)

# -------------------------------------------------------------------
# Phony targets
# -------------------------------------------------------------------

.PHONY: test format install clean help

test: $(TEST_BINS)
	@set -e; for t in $(TEST_BINS); do echo "=== $$t ==="; $$t || exit 1; done

format:
	@command -v clang-format >/dev/null || { \
	  echo "clang-format not in PATH; skipping" >&2; exit 0; }
	clang-format -i \
	  src/*.c src/*.h src/lex/*.c src/lex/*.h \
	  include/*.h \
	  tests/test_claims_proof.c tests/test_runtime_parse.c \
	  tests/test_destructor.c tests/test_extension_e2e.c \
	  bench/bench_jit_real_parser.c

install: $(LIME_BIN) $(LIME_LIB)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -d $(DESTDIR)$(PREFIX)/lib
	install -d $(DESTDIR)$(PREFIX)/include/lime
	install -m 0755 $(LIME_BIN) $(DESTDIR)$(PREFIX)/bin/
	install -m 0644 $(LIME_LIB) $(DESTDIR)$(PREFIX)/lib/
	install -m 0644 include/*.h $(DESTDIR)$(PREFIX)/include/lime/
	install -m 0644 src/snapshot.h src/snapshot_modify.h src/extension.h \
	    $(DESTDIR)$(PREFIX)/include/lime/
	install -m 0644 limpar.c $(DESTDIR)$(PREFIX)/share/lime/limpar.c

clean:
	rm -rf $(BUILD) $(BIN) $(LIB)

help:
	@echo "Targets:"
	@echo "  all      -- lime + library + tests"
	@echo "  test     -- run the test programs (after building)"
	@echo "  format   -- run clang-format"
	@echo "  install  -- install to PREFIX (default /usr/local)"
	@echo "  clean    -- remove build artifacts"
	@echo "Knobs: CC, CFLAGS, LDFLAGS, LEX=1, JIT=0, PREFIX"

# -------------------------------------------------------------------
# Directory creation
# -------------------------------------------------------------------

$(BUILD) $(BUILD)/src $(BUILD)/src/lex $(BUILD)/test $(BIN) $(LIB):
	mkdir -p $@

# -------------------------------------------------------------------
# Compatibility wrappers for the previous Makefile that just wrapped meson.
# -------------------------------------------------------------------

BUILDDIR ?= builddir

.PHONY: meson-setup meson-build meson-test meson-clean meson-reconfigure

meson-setup:
	meson setup $(BUILDDIR)

meson-build: meson-setup
	ninja -C $(BUILDDIR)

meson-test: meson-build
	meson test -C $(BUILDDIR) --print-errorlogs

meson-clean:
	rm -rf $(BUILDDIR)

meson-reconfigure:
	meson setup --reconfigure $(BUILDDIR)
