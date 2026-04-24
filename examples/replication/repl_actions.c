/*-------------------------------------------------------------------------
 *
 * repl_actions.c
 *    Helper functions for the replication protocol parser (Lime version).
 *
 * Provides constructor helpers for building ReplCommand nodes and
 * option lists, plus parser state management.
 *
 * Portions Copyright (c) 1996-2026, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *-------------------------------------------------------------------------
 */
#include "repl_defs.h"

#include <stdarg.h>

/* ---------------------------------------------------------------
 * Parser state lifecycle
 * --------------------------------------------------------------- */

ReplParseState *
repl_parse_state_create(void)
{
    ReplParseState *pstate = (ReplParseState *)calloc(1, sizeof(ReplParseState));
    return pstate;
}

void
repl_parse_state_destroy(ReplParseState *pstate)
{
    if (!pstate)
        return;
    if (pstate->litbuf)
        free(pstate->litbuf);
    /* Note: result and its contents are not freed here -- caller owns them */
    free(pstate);
}

void
repl_parse_state_set_input(ReplParseState *pstate, const char *input, int length)
{
    pstate->input = input;
    pstate->pos = 0;
    pstate->length = length;
    pstate->error_count = 0;
    pstate->result = NULL;
}

/* ---------------------------------------------------------------
 * String helpers
 * --------------------------------------------------------------- */

char *
repl_pstrdup(ReplParseState *pstate, const char *s)
{
    (void)pstate;
    return strdup(s);
}

char *
repl_psprintf(ReplParseState *pstate, const char *fmt, ...)
{
    (void)pstate;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return strdup(buf);
}

/* ---------------------------------------------------------------
 * Command constructors
 * --------------------------------------------------------------- */

ReplCommand *
repl_make_command(ReplParseState *pstate, ReplCommandType type)
{
    (void)pstate;
    ReplCommand *cmd = (ReplCommand *)calloc(1, sizeof(ReplCommand));
    cmd->type = type;
    return cmd;
}

/* ---------------------------------------------------------------
 * DefElem constructors
 * --------------------------------------------------------------- */

ReplDefElem *
repl_make_defelem(ReplParseState *pstate, const char *name)
{
    (void)pstate;
    ReplDefElem *elem = (ReplDefElem *)calloc(1, sizeof(ReplDefElem));
    elem->name = strdup(name);
    elem->val_type = REPL_VAL_NONE;
    return elem;
}

ReplDefElem *
repl_make_defelem_str(ReplParseState *pstate, const char *name, const char *val)
{
    (void)pstate;
    ReplDefElem *elem = (ReplDefElem *)calloc(1, sizeof(ReplDefElem));
    elem->name = strdup(name);
    elem->val_type = REPL_VAL_STRING;
    elem->val.str = strdup(val);
    return elem;
}

ReplDefElem *
repl_make_defelem_int(ReplParseState *pstate, const char *name, int val)
{
    (void)pstate;
    ReplDefElem *elem = (ReplDefElem *)calloc(1, sizeof(ReplDefElem));
    elem->name = strdup(name);
    elem->val_type = REPL_VAL_INTEGER;
    elem->val.ival = val;
    return elem;
}

ReplDefElem *
repl_make_defelem_bool(ReplParseState *pstate, const char *name, bool val)
{
    (void)pstate;
    ReplDefElem *elem = (ReplDefElem *)calloc(1, sizeof(ReplDefElem));
    elem->name = strdup(name);
    elem->val_type = REPL_VAL_BOOLEAN;
    elem->val.bval = val;
    return elem;
}

/* ---------------------------------------------------------------
 * Option list helpers
 * --------------------------------------------------------------- */

ReplOptionList *
repl_option_list_create(ReplParseState *pstate, ReplDefElem *elem)
{
    (void)pstate;
    ReplOptionList *list = (ReplOptionList *)calloc(1, sizeof(ReplOptionList));
    list->capacity = 8;
    list->elems = (ReplDefElem **)calloc(list->capacity, sizeof(ReplDefElem *));
    list->elems[0] = elem;
    list->count = 1;
    return list;
}

ReplOptionList *
repl_option_list_append(ReplParseState *pstate, ReplOptionList *list, ReplDefElem *elem)
{
    (void)pstate;
    if (!list)
        return repl_option_list_create(pstate, elem);

    if (list->count >= list->capacity) {
        list->capacity *= 2;
        list->elems = (ReplDefElem **)realloc(list->elems,
                                               list->capacity * sizeof(ReplDefElem *));
    }
    list->elems[list->count++] = elem;
    return list;
}

/* ---------------------------------------------------------------
 * Error handling
 * --------------------------------------------------------------- */

void
repl_yyerror(ReplParseState *pstate, const char *msg)
{
    pstate->error_count++;
    snprintf(pstate->last_error, sizeof(pstate->last_error), "%s", msg);
    fprintf(stderr, "replication command parser: %s\n", msg);
}

/* ---------------------------------------------------------------
 * Free helpers
 * --------------------------------------------------------------- */

static void
repl_free_defelem(ReplDefElem *elem)
{
    if (!elem) return;
    free(elem->name);
    if (elem->val_type == REPL_VAL_STRING)
        free(elem->val.str);
    free(elem);
}

static void
repl_free_option_list(ReplOptionList *list)
{
    if (!list) return;
    for (int i = 0; i < list->count; i++)
        repl_free_defelem(list->elems[i]);
    free(list->elems);
    free(list);
}

