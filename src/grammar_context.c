/*
** Grammar context stack implementation.
**
** Manages a stack of grammar contexts for switching between embedded
** language grammars.  Each stack entry holds a snapshot reference and
** mode metadata.  The stack starts with a root entry (MODE_SQL) and
** grows as embedded language regions are entered.
**
** Mode detection is driven by registered GrammarModeInfo entries that
** describe trigger tokens/lexemes and exit conditions.
*/
#include "grammar_context.h"
#include "snapshot.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Internal constants                                                 */
/* ------------------------------------------------------------------ */

#define MAX_CONTEXT_DEPTH 32 /* Maximum nesting depth               */
#define MAX_MODES 16         /* Maximum registered grammar modes     */

/* ------------------------------------------------------------------ */
/*  Context stack structure                                            */
/* ------------------------------------------------------------------ */

struct GrammarContextStack {
    /* Stack of context entries.  entries[0] is always the root. */
    GrammarContextEntry entries[MAX_CONTEXT_DEPTH];
    uint32_t depth; /* Current stack depth (1-based count) */

    /* Registered grammar modes */
    GrammarModeInfo modes[MAX_MODES];
    uint32_t nmode;

    /* Bracket depth tracking for the current context */
    int bracket_depth;

    /* Switch callback */
    ContextSwitchCallback switch_cb;
    void *switch_cb_data;
};

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                   */
/* ------------------------------------------------------------------ */

static const GrammarModeInfo *find_mode_info(const GrammarContextStack *stack, GrammarMode mode) {
    for (uint32_t i = 0; i < stack->nmode; i++) {
        if (stack->modes[i].mode == mode) {
            return &stack->modes[i];
        }
    }
    return NULL;
}

static const GrammarModeInfo *find_mode_by_token(const GrammarContextStack *stack, int token_code) {
    for (uint32_t i = 0; i < stack->nmode; i++) {
        if (stack->modes[i].trigger_token >= 0 && stack->modes[i].trigger_token == token_code) {
            return &stack->modes[i];
        }
    }
    return NULL;
}

