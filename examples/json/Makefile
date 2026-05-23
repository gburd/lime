# Makefile for the Lime JSON example.

LIME      ?= ../../bin/lime
LIME_LIB  ?= ../../lib/liblime_parser.a
LIME_TPL  ?= ../../limpar.c
CC        ?= cc
CFLAGS    ?= -O2 -g -Wall -Wextra -Wno-unused-parameter -std=c11
# Use a private include-path variable so the user's CPPFLAGS env
# (set by nix-develop, etc.) doesn't shadow our -I directives.
LIME_CPPFLAGS = -I. -I../../include -I../../src
LDFLAGS   ?=
LDLIBS    ?= -lpthread

# LLVM JIT path is optional but lights up automatically when present.
ifneq (,$(wildcard /nix/store/*-llvm-*))
LDLIBS += -L$(firstword $(wildcard /nix/store/*-llvm-*-lib/lib)) -lLLVM-21
endif

GEN_SRCS = json_grammar.c
GEN_HDRS = json_grammar.h
SRCS     = main.c json_helpers.c json_tokenize.c $(GEN_SRCS)
OBJS     = $(SRCS:.c=.o)
BIN      = json_parser

.PHONY: all clean test
all: $(BIN)

$(BIN): $(OBJS) $(LIME_LIB)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LIME_LIB) $(LDLIBS)

$(GEN_SRCS) $(GEN_HDRS): json_grammar.lime $(LIME)
	$(LIME) -T$(LIME_TPL) -d. json_grammar.lime

%.o: %.c
	$(CC) $(CFLAGS) $(LIME_CPPFLAGS) $(CPPFLAGS) -c $< -o $@

main.o:          main.c          json.h json_tokenize.h json_grammar.h
json_helpers.o:  json_helpers.c  json.h
json_tokenize.o: json_tokenize.c json_tokenize.h json_grammar.h
json_grammar.o:  json_grammar.c  json.h json_grammar.h

test: $(BIN)
	@echo '{"hello":"world","n":42,"arr":[1,2,3]}' | ./$(BIN)

clean:
	rm -f $(OBJS) $(BIN) $(GEN_SRCS) $(GEN_HDRS) json_grammar.out
