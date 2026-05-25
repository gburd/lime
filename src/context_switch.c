/*
** context_switch.c -- runtime grammar-mode boundary detection.
**
** This module sits above grammar_context.c and provides the
** registration / classification / fast-path entry points that host
** parsers use to spot embedded-grammar boundaries in their token
** stream.
**
** As-was (commit 1506723^), this file baked in four trigger lexemes
** (`xmlquery`, `xpath`, `{:`, `json`) as `static const char *`
** globals.  That was wrong: real SQL doesn't say `xmlquery(...)` and
** PostgreSQL uses `XMLPARSE(DOCUMENT ...)`, so the trigger set must
** be runtime-configurable per host grammar.  This restoration drops
** the four globals and the `context_switch_register_defaults`
** convenience function in favour of a single
** `context_switch_register_trigger` registration entry point.
**
** Performance contract: when no triggers are registered (the common
** case in single-grammar host parsers), context_switch_needed()
** returns false after a NULL/zero-count check.  parse_engine.c's
** hook bails on that check so single-grammar parsing pays nothing
** for the extension machinery.
*/
#include "grammar_context.h"
#include "snapshot.h"

#include <stdlib.h>
#include <string.h>

/*
** Range we use for runtime-assigned mode ids.  MODE_CUSTOM (5) is
** reserved for "user-defined / extension grammar" by the enum;
** runtime-registered triggers get ids starting one past it so they
** don't collide with the built-in slots if a future grammar wants to
** mix register_mode() and register_trigger() calls on the same stack.
*/
#define CTX_RUNTIME_MODE_BASE ((int)MODE_CUSTOM + 1)

/* ------------------------------------------------------------------ */
/*  Registration                                                       */
/* ------------------------------------------------------------------ */

bool context_switch_register_trigger(GrammarContextStack *stack, const char *trigger_lexeme,
                                     ParserSnapshot *embedded_snap, const char *mode_name) {
    if (stack == NULL || trigger_lexeme == NULL || embedded_snap == NULL || mode_name == NULL) {
        return false;
    }
    if (trigger_lexeme[0] == '\0') return false;

    /* Reject duplicate trigger lexemes.  Two triggers with the same
    ** prefix would make context_switch_classify_lexeme() ambiguous. */
    uint32_t n = grammar_context_mode_count(stack);
    for (uint32_t i = 0; i < n; i++) {
        const GrammarModeInfo *m = grammar_context_mode_at(stack, i);
        if (m != NULL && m->trigger_lexeme != NULL &&
            strcmp(m->trigger_lexeme, trigger_lexeme) == 0) {
            return false;
        }
    }

    /* Assign a fresh mode id.  Use the count of already-registered
    ** modes as the offset into the runtime-mode range so registrations
    ** don't collide with built-in MODE_XQUERY..MODE_CUSTOM slots. */
    GrammarMode new_mode = (GrammarMode)(CTX_RUNTIME_MODE_BASE + (int)n);

    GrammarModeInfo info = {
        .mode = new_mode,
        .name = mode_name,
        .snapshot = embedded_snap,
        .trigger_token = -1,
        .trigger_lexeme = trigger_lexeme,
        .exit_token = -1, /* exit on bracket depth */
    };
    return grammar_context_register_mode(stack, &info);
}

/* ------------------------------------------------------------------ */
/*  Classification                                                     */
/* ------------------------------------------------------------------ */

GrammarMode context_switch_classify_lexeme(const GrammarContextStack *stack, const char *lexeme) {
    if (stack == NULL || lexeme == NULL) return MODE_NONE;
    if (lexeme[0] == '\0') return MODE_NONE;

    uint32_t n = grammar_context_mode_count(stack);
    for (uint32_t i = 0; i < n; i++) {
        const GrammarModeInfo *m = grammar_context_mode_at(stack, i);
        if (m == NULL || m->trigger_lexeme == NULL) continue;
        size_t tlen = strlen(m->trigger_lexeme);
        if (tlen == 0) continue;
        if (strncmp(lexeme, m->trigger_lexeme, tlen) == 0) {
            return m->mode;
        }
    }
    return MODE_NONE;
}

/* ------------------------------------------------------------------ */
/*  Fast-path predicate                                                */
/* ------------------------------------------------------------------ */

bool context_switch_needed(const GrammarContextStack *stack, int token_code, const char *lexeme) {
    (void)token_code;

    if (stack == NULL) return false;

    /* Fast path: zero registered triggers means the parser is a pure
    ** single-grammar parser and the entire embedded-grammar pipeline
    ** is dormant.  Single load + branch on the parse-engine hot path. */
    if (grammar_context_mode_count(stack) == 0) return false;

    if (grammar_context_is_root_only(stack)) {
        /* At the root: a switch can only fire on a non-empty lexeme. */
        if (lexeme == NULL || lexeme[0] == '\0') return false;
        return context_switch_classify_lexeme(stack, lexeme) != MODE_NONE;
    }

    /* Inside an embedded context: the caller may want to poll for an
    ** exit token, so we always return true and let detect_exit /
    ** grammar_context_close_bracket() decide. */
    return true;
}

/* ------------------------------------------------------------------ */
/*  Exit detection                                                     */
/* ------------------------------------------------------------------ */

bool context_switch_detect_exit(GrammarContextStack *stack, int token_code, const char *lexeme) {
    (void)lexeme; /* Reserved for future lexeme-based exit detection. */

    if (stack == NULL) return false;
    if (grammar_context_is_root_only(stack)) return false;

    /* Delegate to grammar_context.c's explicit-exit-token logic.  The
    ** bracket-depth exit path is handled by
    ** grammar_context_close_bracket(). */
    return grammar_context_detect_exit(stack, token_code);
}
