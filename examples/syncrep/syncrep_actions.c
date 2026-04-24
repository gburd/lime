/*-------------------------------------------------------------------------
 *
 * syncrep_actions.c
 *    Semantic action helpers for the synchronous replication config parser.
 *
 * Converted from: src/backend/replication/syncrep_gram.y (epilogue)
 *
 * Portions Copyright (c) 1996-2026, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *-------------------------------------------------------------------------
 */
#include "syncrep_defs.h"

/* ---------------------------------------------------------------
 * Memory management
 * --------------------------------------------------------------- */

static void
track_alloc(SyncRepParseState *pstate, char *ptr)
{
    if (pstate->num_allocs >= pstate->max_allocs)
    {
        int new_max = pstate->max_allocs * 2;
        if (new_max < 32)
            new_max = 32;
        pstate->allocs = realloc(pstate->allocs, new_max * sizeof(char *));
        pstate->max_allocs = new_max;
    }
    pstate->allocs[pstate->num_allocs++] = ptr;
}

char *
syncrep_alloc(SyncRepParseState *pstate, size_t size)
{
    char *ptr = malloc(size);
    if (!ptr)
    {
        fprintf(stderr, "syncrep_alloc: out of memory\n");
        exit(1);
    }
    track_alloc(pstate, ptr);
    return ptr;
}

char *
syncrep_pstrdup(SyncRepParseState *pstate, const char *s)
{
    if (!s) return NULL;
    size_t len = strlen(s);
    char *dup = syncrep_alloc(pstate, len + 1);
    memcpy(dup, s, len + 1);
    return dup;
}

void
syncrep_free_allocs(SyncRepParseState *pstate)
{
    for (int i = 0; i < pstate->num_allocs; i++)
        free(pstate->allocs[i]);
    pstate->num_allocs = 0;
}

/* ---------------------------------------------------------------
 * Name list operations
 * --------------------------------------------------------------- */

SyncRepNameList *
syncrep_name_list_create(SyncRepParseState *pstate, const char *name)
{
    SyncRepNameList *list =
        (SyncRepNameList *)syncrep_alloc(pstate, sizeof(SyncRepNameList));
    list->capacity = 8;
    list->names = (char **)syncrep_alloc(pstate,
                                          list->capacity * sizeof(char *));
    list->count = 0;
    list->names[list->count++] = syncrep_pstrdup(pstate, name);
    return list;
}

SyncRepNameList *
syncrep_name_list_append(SyncRepParseState *pstate,
                         SyncRepNameList *list,
                         const char *name)
{
    if (list->count >= list->capacity)
    {
        int new_cap = list->capacity * 2;
        char **new_names = (char **)syncrep_alloc(pstate,
                                                   new_cap * sizeof(char *));
        memcpy(new_names, list->names, list->count * sizeof(char *));
        list->names = new_names;
        list->capacity = new_cap;
    }
    list->names[list->count++] = syncrep_pstrdup(pstate, name);
    return list;
}

/* ---------------------------------------------------------------
 * Config construction
 *
 * This is the standalone equivalent of create_syncrep_config()
 * from the original syncrep_gram.y epilogue.
 * --------------------------------------------------------------- */

SyncRepConfig *
syncrep_create_config(SyncRepParseState *pstate,
                      const char *num_sync,
                      SyncRepNameList *members,
                      int syncrep_method)
{
    SyncRepConfig *config =
        (SyncRepConfig *)syncrep_alloc(pstate, sizeof(SyncRepConfig));

    config->num_sync = atoi(num_sync);
    config->syncrep_method = syncrep_method;
    config->nmembers = members->count;

    /* Copy member names */
    config->members = (char **)syncrep_alloc(pstate,
                                              members->count * sizeof(char *));
    for (int i = 0; i < members->count; i++)
        config->members[i] = members->names[i]; /* share pointer */

    return config;
}

/* ---------------------------------------------------------------
 * Config display
 * --------------------------------------------------------------- */

void
syncrep_config_print(const SyncRepConfig *config, FILE *fp)
{
    if (!config)
    {
        fprintf(fp, "(null config)\n");
        return;
    }

    const char *method_str =
        (config->syncrep_method == SYNC_REP_QUORUM) ? "QUORUM" : "PRIORITY";

    fprintf(fp, "method=%s num_sync=%d members=(",
            method_str, config->num_sync);

    for (int i = 0; i < config->nmembers; i++)
    {
        if (i > 0) fprintf(fp, ", ");
        fprintf(fp, "%s", config->members[i]);
    }
    fprintf(fp, ")\n");
}

void
syncrep_config_free(SyncRepConfig *config)
{
    /* Config is allocated through the parse state tracker,
     * so individual frees are handled by syncrep_free_allocs */
    (void)config;
}

/* ---------------------------------------------------------------
 * Error handling
 * --------------------------------------------------------------- */

void
syncrep_yyerror(SyncRepParseState *pstate, const char *msg)
{
    if (pstate->error_count == 0)
    {
        snprintf(pstate->last_error, sizeof(pstate->last_error),
                 "%s at position %d", msg, pstate->pos);
    }
    pstate->error_count++;
    fprintf(stderr, "ERROR: %s at position %d\n", msg, pstate->pos);
}

/* ---------------------------------------------------------------
 * Parser state lifecycle
 * --------------------------------------------------------------- */

SyncRepParseState *
syncrep_parse_state_create(void)
{
    SyncRepParseState *pstate = calloc(1, sizeof(SyncRepParseState));
    if (!pstate)
    {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    return pstate;
}

void
syncrep_parse_state_destroy(SyncRepParseState *pstate)
{
    if (!pstate)
        return;

    syncrep_free_allocs(pstate);
    free(pstate->allocs);
    free(pstate);
}

void
syncrep_parse_state_set_input(SyncRepParseState *pstate,
                              const char *input, int length)
{
    pstate->input = input;
    pstate->pos = 0;
    pstate->length = length;
    pstate->error_count = 0;
    pstate->last_error[0] = '\0';
    pstate->result = NULL;
}
