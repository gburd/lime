# Fan-out for the contrib/ SQL-dialect extension demos (Goal B:
# runtime grammar composition showcases).  Gated by the top-level
# `contrib` feature option (see meson_options.txt); only the
# subdirectories below are wired in because they are the ones that
# currently build cleanly against the extension API in src/extension.h.
#
# duckdb_features/ and edn_literals/ are NOT wired here: their grammar
# files declare rules (DuckDB's SELECT-modifying clauses, EDN's
# SQL-integration expression) that are meant to be merged into a base
# SQL grammar via MOD_ADD_RULE at runtime rather than parsed
# standalone, so `lime` reports "this rule can not be reduced" when
# compiling the .lime file on its own.  Exercising them end-to-end
# needs the extension reduce-callback dispatch described as not yet
# wired in docs/EXTENSIONS.md.  See the task summary for the full
# per-directory status.

subdir('mysql_compat')
subdir('oracle_compat')
subdir('sqlite_compat')
subdir('xml_query')
