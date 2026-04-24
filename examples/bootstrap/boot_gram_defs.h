/*-------------------------------------------------------------------------
 *
 * boot_gram_defs.h
 *    Type definitions and function declarations for the BKI bootstrap
 *    parser (Lime-generated).
 *
 * This header provides the types and interfaces needed by the Lime
 * grammar file (boot_grammar.lime), the tokenizer (boot_tokenize.c),
 * and the semantic action helpers (boot_actions.c).
 *
 * Portions Copyright (c) 1996-2026, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *-------------------------------------------------------------------------
 */
#ifndef BOOT_GRAM_DEFS_H
#define BOOT_GRAM_DEFS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

/*
 * When building standalone (outside PostgreSQL), define Oid and related types.
 * When building inside PostgreSQL, these come from postgres.h.
 */
#ifndef BOOT_INSIDE_PG

typedef unsigned int Oid;
#define InvalidOid  ((Oid) 0)

#endif /* BOOT_INSIDE_PG */

/* ---------------------------------------------------------------
 * Column nullness constants (matching PostgreSQL's bootparse.y)
 * --------------------------------------------------------------- */
#define BOOTCOL_NULL_AUTO           0
#define BOOTCOL_NULL_FORCE_NOT_NULL 1
#define BOOTCOL_NULL_FORCE_NULL     2

/* ---------------------------------------------------------------
 * Token value structure
 *
 * Each token carries either a string value (.str for identifiers
 * and literals) or a keyword string (.kw for keyword tokens).
 * --------------------------------------------------------------- */
typedef struct BootToken {
    char       *str;        /* dynamically allocated string (ID tokens) */
    const char *kw;         /* static keyword string (keyword tokens) */
    int         ival;       /* integer value (unused in bootstrap grammar) */
} BootToken;

/* ---------------------------------------------------------------
 * Index parameter: a column name paired with an operator class
 * --------------------------------------------------------------- */
typedef struct BootIndexParam {
    char *colname;
    char *opclass;
} BootIndexParam;

/* ---------------------------------------------------------------
 * Index parameter list (simple growable array)
 * --------------------------------------------------------------- */
typedef struct BootIndexParamList {
    BootIndexParam **params;
    int              count;
    int              capacity;
} BootIndexParamList;

/* ---------------------------------------------------------------
 * Column definition (for tracking during CREATE)
 * --------------------------------------------------------------- */
typedef struct BootColumnDef {
    char *colname;
    char *typname;
    int   nullness;
} BootColumnDef;

/* ---------------------------------------------------------------
 * Parser state structure (passed as %extra_argument)
 *
 * This replaces the combination of Bison's %parse-param,
 * %lex-param, and the static globals from the original grammar.
 * --------------------------------------------------------------- */
typedef struct BootParseState {
    /* Input */
    const char *input;          /* BKI input string */
    int         pos;            /* current scan position */
    int         length;         /* total input length */
    int         lineno;         /* current line number */

    /* Column tracking for CREATE */
    BootColumnDef *columns;     /* column definitions array */
    int            num_columns; /* number of columns defined */
    int            max_columns; /* capacity of columns array */

    /* Value tracking for INSERT */
    char      **values;         /* column values for current INSERT */
    int        *nulls;          /* null flags for current INSERT */
    int         num_values;     /* number of values read */
    int         max_values;     /* capacity of values/nulls arrays */

    /* Current relation context */
    char       *current_rel;    /* name of currently open relation */
    int         current_rel_cols; /* number of columns in open relation */

    /* Error state */
    int         error_count;
    char        last_error[256];

    /* Memory management (simple arena for per-line allocations) */
    char      **allocs;         /* tracked allocations */
    int         num_allocs;
    int         max_allocs;
} BootParseState;

/* ---------------------------------------------------------------
 * Tokenizer interface
 * --------------------------------------------------------------- */

/*
 * boot_scan_next - Scan the next token from the input.
 *
 * Returns the token type (as defined in boot_grammar.h).
 * The token value is stored in *token_val.
 */
int boot_scan_next(BootParseState *pstate, BootToken *token_val);

/* ---------------------------------------------------------------
 * Semantic action helpers (boot_actions.c)
 * --------------------------------------------------------------- */

/* Memory management */
char *boot_alloc(BootParseState *pstate, size_t size);
char *boot_pstrdup(BootParseState *pstate, const char *s);
void  boot_free_line_allocs(BootParseState *pstate);

/* Per-line processing markers */
void boot_do_start(BootParseState *pstate);
void boot_do_end(BootParseState *pstate);

/* Relation operations */
void boot_openrel(BootParseState *pstate, const char *relname);
void boot_closerel(BootParseState *pstate, const char *relname);

/* CREATE command */
void boot_do_define_attr(BootParseState *pstate, const char *colname,
                         const char *typname, int nullness);
void boot_do_create(BootParseState *pstate, const char *relname, Oid oid,
                    int bootstrap, int shared, Oid rowtypeoid);

/* INSERT command */
void boot_do_insert_one_value(BootParseState *pstate, const char *value);
void boot_do_insert_one_null(BootParseState *pstate);
void boot_do_insert(BootParseState *pstate);

/* Index operations */
BootIndexParam     *boot_make_index_param(BootParseState *pstate,
                                          const char *colname,
                                          const char *opclass);
BootIndexParamList *boot_index_param_list_create(BootParseState *pstate,
                                                  BootIndexParam *param);
BootIndexParamList *boot_index_param_list_append(BootParseState *pstate,
                                                  BootIndexParamList *list,
                                                  BootIndexParam *param);
void boot_do_declare_index(BootParseState *pstate, const char *idxname,
                           Oid idxoid, const char *relname,
                           const char *amname, BootIndexParamList *params,
                           int is_unique);

/* Toast operations */
void boot_do_declare_toast(BootParseState *pstate, const char *relname,
                           Oid toastoid, Oid indexoid);

/* Build indices */
void boot_do_build_indices(BootParseState *pstate);

/* OID conversion */
Oid  boot_atooid(const char *s);

/* Error handling */
void boot_yyerror(BootParseState *pstate, const char *msg);

/* ---------------------------------------------------------------
 * Parser state lifecycle
 * --------------------------------------------------------------- */
BootParseState *boot_parse_state_create(void);
void            boot_parse_state_destroy(BootParseState *pstate);
void            boot_parse_state_set_input(BootParseState *pstate,
                                           const char *input, int length);

#endif /* BOOT_GRAM_DEFS_H */