static const GrammarModeInfo *find_mode_by_lexeme(const GrammarContextStack *stack,
                                                  const char *lexeme) {
    if (lexeme == NULL) return NULL;
    for (uint32_t i = 0; i < stack->nmode; i++) {
        if (stack->modes[i].trigger_lexeme != NULL) {
            size_t tlen = strlen(stack->modes[i].trigger_lexeme);
            if (strncmp(lexeme, stack->modes[i].trigger_lexeme, tlen) == 0) {
                return &stack->modes[i];
            }
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Create / destroy                                                   */
/* ------------------------------------------------------------------ */

GrammarContextStack *grammar_context_create(ParserSnapshot *root_snapshot) {
    if (root_snapshot == NULL) return NULL;

    GrammarContextStack *stack = calloc(1, sizeof(GrammarContextStack));
    if (stack == NULL) return NULL;

    /* Initialize root entry */
    stack->entries[0].mode = MODE_SQL;
    stack->entries[0].mode_name = "sql";
    stack->entries[0].snapshot = snapshot_acquire(root_snapshot);
    stack->entries[0].depth = 0;
    stack->entries[0].start_offset = 0;
    stack->depth = 1;
    stack->bracket_depth = 0;

    return stack;
}

void grammar_context_destroy(GrammarContextStack *stack) {
    if (stack == NULL) return;

    /* Release snapshot references for all stack entries */
    for (uint32_t i = 0; i < stack->depth; i++) {
        if (stack->entries[i].snapshot != NULL) {
            snapshot_release(stack->entries[i].snapshot);
        }
    }

    /* Release snapshot references for registered modes */
    for (uint32_t i = 0; i < stack->nmode; i++) {
        if (stack->modes[i].snapshot != NULL) {
            snapshot_release(stack->modes[i].snapshot);
        }
    }

    free(stack);
}

/* ------------------------------------------------------------------ */
/*  Mode registration                                                  */
/* ------------------------------------------------------------------ */

bool grammar_context_register_mode(GrammarContextStack *stack, const GrammarModeInfo *info) {
    if (stack == NULL || info == NULL) return false;
    if (stack->nmode >= MAX_MODES) return false;
    if (info->snapshot == NULL) return false;

    /* Check for duplicate mode */
    if (find_mode_info(stack, info->mode) != NULL) return false;

    GrammarModeInfo *slot = &stack->modes[stack->nmode];
    *slot = *info;
    slot->snapshot = snapshot_acquire(info->snapshot);
    stack->nmode++;
    return true;
}

/* ------------------------------------------------------------------ */
/*  Context detection                                                  */
/* ------------------------------------------------------------------ */

bool grammar_context_detect_switch(GrammarContextStack *stack, int token_code, const char *lexeme,
                                   uint32_t offset) {
    if (stack == NULL) return false;

    /* Try token-based detection first */
    const GrammarModeInfo *mode = find_mode_by_token(stack, token_code);

    /* Then try lexeme-based detection */
    if (mode == NULL) {
        mode = find_mode_by_lexeme(stack, lexeme);
    }

    if (mode == NULL) return false;

    /* Push the new context */
    return grammar_context_push(stack, mode->mode, offset);
}

bool grammar_context_detect_exit(GrammarContextStack *stack, int token_code) {
    if (stack == NULL || stack->depth <= 1) return false;

    GrammarContextEntry *top = &stack->entries[stack->depth - 1];
    const GrammarModeInfo *mode = find_mode_info(stack, top->mode);
    if (mode == NULL) return false;

    /* Check explicit exit token */
    if (mode->exit_token >= 0 && mode->exit_token == token_code) {
        return grammar_context_pop(stack);
    }

    return false;
}

/* ------------------------------------------------------------------ */
/*  Explicit push / pop                                                */
/* ------------------------------------------------------------------ */

bool grammar_context_push(GrammarContextStack *stack, GrammarMode mode, uint32_t offset) {
    if (stack == NULL) return false;
    if (stack->depth >= MAX_CONTEXT_DEPTH) return false;

    const GrammarModeInfo *info = find_mode_info(stack, mode);
    if (info == NULL) return false;

    /* Invoke callback */
    GrammarMode prev = stack->entries[stack->depth - 1].mode;
    if (stack->switch_cb != NULL) {
        if (!stack->switch_cb(prev, mode, stack->switch_cb_data)) {
            return false; /* Callback vetoed the switch */
        }
    }

    GrammarContextEntry *entry = &stack->entries[stack->depth];
    entry->mode = mode;
    entry->mode_name = info->name;
    entry->snapshot = snapshot_acquire(info->snapshot);
    entry->depth = stack->bracket_depth;
    entry->start_offset = offset;

    stack->depth++;
    return true;
}

bool grammar_context_pop(GrammarContextStack *stack) {
    if (stack == NULL || stack->depth <= 1) return false;

    uint32_t top_idx = stack->depth - 1;
    GrammarContextEntry *top = &stack->entries[top_idx];

    /* Invoke callback */
    GrammarMode leaving = top->mode;
    GrammarMode returning = stack->entries[top_idx - 1].mode;
    if (stack->switch_cb != NULL) {
        /* Pop callbacks are informational (not vetoable) -- call anyway */
        stack->switch_cb(leaving, returning, stack->switch_cb_data);
    }

    /* Release snapshot */
    if (top->snapshot != NULL) {
        snapshot_release(top->snapshot);
    }
    memset(top, 0, sizeof(*top));

    stack->depth--;
    return true;
}

/* ------------------------------------------------------------------ */
/*  Queries                                                            */
/* ------------------------------------------------------------------ */

ParserSnapshot *grammar_context_current_snapshot(const GrammarContextStack *stack) {
    if (stack == NULL || stack->depth == 0) return NULL;
    return stack->entries[stack->depth - 1].snapshot;
}

GrammarMode grammar_context_current_mode(const GrammarContextStack *stack) {
    if (stack == NULL || stack->depth == 0) return MODE_SQL;
    return stack->entries[stack->depth - 1].mode;
}

uint32_t grammar_context_depth(const GrammarContextStack *stack) {
    if (stack == NULL) return 0;
    return stack->depth - 1; /* 0 = root only */
}

bool grammar_context_is_root_only(const GrammarContextStack *stack) {
    if (stack == NULL) return true;
    return stack->depth <= 1;
}

/* ------------------------------------------------------------------ */
/*  Switch callback                                                    */
/* ------------------------------------------------------------------ */

void grammar_context_set_switch_callback(GrammarContextStack *stack, ContextSwitchCallback cb,
                                         void *user_data) {
    if (stack == NULL) return;
    stack->switch_cb = cb;
    stack->switch_cb_data = user_data;
}

/* ------------------------------------------------------------------ */
/*  Bracket tracking                                                   */
/* ------------------------------------------------------------------ */

void grammar_context_open_bracket(GrammarContextStack *stack) {
    if (stack == NULL) return;
    stack->bracket_depth++;
}

bool grammar_context_close_bracket(GrammarContextStack *stack) {
    if (stack == NULL) return false;

    stack->bracket_depth--;

    /* Check if closing bracket returns to the entry depth of the
    ** current context, triggering an automatic pop. */
    if (stack->depth > 1) {
        GrammarContextEntry *top = &stack->entries[stack->depth - 1];
        const GrammarModeInfo *info = find_mode_info(stack, top->mode);

        /* Auto-exit when exit_token is -1 (bracket-depth based exit)
        ** and bracket depth drops below the entry depth.  The entry
        ** depth records the bracket_depth at push time; when the
        ** matching close bracket is consumed, bracket_depth will go
        ** below that level. */
        if (info != NULL && info->exit_token == -1 && stack->bracket_depth < top->depth) {
            return grammar_context_pop(stack);
        }
    }

    return false;
}
