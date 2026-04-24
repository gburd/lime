/*-------------------------------------------------------------------------
 *
 * boot_actions.c
 *    Semantic action helpers for the BKI bootstrap parser (Lime version).
 *
 * This file implements all the semantic actions that were originally
 * embedded in bootparse.y's grammar rules. In the Lime conversion,
 * these are called from grammar reduction actions.
 *
 * When building standalone (outside PostgreSQL), these functions
 * provide a simulation that validates BKI syntax and tracks the
 * parsed commands. When built inside PostgreSQL (BOOT_INSIDE_PG),
 * they would call the real catalog manipulation functions.
 *
 * Portions Copyright (c) 1996-2026, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *-------------------------------------------------------------------------
 */
#include "boot_gram_defs.h"

#include <stdarg.h>

/* ---------------------------------------------------------------
 * Memory management
 *
 * Simple tracked allocation for per-line cleanup, analogous to
 * the MemoryContext approach in the original PostgreSQL code.
 * --------------------------------------------------------------- */

static void
track_alloc(BootParseState *pstate, char *ptr)
{
    if (pstate->num_allocs >= pstate->max_allocs)
    {
        int new_max = pstate->max_allocs * 2;
        if (new_max < 64)
            new_max = 64;
        pstate->allocs = realloc(pstate->allocs, new_max * sizeof(char *));
        pstate->max_allocs = new_max;
    }
    pstate->allocs[pstate->num_allocs++] = ptr;
}

char *
boot_alloc(BootParseState *pstate, size_t size)
{
    char *ptr = malloc(size);
    if (!ptr)
    {
        fprintf(stderr, "boot_alloc: out of memory\n");
        exit(1);
    }
    track_alloc(pstate, ptr);
    return ptr;
}

char *
boot_pstrdup(BootParseState *pstate, const char *s)
{
    if (!s) return NULL;
    size_t len = strlen(s);
    char *dup = boot_alloc(pstate, len + 1);
    memcpy(dup, s, len + 1);
    return dup;
}

void
boot_free_line_allocs(BootParseState *pstate)
{
    for (int i = 0; i < pstate->num_allocs; i++)
        free(pstate->allocs[i]);
    pstate->num_allocs = 0;
}

/* ---------------------------------------------------------------
 * Per-line processing markers
 *
 * In the original, do_start() switches to per-line memory context
 * and do_end() resets it. Here we just manage our tracked allocations.
 * --------------------------------------------------------------- */

void
boot_do_start(BootParseState *pstate)
{
    /* Nothing special needed in standalone mode */
    (void)pstate;
}

void
boot_do_end(BootParseState *pstate)
{
    /* Free per-line allocations */
    boot_free_line_allocs(pstate);
}

/* ---------------------------------------------------------------
 * Relation operations
 * --------------------------------------------------------------- */

void
boot_openrel(BootParseState *pstate, const char *relname)
{
#ifdef BOOT_INSIDE_PG
    /* In PostgreSQL: calls the real boot_openrel() */
    (void)pstate;
    boot_openrel(relname);
#else
    /* Standalone: just track the relation name */
    if (pstate->current_rel)
        free(pstate->current_rel);
    pstate->current_rel = strdup(relname);
    fprintf(stdout, "OPEN %s\n", relname);
#endif
}

void
boot_closerel(BootParseState *pstate, const char *relname)
{
#ifdef BOOT_INSIDE_PG
    (void)pstate;
    closerel(relname);
#else
    fprintf(stdout, "CLOSE %s\n", relname ? relname : "(current)");
    if (pstate->current_rel)
    {
        free(pstate->current_rel);
        pstate->current_rel = NULL;
    }
    pstate->current_rel_cols = 0;
#endif
}

/* ---------------------------------------------------------------
 * CREATE command
 * --------------------------------------------------------------- */

void
boot_do_define_attr(BootParseState *pstate, const char *colname,
                    const char *typname, int nullness)
{
    /* Grow column array if needed */
    if (pstate->num_columns >= pstate->max_columns)
    {
        int new_max = pstate->max_columns * 2;
        if (new_max < 16)
            new_max = 16;
        pstate->columns = realloc(pstate->columns,
                                  new_max * sizeof(BootColumnDef));
        pstate->max_columns = new_max;
    }

    BootColumnDef *col = &pstate->columns[pstate->num_columns++];
    col->colname = strdup(colname);
    col->typname = strdup(typname);
    col->nullness = nullness;
}

