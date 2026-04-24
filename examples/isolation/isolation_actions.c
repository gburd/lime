/*-------------------------------------------------------------------------
 *
 * isolation_actions.c
 *    Semantic action helpers for the isolation test spec parser.
 *
 * Converted from: src/test/isolation/specparse.y
 *
 * Portions Copyright (c) 1996-2026, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *-------------------------------------------------------------------------
 */
#include "isolation_defs.h"

/* ---------------------------------------------------------------
 * Memory management
 * --------------------------------------------------------------- */

static void
track_alloc(IsolParseState *pstate, char *ptr)
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
isol_alloc(IsolParseState *pstate, size_t size)
{
    char *ptr = malloc(size);
    if (!ptr)
    {
        fprintf(stderr, "isol_alloc: out of memory\n");
        exit(1);
    }
    track_alloc(pstate, ptr);
    return ptr;
}

char *
isol_pstrdup(IsolParseState *pstate, const char *s)
{
    if (!s) return NULL;
    size_t len = strlen(s);
    char *dup = isol_alloc(pstate, len + 1);
    memcpy(dup, s, len + 1);
    return dup;
}

void
isol_free_allocs(IsolParseState *pstate)
{
    for (int i = 0; i < pstate->num_allocs; i++)
        free(pstate->allocs[i]);
    pstate->num_allocs = 0;
}

/* ---------------------------------------------------------------
 * Pointer list operations
 * --------------------------------------------------------------- */

IsolPtrList *
isol_ptr_list_empty(IsolParseState *pstate)
{
    IsolPtrList *list = (IsolPtrList *)isol_alloc(pstate, sizeof(IsolPtrList));
    list->elements = NULL;
    list->nelements = 0;
    list->capacity = 0;
    return list;
}

IsolPtrList *
isol_ptr_list_create(IsolParseState *pstate, void *elem)
{
    IsolPtrList *list = (IsolPtrList *)isol_alloc(pstate, sizeof(IsolPtrList));
    list->capacity = 8;
    list->elements = (void **)isol_alloc(pstate, list->capacity * sizeof(void *));
    list->nelements = 1;
    list->elements[0] = elem;
    return list;
}

IsolPtrList *
isol_ptr_list_append(IsolParseState *pstate, IsolPtrList *list, void *elem)
{
    if (list->nelements >= list->capacity)
    {
        int new_cap = list->capacity * 2;
        if (new_cap < 8) new_cap = 8;
        void **new_elems = (void **)isol_alloc(pstate, new_cap * sizeof(void *));
        if (list->elements)
            memcpy(new_elems, list->elements, list->nelements * sizeof(void *));
        list->elements = new_elems;
        list->capacity = new_cap;
    }
    list->elements[list->nelements++] = elem;
    return list;
}

/* ---------------------------------------------------------------
 * AST node constructors
 * --------------------------------------------------------------- */

Session *
isol_make_session(IsolParseState *pstate, char *name, char *setupsql,
                  IsolPtrList *steps, char *teardownsql)
{
    Session *s = (Session *)isol_alloc(pstate, sizeof(Session));
    s->name = name;
    s->setupsql = setupsql;
    s->steps = (Step **)steps->elements;
    s->nsteps = steps->nelements;
    s->teardownsql = teardownsql;
    return s;
}

Step *
isol_make_step(IsolParseState *pstate, char *name, char *sql)
{
    Step *s = (Step *)isol_alloc(pstate, sizeof(Step));
    s->name = name;
    s->sql = sql;
    s->session = -1;
    s->used = false;
    return s;
}

Permutation *
isol_make_permutation(IsolParseState *pstate, IsolPtrList *steps)
{
    Permutation *p = (Permutation *)isol_alloc(pstate, sizeof(Permutation));
    p->nsteps = steps->nelements;
    p->steps = (PermutationStep **)steps->elements;
    return p;
}

PermutationStep *
isol_make_perm_step(IsolParseState *pstate, char *name, IsolPtrList *blockers)
{
    PermutationStep *ps = (PermutationStep *)isol_alloc(pstate,
                                                         sizeof(PermutationStep));
    ps->name = name;
    if (blockers)
    {
        ps->blockers = (PermutationStepBlocker **)blockers->elements;
        ps->nblockers = blockers->nelements;
    }
    else
    {
        ps->blockers = NULL;
        ps->nblockers = 0;
    }
    ps->step = NULL;
    return ps;
}

PermutationStepBlocker *
isol_make_blocker_step(IsolParseState *pstate, char *stepname)
{
    PermutationStepBlocker *b = (PermutationStepBlocker *)isol_alloc(pstate,
                                    sizeof(PermutationStepBlocker));
    b->stepname = stepname;
    b->blocktype = PSB_OTHER_STEP;
    b->num_notices = -1;
    b->step = NULL;
    b->target_notices = -1;
    return b;
}

