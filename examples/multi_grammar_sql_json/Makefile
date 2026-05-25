# Makefile for the multi_grammar_sql_json example.
#
# Reuses the JSON example's grammar + helpers as the embedded
# language.  The host SQL recogniser is hand-rolled in
# multi_driver.c.

LIME      ?= ../../bin/lime
LIME_LIB  ?= ../../lib/liblime_parser.a
LIME_TPL  ?= ../../limpar.c
JSON_DIR  ?= ../json
CC        ?= cc
CFLAGS    ?= -O2 -g -Wall -Wextra -Wno-unused-parameter -std=c11
CPPFLAGS_LOCAL = -I. -I$(JSON_DIR) -I../../include -I../../src
LDLIBS    ?= -lpthread

GEN_SRCS = $(JSON_DIR)/json_grammar.c
GEN_HDRS = $(JSON_DIR)/json_grammar.h

LOCAL_SRCS = main.c multi_driver.c multi_helpers.c \
             $(JSON_DIR)/json_helpers.c $(JSON_DIR)/json_tokenize.c
SRCS  = $(LOCAL_SRCS) $(GEN_SRCS)
OBJS  = $(LOCAL_SRCS:.c=.o) json_grammar.o
BIN   = multi_grammar_sql_json

.PHONY: all clean test
all: $(BIN)

$(BIN): $(OBJS) $(LIME_LIB)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LIME_LIB) $(LDLIBS)

$(GEN_SRCS) $(GEN_HDRS): $(JSON_DIR)/json_grammar.lime $(LIME)
	$(MAKE) -C $(JSON_DIR) json_grammar.c json_grammar.h

json_grammar.o: $(GEN_SRCS)
	$(CC) $(CFLAGS) $(CPPFLAGS_LOCAL) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS_LOCAL) -c $< -o $@

main.o:           main.c          multi.h $(JSON_DIR)/json.h $(GEN_HDRS)
multi_driver.o:   multi_driver.c  multi.h $(JSON_DIR)/json.h $(JSON_DIR)/json_tokenize.h $(GEN_HDRS)
multi_helpers.o:  multi_helpers.c multi.h $(JSON_DIR)/json.h

test: $(BIN)
	./$(BIN) "SELECT id, json '{\"a\":1}' FROM t WHERE id = 5;"

clean:
	rm -f $(OBJS) $(BIN)
