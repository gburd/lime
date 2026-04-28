# Makefile for Calculator example with runtime extension loading.
#
# Builds:
#   calc             — host executable (base 4-operation calculator)
#   libcalc_power.so — extension shared library (adds ^ operator)

# Paths
LIME      ?= ../../lime
CC        ?= cc
CFLAGS    ?= -std=c11 -Wall -Wextra -Wno-unused-parameter -g -O2
LDFLAGS   ?=
TEMPLATE  ?= ../../limpar.c

.PHONY: all test clean distclean

all: calc libcalc_power.so

# Build the lime tool if it doesn't exist
$(LIME):
	$(MAKE) -C ../.. lime

# --- Grammar generation -----------------------------------------------

# Lime generates calc.c / calc.h from calc.lime
calc.c calc.h: calc.lime $(LIME) $(TEMPLATE)
	$(LIME) -T$(TEMPLATE) calc.lime
	@test -f calc.c || { echo "ERROR: Lime failed to generate calc.c"; exit 1; }

# Lime generates calc_power.c / calc_power.h from calc_power.lime
calc_power.c calc_power.h: calc_power.lime $(LIME) $(TEMPLATE)
	$(LIME) -T$(TEMPLATE) calc_power.lime
	@test -f calc_power.c || { echo "ERROR: Lime failed to generate calc_power.c"; exit 1; }

# --- Host executable ---------------------------------------------------

calc: main.o calc_parser.o tokenize.o
	$(CC) $(LDFLAGS) -o $@ $^ -ldl

main.o: main.c tokenize.h calc.h
	$(CC) $(CFLAGS) -c -o $@ main.c

calc_parser.o: calc.c calc.h
	$(CC) $(CFLAGS) -c -o $@ calc.c

tokenize.o: tokenize.c tokenize.h
	$(CC) $(CFLAGS) -c -o $@ tokenize.c

# --- Extension shared library ------------------------------------------

libcalc_power.so: calc_power_ext.o calc_power_parser.o tokenize_ext.o
	$(CC) -shared -o $@ $^

calc_power_ext.o: calc_power_ext.c tokenize.h calc_power.h
	$(CC) $(CFLAGS) -fPIC -c -o $@ calc_power_ext.c

calc_power_parser.o: calc_power.c calc_power.h
	$(CC) $(CFLAGS) -fPIC -c -o $@ calc_power.c

tokenize_ext.o: tokenize.c tokenize.h
	$(CC) $(CFLAGS) -fPIC -c -o $@ tokenize.c

# --- Test / run --------------------------------------------------------

test: calc libcalc_power.so
	./calc

# --- Clean up ----------------------------------------------------------

clean:
	rm -f *.o calc libcalc_power.so

distclean: clean
	rm -f calc.c calc.h calc.out
	rm -f calc_power.c calc_power.h calc_power.out
