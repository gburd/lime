# LLM Oracle Disambiguation Example
#
# Build targets:
#   make          - Build with libcurl support
#   make no-curl  - Build without libcurl (stub mode, for testing)
#   make clean    - Remove build artifacts

CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -O2 -g
INCDIR   = ../../include
SRCDIR   = ../../src
BUILDDIR = ../../builddir/src

# Library: lime_parser from the build directory
LIBS     = -L$(BUILDDIR) -llime_parser -lpthread -ldl

.PHONY: all no-curl clean

all: nlsql_demo

nlsql_demo: nlsql_extension.c llm_client.c llm_client.h
	$(CC) $(CFLAGS) -I$(INCDIR) -I$(SRCDIR) \
		nlsql_extension.c llm_client.c \
		$(LIBS) -lcurl \
		-o $@

no-curl: nlsql_extension.c llm_client.c llm_client.h
	$(CC) $(CFLAGS) -DLLM_NO_CURL -I$(INCDIR) -I$(SRCDIR) \
		nlsql_extension.c llm_client.c \
		$(LIBS) \
		-o nlsql_demo

clean:
	rm -f nlsql_demo *.o