void
repl_free_command(ReplCommand *cmd)
{
    if (!cmd) return;

    switch (cmd->type) {
        case REPL_CMD_BASE_BACKUP:
            repl_free_option_list(cmd->u.base_backup.options);
            break;
        case REPL_CMD_START_REPLICATION:
            free(cmd->u.start_repl.slotname);
            repl_free_option_list(cmd->u.start_repl.options);
            break;
        case REPL_CMD_CREATE_REPLICATION_SLOT:
            free(cmd->u.create_slot.slotname);
            free(cmd->u.create_slot.plugin);
            repl_free_option_list(cmd->u.create_slot.options);
            break;
        case REPL_CMD_DROP_REPLICATION_SLOT:
            free(cmd->u.drop_slot.slotname);
            break;
        case REPL_CMD_ALTER_REPLICATION_SLOT:
            free(cmd->u.alter_slot.slotname);
            repl_free_option_list(cmd->u.alter_slot.options);
            break;
        case REPL_CMD_READ_REPLICATION_SLOT:
            free(cmd->u.read_slot.slotname);
            break;
        case REPL_CMD_SHOW:
            free(cmd->u.show.varname);
            break;
        case REPL_CMD_IDENTIFY_SYSTEM:
        case REPL_CMD_TIMELINE_HISTORY:
        case REPL_CMD_UPLOAD_MANIFEST:
            break;
    }

    free(cmd);
}

/* ---------------------------------------------------------------
 * Debug/print helpers
 * --------------------------------------------------------------- */

static void
print_option_list(const ReplOptionList *list)
{
    if (!list || list->count == 0)
        return;

    printf("  options:\n");
    for (int i = 0; i < list->count; i++) {
        const ReplDefElem *e = list->elems[i];
        printf("    %s", e->name);
        switch (e->val_type) {
            case REPL_VAL_NONE:
                break;
            case REPL_VAL_STRING:
                printf(" = '%s'", e->val.str);
                break;
            case REPL_VAL_INTEGER:
                printf(" = %d", e->val.ival);
                break;
            case REPL_VAL_BOOLEAN:
                printf(" = %s", e->val.bval ? "true" : "false");
                break;
        }
        printf("\n");
    }
}

void
repl_print_command(const ReplCommand *cmd)
{
    if (!cmd) {
        printf("(null command)\n");
        return;
    }

    switch (cmd->type) {
        case REPL_CMD_IDENTIFY_SYSTEM:
            printf("IDENTIFY_SYSTEM\n");
            break;

        case REPL_CMD_BASE_BACKUP:
            printf("BASE_BACKUP\n");
            print_option_list(cmd->u.base_backup.options);
            break;

        case REPL_CMD_START_REPLICATION:
            if (cmd->u.start_repl.kind == REPLICATION_KIND_PHYSICAL) {
                printf("START_REPLICATION PHYSICAL\n");
                if (cmd->u.start_repl.slotname)
                    printf("  slot: %s\n", cmd->u.start_repl.slotname);
                printf("  startpoint: %X/%08X\n",
                       (uint32)(cmd->u.start_repl.startpoint >> 32),
                       (uint32)(cmd->u.start_repl.startpoint));
                if (cmd->u.start_repl.timeline > 0)
                    printf("  timeline: %u\n", cmd->u.start_repl.timeline);
            } else {
                printf("START_REPLICATION LOGICAL\n");
                if (cmd->u.start_repl.slotname)
                    printf("  slot: %s\n", cmd->u.start_repl.slotname);
                printf("  startpoint: %X/%08X\n",
                       (uint32)(cmd->u.start_repl.startpoint >> 32),
                       (uint32)(cmd->u.start_repl.startpoint));
                print_option_list(cmd->u.start_repl.options);
            }
            break;

        case REPL_CMD_CREATE_REPLICATION_SLOT:
            printf("CREATE_REPLICATION_SLOT %s %s%s\n",
                   cmd->u.create_slot.slotname ? cmd->u.create_slot.slotname : "(null)",
                   cmd->u.create_slot.kind == REPLICATION_KIND_PHYSICAL ? "PHYSICAL" : "LOGICAL",
                   cmd->u.create_slot.temporary ? " TEMPORARY" : "");
            if (cmd->u.create_slot.plugin)
                printf("  plugin: %s\n", cmd->u.create_slot.plugin);
            print_option_list(cmd->u.create_slot.options);
            break;

        case REPL_CMD_DROP_REPLICATION_SLOT:
            printf("DROP_REPLICATION_SLOT %s%s\n",
                   cmd->u.drop_slot.slotname ? cmd->u.drop_slot.slotname : "(null)",
                   cmd->u.drop_slot.wait ? " WAIT" : "");
            break;

        case REPL_CMD_ALTER_REPLICATION_SLOT:
            printf("ALTER_REPLICATION_SLOT %s\n",
                   cmd->u.alter_slot.slotname ? cmd->u.alter_slot.slotname : "(null)");
            print_option_list(cmd->u.alter_slot.options);
            break;

        case REPL_CMD_READ_REPLICATION_SLOT:
            printf("READ_REPLICATION_SLOT %s\n",
                   cmd->u.read_slot.slotname ? cmd->u.read_slot.slotname : "(null)");
            break;

        case REPL_CMD_TIMELINE_HISTORY:
            printf("TIMELINE_HISTORY %u\n", cmd->u.timeline_history.timeline);
            break;

        case REPL_CMD_SHOW:
            printf("SHOW %s\n",
                   cmd->u.show.varname ? cmd->u.show.varname : "(null)");
            break;

        case REPL_CMD_UPLOAD_MANIFEST:
            printf("UPLOAD_MANIFEST\n");
            break;
    }
}