PermutationStepBlocker *
isol_make_blocker_notices(IsolParseState *pstate, char *stepname,
                          int num_notices)
{
    PermutationStepBlocker *b = (PermutationStepBlocker *)isol_alloc(pstate,
                                    sizeof(PermutationStepBlocker));
    b->stepname = stepname;
    b->blocktype = PSB_NUM_NOTICES;
    b->num_notices = num_notices;
    b->step = NULL;
    b->target_notices = -1;
    return b;
}

PermutationStepBlocker *
isol_make_blocker_once(IsolParseState *pstate)
{
    PermutationStepBlocker *b = (PermutationStepBlocker *)isol_alloc(pstate,
                                    sizeof(PermutationStepBlocker));
    b->stepname = NULL;
    b->blocktype = PSB_ONCE;
    b->num_notices = -1;
    b->step = NULL;
    b->target_notices = -1;
    return b;
}

/* ---------------------------------------------------------------
 * Error handling
 * --------------------------------------------------------------- */

void
isol_yyerror(IsolParseState *pstate, const char *msg)
{
    if (pstate->error_count == 0)
    {
        snprintf(pstate->last_error, sizeof(pstate->last_error),
                 "%s at line %d", msg, pstate->lineno);
    }
    pstate->error_count++;
    fprintf(stderr, "ERROR: %s at line %d\n", msg, pstate->lineno);
}

/* ---------------------------------------------------------------
 * Parser state lifecycle
 * --------------------------------------------------------------- */

IsolParseState *
isol_parse_state_create(void)
{
    IsolParseState *pstate = calloc(1, sizeof(IsolParseState));
    if (!pstate)
    {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    pstate->lineno = 1;
    return pstate;
}

void
isol_parse_state_destroy(IsolParseState *pstate)
{
    if (!pstate) return;
    isol_free_allocs(pstate);
    free(pstate->allocs);
    free(pstate);
}

void
isol_parse_state_set_input(IsolParseState *pstate, const char *input,
                            int length)
{
    pstate->input = input;
    pstate->pos = 0;
    pstate->length = length;
    pstate->lineno = 1;
    pstate->error_count = 0;
    pstate->last_error[0] = '\0';
    memset(&pstate->result, 0, sizeof(TestSpec));
}

/* ---------------------------------------------------------------
 * Result display
 * --------------------------------------------------------------- */

void
isol_testspec_print(const TestSpec *spec, FILE *fp)
{
    int i, j;

    /* Setup blocks */
    for (i = 0; i < spec->nsetupsqls; i++)
        fprintf(fp, "SETUP { %s }\n", spec->setupsqls[i]);

    /* Teardown */
    if (spec->teardownsql)
        fprintf(fp, "TEARDOWN { %s }\n", spec->teardownsql);

    /* Sessions */
    for (i = 0; i < spec->nsessions; i++)
    {
        Session *s = spec->sessions[i];
        fprintf(fp, "SESSION %s", s->name);
        if (s->setupsql)
            fprintf(fp, " SETUP { %s }", s->setupsql);
        fprintf(fp, "\n");
        for (j = 0; j < s->nsteps; j++)
            fprintf(fp, "  STEP %s { %s }\n", s->steps[j]->name,
                    s->steps[j]->sql);
        if (s->teardownsql)
            fprintf(fp, "  TEARDOWN { %s }\n", s->teardownsql);
    }

    /* Permutations */
    for (i = 0; i < spec->npermutations; i++)
    {
        Permutation *p = spec->permutations[i];
        fprintf(fp, "PERMUTATION");
        for (j = 0; j < p->nsteps; j++)
        {
            PermutationStep *ps = p->steps[j];
            fprintf(fp, " %s", ps->name);
            if (ps->nblockers > 0)
            {
                int k;
                fprintf(fp, "(");
                for (k = 0; k < ps->nblockers; k++)
                {
                    PermutationStepBlocker *b = ps->blockers[k];
                    if (k > 0) fprintf(fp, ", ");
                    switch (b->blocktype)
                    {
                        case PSB_OTHER_STEP:
                            fprintf(fp, "%s", b->stepname);
                            break;
                        case PSB_NUM_NOTICES:
                            fprintf(fp, "%s notices %d",
                                    b->stepname, b->num_notices);
                            break;
                        case PSB_ONCE:
                            fprintf(fp, "*");
                            break;
                    }
                }
                fprintf(fp, ")");
            }
        }
        fprintf(fp, "\n");
    }
}
