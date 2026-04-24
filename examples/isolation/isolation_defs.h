/*-------------------------------------------------------------------------
 *
 * isolation_defs.h
 *    Type definitions and function declarations for the isolation test
 *    spec parser (Lime-generated).
 *
 * Converted from: src/test/isolation/specparse.y
 *                 src/test/isolation/specscanner.l
 *
 * Portions Copyright (c) 1996-2026, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *-------------------------------------------------------------------------
 */
#ifndef ISOLATION_DEFS_H
#define ISOLATION_DEFS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

/* ---------------------------------------------------------------
 * Boolean type (standalone)
 * --------------------------------------------------------------- */
#ifndef false
#define false 0
#define true  1
#endif

/* ---------------------------------------------------------------
 * Blocker types (matching PostgreSQL's isolationtester.h)
 * --------------------------------------------------------------- */
typedef enum {
    PSB_OTHER_STEP = 0,     /* Wait for another named step */
    PSB_NUM_NOTICES,        /* Wait for a number of notices */
    PSB_ONCE                /* Wait once (wildcard *) */
} PermutationStepBlockerType;

/* ---------------------------------------------------------------
 * AST node types (matching PostgreSQL's isolationtester.h)
 * --------------------------------------------------------------- */

typedef struct PermutationStepBlocker {
    char       *stepname;       /* step to wait for (NULL for PSB_ONCE) */
    PermutationStepBlockerType blocktype;
    int         num_notices;    /* for PSB_NUM_NOTICES; -1 otherwise */
    void       *step;          /* resolved at runtime; NULL during parse */
    int         target_notices; /* resolved at runtime; -1 during parse */
} PermutationStepBlocker;

typedef struct PermutationStep {
    char       *name;           /* step name */
    PermutationStepBlocker **blockers;
    int         nblockers;
    void       *step;          /* resolved at runtime */
} PermutationStep;

typedef struct Permutation {
    int         nsteps;
    PermutationStep **steps;
} Permutation;

typedef struct Step {
    char       *name;
    char       *sql;
    int         session;        /* set to -1 until resolved */
    int         used;           /* boolean */
} Step;

typedef struct Session {
    char       *name;
    char       *setupsql;
    Step      **steps;
    int         nsteps;
    char       *teardownsql;
} Session;

typedef struct TestSpec {
    char      **setupsqls;
    int         nsetupsqls;
    char       *teardownsql;
    Session   **sessions;
    int         nsessions;
    Permutation **permutations;
    int         npermutations;
} TestSpec;

/* ---------------------------------------------------------------
 * Pointer list (for parser use -- replaces the Bison ptr_list union)
 * --------------------------------------------------------------- */
typedef struct IsolPtrList {
    void      **elements;
    int         nelements;
    int         capacity;
} IsolPtrList;

/* ---------------------------------------------------------------
 * Token value structure
 * --------------------------------------------------------------- */
typedef struct IsolToken {
    char *str;      /* string value (identifier, sqlblock) */
    int   ival;     /* integer value (INTEGER token) */
} IsolToken;

/* ---------------------------------------------------------------
 * Parser state
 * --------------------------------------------------------------- */
typedef struct IsolParseState {
    /* Input */
    const char *input;
    int         pos;
    int         length;
    int         lineno;

    /* Result */
    TestSpec    result;

    /* Error state */
    int         error_count;
    char        last_error[256];

    /* Memory tracking */
    char      **allocs;
    int         num_allocs;
    int         max_allocs;
} IsolParseState;

/* ---------------------------------------------------------------
 * Tokenizer interface
 * --------------------------------------------------------------- */
int isol_scan_next(IsolParseState *pstate, IsolToken *tok);

/* ---------------------------------------------------------------
 * Memory management
 * --------------------------------------------------------------- */
char *isol_alloc(IsolParseState *pstate, size_t size);
char *isol_pstrdup(IsolParseState *pstate, const char *s);
void  isol_free_allocs(IsolParseState *pstate);

/* ---------------------------------------------------------------
 * Pointer list operations
 * --------------------------------------------------------------- */
IsolPtrList *isol_ptr_list_empty(IsolParseState *pstate);
IsolPtrList *isol_ptr_list_create(IsolParseState *pstate, void *elem);
IsolPtrList *isol_ptr_list_append(IsolParseState *pstate,
                                   IsolPtrList *list, void *elem);

/* ---------------------------------------------------------------
 * AST node constructors
 * --------------------------------------------------------------- */
Session           *isol_make_session(IsolParseState *pstate, char *name,
                                     char *setupsql, IsolPtrList *steps,
                                     char *teardownsql);
Step              *isol_make_step(IsolParseState *pstate, char *name,
                                  char *sql);
Permutation       *isol_make_permutation(IsolParseState *pstate,
                                          IsolPtrList *steps);
PermutationStep   *isol_make_perm_step(IsolParseState *pstate, char *name,
                                        IsolPtrList *blockers);
PermutationStepBlocker *isol_make_blocker_step(IsolParseState *pstate,
                                                char *stepname);
PermutationStepBlocker *isol_make_blocker_notices(IsolParseState *pstate,
                                                   char *stepname,
                                                   int num_notices);
PermutationStepBlocker *isol_make_blocker_once(IsolParseState *pstate);

/* ---------------------------------------------------------------
 * Error handling
 * --------------------------------------------------------------- */
void isol_yyerror(IsolParseState *pstate, const char *msg);

/* ---------------------------------------------------------------
 * Parser state lifecycle
 * --------------------------------------------------------------- */
IsolParseState *isol_parse_state_create(void);
void            isol_parse_state_destroy(IsolParseState *pstate);
void            isol_parse_state_set_input(IsolParseState *pstate,
                                            const char *input, int length);

/* ---------------------------------------------------------------
 * Result display
 * --------------------------------------------------------------- */
void isol_testspec_print(const TestSpec *spec, FILE *fp);

#endif /* ISOLATION_DEFS_H */
