# Makefile for Calculator example with runtime extension loading.
#
# Builds:
#   calc               -- host executable (base 4-operation calculator)
#   libcalc_power.so   -- extension shared library (adds ^ as exponentiation)
#   libcalc_bitwise.so -- extension shared library (adds &, |, ^, <<, >>)
#   calc_conflicts     -- conflict demonstration (loads both extensions)

# Paths
LIME      ?= ../../lime
CC        ?= cc
CFLAGS    ?= -std=c11 -Wall -Wextra -Wno-unused-parameter -g -O2
LDFLAGS   ?=
TEMPLATE  ?= ../../limpar.c

.PHONY: all test test-conflicts clean distclean

all: calc libcalc_power.so libcalc_bitwise.so calc_conflicts

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

# Lime generates calc_bitwise.c / calc_bitwise.h from calc_bitwise.lime
calc_bitwise.c calc_bitwise.h: calc_bitwise.lime $(LIME) $(TEMPLATE)
	$(LIME) -T$(TEMPLATE) calc_bitwise.lime
	@test -f calc_bitwise.c || { echo "ERROR: Lime failed to generate calc_bitwise.c"; exit 1; }

# --- Host executable ---------------------------------------------------

calc: main.o calc_parser.o tokenize.o
	$(CC) $(LDFLAGS) -o $@ $^ -ldl

main.o: main.c tokenize.h calc.h
	$(CC) $(CFLAGS) -c -o $@ main.c

calc_parser.o: calc.c calc.h
	$(CC) $(CFLAGS) -c -o $@ calc.c

tokenize.o: tokenize.c tokenize.h
	$(CC) $(CFLAGS) -c -o $@ tokenize.c

# --- Conflict demonstration executable --------------------------------

calc_conflicts: main_conflicts.o calc_parser.o tokenize.o
	$(CC) $(LDFLAGS) -o $@ $^ -ldl

main_conflicts.o: main_conflicts.c tokenize.h calc.h
	$(CC) $(CFLAGS) -c -o $@ main_conflicts.c

# --- Power extension shared library -----------------------------------

libcalc_power.so: calc_power_ext.o calc_power_parser.o tokenize_power.o
	$(CC) -shared -o $@ $^

calc_power_ext.o: calc_power_ext.c tokenize.h calc_power.h
	$(CC) $(CFLAGS) -fPIC -c -o $@ calc_power_ext.c

calc_power_parser.o: calc_power.c calc_power.h
	$(CC) $(CFLAGS) -fPIC -c -o $@ calc_power.c

tokenize_power.o: tokenize.c tokenize.h
	$(CC) $(CFLAGS) -fPIC -c -o $@ tokenize.c

# --- Bitwise extension shared library ---------------------------------

libcalc_bitwise.so: calc_bitwise_ext.o calc_bitwise_parser.o tokenize_bitwise.o
	$(CC) -shared -o $@ $^

calc_bitwise_ext.o: calc_bitwise_ext.c tokenize.h calc_bitwise.h
	$(CC) $(CFLAGS) -fPIC -c -o $@ calc_bitwise_ext.c

calc_bitwise_parser.o: calc_bitwise.c calc_bitwise.h
	$(CC) $(CFLAGS) -fPIC -c -o $@ calc_bitwise.c

tokenize_bitwise.o: tokenize.c tokenize.h
	$(CC) $(CFLAGS) -fPIC -c -o $@ tokenize.c

# --- Test / run --------------------------------------------------------

test: calc libcalc_power.so
	./calc

test-conflicts: calc_conflicts libcalc_power.so libcalc_bitwise.so
	./calc_conflicts

# --- Clean up ----------------------------------------------------------

clean:
	rm -f *.o calc calc_conflicts libcalc_power.so libcalc_bitwise.so

distclean: clean
	rm -f calc.c calc.h calc.out
	rm -f calc_power.c calc_power.h calc_power.out
	rm -f calc_bitwise.c calc_bitwise.h calc_bitwise.out