void
boot_do_create(BootParseState *pstate, const char *relname, Oid oid,
               int bootstrap, int shared, Oid rowtypeoid)
{
#ifdef BOOT_INSIDE_PG
    /* In PostgreSQL, this would call heap_create or heap_create_with_catalog */
    (void)pstate;
    (void)relname;
    (void)oid;
    (void)bootstrap;
    (void)shared;
    (void)rowtypeoid;
#else
    fprintf(stdout, "CREATE%s%s %s %u",
            bootstrap ? " BOOTSTRAP" : "",
            shared ? " SHARED" : "",
            relname, oid);
    if (rowtypeoid != InvalidOid)
        fprintf(stdout, " ROWTYPE_OID %u", rowtypeoid);
    fprintf(stdout, " (");
    for (int i = 0; i < pstate->num_columns; i++)
    {
        if (i > 0) fprintf(stdout, ", ");
        fprintf(stdout, "%s = %s", pstate->columns[i].colname,
                pstate->columns[i].typname);
        if (pstate->columns[i].nullness == BOOTCOL_NULL_FORCE_NOT_NULL)
            fprintf(stdout, " FORCE NOT NULL");
        else if (pstate->columns[i].nullness == BOOTCOL_NULL_FORCE_NULL)
            fprintf(stdout, " FORCE NULL");
    }
    fprintf(stdout, ")\n");

    /* Track column count for later INSERT validation */
    pstate->current_rel_cols = pstate->num_columns;

    /* Clean up column definitions */
    for (int i = 0; i < pstate->num_columns; i++)
    {
        free(pstate->columns[i].colname);
        free(pstate->columns[i].typname);
    }
    pstate->num_columns = 0;
#endif
}

/* ---------------------------------------------------------------
 * INSERT command
 * --------------------------------------------------------------- */

void
boot_do_insert_one_value(BootParseState *pstate, const char *value)
{
    /* Grow value arrays if needed */
    if (pstate->num_values >= pstate->max_values)
    {
        int new_max = pstate->max_values * 2;
        if (new_max < 64)
            new_max = 64;
        pstate->values = realloc(pstate->values, new_max * sizeof(char *));
        pstate->nulls = realloc(pstate->nulls, new_max * sizeof(int));
        pstate->max_values = new_max;
    }

    pstate->values[pstate->num_values] = strdup(value);
    pstate->nulls[pstate->num_values] = 0;
    pstate->num_values++;
}

void
boot_do_insert_one_null(BootParseState *pstate)
{
    /* Grow value arrays if needed */
    if (pstate->num_values >= pstate->max_values)
    {
        int new_max = pstate->max_values * 2;
        if (new_max < 64)
            new_max = 64;
        pstate->values = realloc(pstate->values, new_max * sizeof(char *));
        pstate->nulls = realloc(pstate->nulls, new_max * sizeof(int));
        pstate->max_values = new_max;
    }

    pstate->values[pstate->num_values] = NULL;
    pstate->nulls[pstate->num_values] = 1;
    pstate->num_values++;
}

void
boot_do_insert(BootParseState *pstate)
{
#ifdef BOOT_INSIDE_PG
    /* In PostgreSQL: validate column count and call InsertOneTuple */
    (void)pstate;
#else
    fprintf(stdout, "INSERT (");
    for (int i = 0; i < pstate->num_values; i++)
    {
        if (i > 0) fprintf(stdout, " ");
        if (pstate->nulls[i])
            fprintf(stdout, "_null_");
        else
            fprintf(stdout, "%s", pstate->values[i]);
    }
    fprintf(stdout, ")\n");

    /* Validate column count if we know it */
    if (pstate->current_rel_cols > 0 &&
        pstate->num_values != pstate->current_rel_cols)
    {
        fprintf(stderr, "WARNING: incorrect number of columns in row "
                "(expected %d, got %d)\n",
                pstate->current_rel_cols, pstate->num_values);
    }

    /* Clean up values */
    for (int i = 0; i < pstate->num_values; i++)
    {
        if (pstate->values[i])
            free(pstate->values[i]);
    }
    pstate->num_values = 0;
#endif
}

/* ---------------------------------------------------------------
 * Index operations
 * --------------------------------------------------------------- */

BootIndexParam *
boot_make_index_param(BootParseState *pstate, const char *colname,
                      const char *opclass)
{
    BootIndexParam *p = (BootIndexParam *)boot_alloc(pstate, sizeof(BootIndexParam));
    p->colname = boot_pstrdup(pstate, colname);
    p->opclass = boot_pstrdup(pstate, opclass);
    return p;
}

BootIndexParamList *
boot_index_param_list_create(BootParseState *pstate, BootIndexParam *param)
{
    BootIndexParamList *list =
        (BootIndexParamList *)boot_alloc(pstate, sizeof(BootIndexParamList));
    list->capacity = 8;
    list->params = (BootIndexParam **)boot_alloc(pstate,
                                                  list->capacity * sizeof(BootIndexParam *));
    list->count = 0;
    list->params[list->count++] = param;
    return list;
}

