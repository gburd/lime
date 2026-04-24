/*-------------------------------------------------------------------------
 *
 * syncrep_defs.h
 *    Type definitions and function declarations for the synchronous
 *    replication config parser (Lime-generated).
 *
 * Converted from: src/backend/replication/syncrep_gram.y
 *                 src/backend/replication/syncrep_scanner.l
 *
 * Portions Copyright (c) 1996-2026, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *-------------------------------------------------------------------------
 */
#ifndef SYNCREP_DEFS_H
#define SYNCREP_DEFS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

/* ---------------------------------------------------------------
 * Synchronous replication method constants
 * (matching PostgreSQL's replication/syncrep.h)
 * --------------------------------------------------------------- */
#define SYNC_REP_PRIORITY  0
#define SYNC_REP_QUORUM    1

/* ---------------------------------------------------------------
 * Token value structure
 * --------------------------------------------------------------- */
typedef struct SyncRepToken {
    char *str;     /* string value (for NAME and NUM tokens) */
} SyncRepToken;

/* ---------------------------------------------------------------
 * Name list (growable array of standby names)
 * --------------------------------------------------------------- */
typedef struct SyncRepNameList {
    char **names;
    int    count;
    int    capacity;
} SyncRepNameList;

/* ---------------------------------------------------------------
 * Parsed synchronous replication configuration
 *
 * This is the standalone equivalent of PostgreSQL's SyncRepConfigData.
 * The original uses a flat representation with member_names as a
 * variable-length array of null-terminated strings. We use a simpler
 * pointer-based list for the standalone version.
 * --------------------------------------------------------------- */
typedef struct SyncRepConfig {
    int    num_sync;       /* number of synchronous standbys required */
    int    syncrep_method; /* SYNC_REP_PRIORITY or SYNC_REP_QUORUM */
    int    nmembers;       /* number of member names */
    char **members;        /* array of member name strings */
} SyncRepConfig;

/* ---------------------------------------------------------------
 * Parser state (passed as %extra_argument)
 * --------------------------------------------------------------- */
typedef struct SyncRepParseState {
    /* Input */
    const char *input;
    int         pos;
    int         length;

    /* Result */
    SyncRepConfig *result;

    /* Error state */
    int    error_count;
    char   last_error[256];

    /* Memory tracking */
    char **allocs;
    int    num_allocs;
    int    max_allocs;
} SyncRepParseState;

/* ---------------------------------------------------------------
 * Tokenizer interface
 * --------------------------------------------------------------- */
int syncrep_scan_next(SyncRepParseState *pstate, SyncRepToken *tok);

/* ---------------------------------------------------------------
 * Semantic action helpers
 * --------------------------------------------------------------- */

/* Memory management */
char *syncrep_alloc(SyncRepParseState *pstate, size_t size);
char *syncrep_pstrdup(SyncRepParseState *pstate, const char *s);
void  syncrep_free_allocs(SyncRepParseState *pstate);

/* Config construction */
SyncRepConfig    *syncrep_create_config(SyncRepParseState *pstate,
                                        const char *num_sync,
                                        SyncRepNameList *members,
                                        int syncrep_method);

/* Name list operations */
SyncRepNameList  *syncrep_name_list_create(SyncRepParseState *pstate,
                                           const char *name);
SyncRepNameList  *syncrep_name_list_append(SyncRepParseState *pstate,
                                           SyncRepNameList *list,
                                           const char *name);

/* Error handling */
void syncrep_yyerror(SyncRepParseState *pstate, const char *msg);

/* Parser state lifecycle */
SyncRepParseState *syncrep_parse_state_create(void);
void               syncrep_parse_state_destroy(SyncRepParseState *pstate);
void               syncrep_parse_state_set_input(SyncRepParseState *pstate,
                                                  const char *input,
                                                  int length);

/* Config display/free */
void syncrep_config_print(const SyncRepConfig *config, FILE *fp);
void syncrep_config_free(SyncRepConfig *config);

#endif /* SYNCREP_DEFS_H */
