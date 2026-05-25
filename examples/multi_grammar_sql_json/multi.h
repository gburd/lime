/*
** multi.h -- shared types for the SQL+JSON multi-grammar example.
**
** Defines the SQL AST node types.  JSON nodes come from
** ../json/json.h (re-exported via -I../json) and appear inside
** SQL columns as JSON_LITERAL values.
*/
#ifndef LIME_EXAMPLE_MULTI_H
#define LIME_EXAMPLE_MULTI_H

#include "json.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/*  SQL AST                                                            */
/* ------------------------------------------------------------------ */

typedef enum SqlColumnKind {
    SQL_COL_IDENT,        /* a plain identifier, e.g. "id" */
    SQL_COL_JSON_LITERAL, /* json '{...}' literal -- holds a parsed JsonValue * */
} SqlColumnKind;

typedef struct SqlColumn {
    SqlColumnKind kind;
    int line;
    int col;
    union {
        char *ident;          /* SQL_COL_IDENT (heap-owned) */
        JsonValue *json_root; /* SQL_COL_JSON_LITERAL */
    };
} SqlColumn;

typedef struct SqlSelect {
    SqlColumn **columns;
    size_t ncolumns;
    size_t cap;
    char *table;     /* heap-owned */
    char *where_lhs; /* heap-owned */
    long where_rhs;  /* numeric */
} SqlSelect;

SqlSelect *sql_select_new(void);
void sql_select_add_column(SqlSelect *s, SqlColumn *col);
void sql_select_destroy(SqlSelect *s);

SqlColumn *sql_col_ident(const char *name, int line, int col);
SqlColumn *sql_col_json(JsonValue *root, int line, int col);

void sql_select_print(FILE *out, const SqlSelect *s);

/* ------------------------------------------------------------------ */
/*  Driver entry point (used by main.c and by test_context_switch_e2e) */
/* ------------------------------------------------------------------ */

typedef enum MultiParseStatus {
    MULTI_OK = 0,
    MULTI_LEX_ERROR,    /* unknown character / unterminated json literal */
    MULTI_PARSE_ERROR,  /* malformed SQL */
    MULTI_JSON_ERROR,   /* embedded JSON didn't parse */
    MULTI_NO_TRIGGER,   /* JSON trigger lexeme not registered */
} MultiParseStatus;

/*
** Parse one SQL statement (with possible embedded json '...' literals).
**
** On success, *out is filled with a fresh SqlSelect that the caller
** must free with sql_select_destroy().  On failure, *out is NULL and
** an error code is returned.
**
** If `register_json_trigger` is false, the example registers no
** triggers -- useful for testing the no-triggers fast path.
*/
MultiParseStatus multi_parse_sql(const char *input, bool register_json_trigger,
                                 SqlSelect **out);

#endif /* LIME_EXAMPLE_MULTI_H */
