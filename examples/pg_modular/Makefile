# Makefile for modular PostgreSQL grammar
#
# Uses lime-compose to assemble literate modules into a single .lime file,
# then runs the Lime parser generator.

# Paths
LIME         ?= ../../lime
LIME_COMPOSE ?= ../../tools/lime-compose
CC           ?= cc
CFLAGS       ?= -std=c11 -Wall -Wextra -g -O2
TEMPLATE     ?= ../../limpar.c

# Module files (in dependency order via lime-compose)
MODULES = \
	config.md \
	tokens.md \
	base/type_decls.md \
	base/toplevel.md \
	base/helpers.md \
	base/keywords.md \
	utility/variables.md \
	base/typenames.md \
	expr/expressions.md \
	from_clause/from.md \
	select_targets/clauses.md \
	window/window.md \
	cte/cte.md \
	dml/select.md \
	dml/insert.md \
	dml/update.md \
	dml/delete.md \
	dml/merge.md \
	ddl/tables.md \
	ddl/alter_table.md \
	ddl/index.md \
	ddl/sequence.md \
	ddl/drop.md \
	ddl/alter_misc.md \
	ddl/extensions.md \
	ddl/schema.md \
	functions/functions.md \
	security/security.md \
	transactions/transactions.md \
	ddl/views_triggers.md \
	utility/utility.md \
	expr/json_xml.md \
	expr/graph.md \
	ddl/pub_sub.md \
	base/plpgsql.md

# Output files
COMBINED    = pg_grammar.lime
PARSER_C    = pg_grammar.c
PARSER_H    = pg_grammar.h

.PHONY: all compose generate validate list-modules clean

all: generate

# Validate module dependencies
validate:
	$(LIME_COMPOSE) --dry-run $(MODULES)

# List all modules
list-modules:
	$(LIME_COMPOSE) --list-modules $(MODULES)

# Compose modules into single .lime file
compose: $(COMBINED)

$(COMBINED): $(MODULES)
	$(LIME_COMPOSE) -v -o $(COMBINED) $(MODULES)

# Generate parser C source from composed grammar
generate: $(PARSER_C)

$(PARSER_C) $(PARSER_H): $(COMBINED) $(LIME) $(TEMPLATE)
	$(LIME) -T$(TEMPLATE) $(COMBINED)
	@test -f $(PARSER_C) || { echo "ERROR: Lime failed"; exit 1; }

$(LIME):
	$(MAKE) -C ../.. lime

clean:
	rm -f $(COMBINED) $(PARSER_C) $(PARSER_H) pg_grammar.out