BootIndexParamList *
boot_index_param_list_append(BootParseState *pstate,
                             BootIndexParamList *list,
                             BootIndexParam *param)
{
    if (list->count >= list->capacity)
    {
        int new_cap = list->capacity * 2;
        BootIndexParam **new_params =
            (BootIndexParam **)boot_alloc(pstate, new_cap * sizeof(BootIndexParam *));
        memcpy(new_params, list->params, list->count * sizeof(BootIndexParam *));
        list->params = new_params;
        list->capacity = new_cap;
    }
    list->params[list->count++] = param;
    return list;
}

void
boot_do_declare_index(BootParseState *pstate, const char *idxname,
                      Oid idxoid, const char *relname,
                      const char *amname, BootIndexParamList *params,
                      int is_unique)
{
#ifdef BOOT_INSIDE_PG
    /* In PostgreSQL: build IndexStmt and call DefineIndex */
    (void)pstate;
    (void)idxname;
    (void)idxoid;
    (void)relname;
    (void)amname;
    (void)params;
    (void)is_unique;
#else
    fprintf(stdout, "DECLARE%s INDEX %s %u ON %s USING %s (",
            is_unique ? " UNIQUE" : "",
            idxname, idxoid, relname, amname);
    for (int i = 0; i < params->count; i++)
    {
        if (i > 0) fprintf(stdout, ", ");
        fprintf(stdout, "%s %s",
                params->params[i]->colname,
                params->params[i]->opclass);
    }
    fprintf(stdout, ")\n");
#endif
}

/* ---------------------------------------------------------------
 * Toast operations
 * --------------------------------------------------------------- */

void
boot_do_declare_toast(BootParseState *pstate, const char *relname,
                      Oid toastoid, Oid indexoid)
{
#ifdef BOOT_INSIDE_PG
    /* In PostgreSQL: call BootstrapToastTable */
    (void)pstate;
    (void)relname;
    (void)toastoid;
    (void)indexoid;
#else
    fprintf(stdout, "DECLARE TOAST %u %u ON %s\n",
            toastoid, indexoid, relname);
#endif
}

/* ---------------------------------------------------------------
 * Build indices
 * --------------------------------------------------------------- */

void
boot_do_build_indices(BootParseState *pstate)
{
#ifdef BOOT_INSIDE_PG
    (void)pstate;
    build_indices();
#else
    fprintf(stdout, "BUILD INDICES\n");
    (void)pstate;
#endif
}

/* ---------------------------------------------------------------
 * OID conversion
 * --------------------------------------------------------------- */

Oid
boot_atooid(const char *s)
{
    unsigned long val;
    char *endptr;

    if (!s || !*s)
        return InvalidOid;

    errno = 0;
    val = strtoul(s, &endptr, 10);
    if (errno != 0 || *endptr != '\0')
        return InvalidOid;

    return (Oid)val;
}

/* ---------------------------------------------------------------
 * Error handling
 * --------------------------------------------------------------- */

void
boot_yyerror(BootParseState *pstate, const char *msg)
{
    snprintf(pstate->last_error, sizeof(pstate->last_error),
             "%s at line %d", msg, pstate->lineno);
    pstate->error_count++;
    fprintf(stderr, "ERROR: %s\n", pstate->last_error);
}

/* ---------------------------------------------------------------
 * Parser state lifecycle
 * --------------------------------------------------------------- */

BootParseState *
boot_parse_state_create(void)
{
    BootParseState *pstate = calloc(1, sizeof(BootParseState));
    if (!pstate)
    {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    pstate->lineno = 1;
    return pstate;
}

void
boot_parse_state_destroy(BootParseState *pstate)
{
    if (!pstate)
        return;

    /* Free tracked allocations */
    boot_free_line_allocs(pstate);
    free(pstate->allocs);

    /* Free columns */
    for (int i = 0; i < pstate->num_columns; i++)
    {
        free(pstate->columns[i].colname);
        free(pstate->columns[i].typname);
    }
    free(pstate->columns);

    /* Free values */
    for (int i = 0; i < pstate->num_values; i++)
    {
        if (pstate->values[i])
            free(pstate->values[i]);
    }
    free(pstate->values);
    free(pstate->nulls);

    /* Free relation name */
    free(pstate->current_rel);

    free(pstate);
}

void
boot_parse_state_set_input(BootParseState *pstate, const char *input,
                           int length)
{
    pstate->input = input;
    pstate->pos = 0;
    pstate->length = length;
    pstate->lineno = 1;
    pstate->error_count = 0;
    pstate->last_error[0] = '\0';
}
