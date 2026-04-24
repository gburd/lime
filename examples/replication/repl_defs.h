/*-------------------------------------------------------------------------
 *
 * repl_defs.h
 *    Type definitions and function declarations for the replication
 *    protocol parser (Lime-generated).
 *
 * This header provides the types and interfaces needed by the Lime
 * grammar file (repl_gram.lime), the tokenizer (repl_tokenize.c),
 * and the standalone driver (main.c).
 *
 * Converted from: src/backend/replication/repl_gram.y
 *                 src/backend/replication/repl_scanner.l
 *
 * Portions Copyright (c) 1996-2026, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *-------------------------------------------------------------------------
 */
#ifndef REPL_DEFS_H
#define REPL_DEFS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

/*
 * When building standalone (outside PostgreSQL), define types that
 * would normally come from postgres.h.
 */
#ifndef REPL_INSIDE_PG

typedef unsigned int Oid;
typedef uint64_t XLogRecPtr;
typedef uint32_t uint32;

#define InvalidXLogRecPtr  ((XLogRecPtr) 0)

#endif /* REPL_INSIDE_PG */

/* ---------------------------------------------------------------
 * Replication command types (matching PostgreSQL replnodes.h)
 * --------------------------------------------------------------- */
typedef enum ReplCommandType {
    REPL_CMD_IDENTIFY_SYSTEM,
    REPL_CMD_BASE_BACKUP,
    REPL_CMD_START_REPLICATION,
    REPL_CMD_CREATE_REPLICATION_SLOT,
    REPL_CMD_DROP_REPLICATION_SLOT,
    REPL_CMD_ALTER_REPLICATION_SLOT,
    REPL_CMD_READ_REPLICATION_SLOT,
    REPL_CMD_TIMELINE_HISTORY,
    REPL_CMD_SHOW,
    REPL_CMD_UPLOAD_MANIFEST
} ReplCommandType;

typedef enum ReplicationKind {
    REPLICATION_KIND_PHYSICAL,
    REPLICATION_KIND_LOGICAL
} ReplicationKind;

/* ---------------------------------------------------------------
 * Generic option (name/value pair)
 * --------------------------------------------------------------- */
typedef struct ReplDefElem {
    char *name;
    enum { REPL_VAL_NONE, REPL_VAL_STRING, REPL_VAL_INTEGER, REPL_VAL_BOOLEAN } val_type;
    union {
        char   *str;
        int     ival;
        bool    bval;
    } val;
} ReplDefElem;

/* ---------------------------------------------------------------
 * Option list (simple growable array)
 * --------------------------------------------------------------- */
typedef struct ReplOptionList {
    ReplDefElem **elems;
    int           count;
    int           capacity;
} ReplOptionList;

/* ---------------------------------------------------------------
 * Replication command node (union of all command types)
 * --------------------------------------------------------------- */
typedef struct ReplCommand {
    ReplCommandType type;
    union {
        /* IDENTIFY_SYSTEM has no fields */

        /* BASE_BACKUP */
        struct {
            ReplOptionList *options;
        } base_backup;

        /* START_REPLICATION (physical) */
        struct {
            ReplicationKind kind;
            char           *slotname;
            XLogRecPtr      startpoint;
            uint32          timeline;
            ReplOptionList *options;     /* for logical replication */
        } start_repl;

        /* CREATE_REPLICATION_SLOT */
        struct {
            ReplicationKind kind;
            char           *slotname;
            char           *plugin;     /* for logical */
            bool            temporary;
            ReplOptionList *options;
        } create_slot;

        /* DROP_REPLICATION_SLOT */
        struct {
            char *slotname;
            bool  wait;
        } drop_slot;

        /* ALTER_REPLICATION_SLOT */
        struct {
            char           *slotname;
            ReplOptionList *options;
        } alter_slot;

        /* READ_REPLICATION_SLOT */
        struct {
            char *slotname;
        } read_slot;

        /* TIMELINE_HISTORY */
        struct {
            uint32 timeline;
        } timeline_history;

        /* SHOW */
        struct {
            char *varname;
        } show;

        /* UPLOAD_MANIFEST has no fields */
    } u;
} ReplCommand;

/* ---------------------------------------------------------------
 * Token value structure
 *
 * Each token carries one of these values depending on the token type.
 * --------------------------------------------------------------- */
typedef struct ReplToken {
    char       *str;        /* dynamically allocated string (IDENT, SCONST) */
    uint32      uintval;    /* unsigned integer (UCONST) */
    XLogRecPtr  recptr;     /* WAL location (RECPTR) */
} ReplToken;

/* ---------------------------------------------------------------
 * Parser state structure (passed as %extra_argument)
 * --------------------------------------------------------------- */
typedef struct ReplParseState {
    /* Input */
    const char  *input;
    int          pos;
    int          length;

    /* Scanner state for quoted strings */
    char        *litbuf;
    int          litbuf_len;
    int          litbuf_cap;

    /* Parse result */
    ReplCommand *result;

    /* Error state */
    int          error_count;
    char         last_error[256];
} ReplParseState;

/* ---------------------------------------------------------------
 * Constructor helpers
 * --------------------------------------------------------------- */
ReplCommand    *repl_make_command(ReplParseState *pstate, ReplCommandType type);
ReplDefElem    *repl_make_defelem(ReplParseState *pstate, const char *name);
ReplDefElem    *repl_make_defelem_str(ReplParseState *pstate, const char *name, const char *val);
ReplDefElem    *repl_make_defelem_int(ReplParseState *pstate, const char *name, int val);
ReplDefElem    *repl_make_defelem_bool(ReplParseState *pstate, const char *name, bool val);
ReplOptionList *repl_option_list_create(ReplParseState *pstate, ReplDefElem *elem);
ReplOptionList *repl_option_list_append(ReplParseState *pstate, ReplOptionList *list, ReplDefElem *elem);
char           *repl_pstrdup(ReplParseState *pstate, const char *s);
char           *repl_psprintf(ReplParseState *pstate, const char *fmt, ...);

/* ---------------------------------------------------------------
 * Tokenizer interface
 * --------------------------------------------------------------- */
int  repl_scan_next(ReplParseState *pstate, ReplToken *token_val);

/* ---------------------------------------------------------------
 * Error handling
 * --------------------------------------------------------------- */
void repl_yyerror(ReplParseState *pstate, const char *msg);

/* ---------------------------------------------------------------
 * Parser state lifecycle
 * --------------------------------------------------------------- */
ReplParseState *repl_parse_state_create(void);
void            repl_parse_state_destroy(ReplParseState *pstate);
void            repl_parse_state_set_input(ReplParseState *pstate,
                                           const char *input, int length);

/* ---------------------------------------------------------------
 * Debug/print helpers
 * --------------------------------------------------------------- */
void repl_print_command(const ReplCommand *cmd);

#endif /* REPL_DEFS_H */
