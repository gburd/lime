/*
** Context switch - Grammar switching heuristics and fast-path logic.
**
** This module provides the detection layer that identifies language
** boundaries in mixed-language input streams.  It works with the
** grammar context stack to push/pop grammar modes.
**
** Supported boundary patterns:
**   SQL -> XQuery:  xmlquery( ... )
**   SQL -> XPath:   xpath( ... )
**   SQL -> EDN:     {: ... :}
**   SQL -> JSON:    json '...' or json"..."
**
** The fast-path check (context_switch_needed()) allows the parser to
** skip all detection logic when only the root grammar is active and
** the current token is not a potential mode trigger.
*/
#include "grammar_context.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ------------------------------------------------------------------ */
/*  Well-known trigger lexemes                                         */
/* ------------------------------------------------------------------ */

static const char *TRIGGER_XQUERY = "xmlquery";
static const char *TRIGGER_XPATH = "xpath";
static const char *TRIGGER_EDN = "{:";
static const char *TRIGGER_JSON = "json";

/* ------------------------------------------------------------------ */
/*  Lexeme classification                                              */
/* ------------------------------------------------------------------ */

/*
** Classify a lexeme as a potential language boundary trigger.
** Returns the grammar mode it would trigger, or MODE_SQL if no
** boundary is detected.
*/
GrammarMode context_switch_classify_lexeme(const char *lexeme) {
    if (lexeme == NULL) return MODE_SQL;

    size_t len = strlen(lexeme);
    if (len == 0) return MODE_SQL;

    /* Case-insensitive comparison for keyword triggers */
    if (len >= 8) {
        /* Check for "xmlquery" (case-insensitive) */
        char lower[9];
        for (int i = 0; i < 8; i++) {
            lower[i] = (char)tolower((unsigned char)lexeme[i]);
        }
        lower[8] = '\0';
        if (strcmp(lower, TRIGGER_XQUERY) == 0) {
            return MODE_XQUERY;
        }
    }

    if (len >= 5) {
        /* Check for "xpath" (case-insensitive) */
        char lower[6];
        for (int i = 0; i < 5; i++) {
            lower[i] = (char)tolower((unsigned char)lexeme[i]);
        }
        lower[5] = '\0';
        if (strcmp(lower, TRIGGER_XPATH) == 0) {
            return MODE_XPATH;
        }
    }

    if (len >= 4) {
        /* Check for "json" (case-insensitive) */
        char lower[5];
        for (int i = 0; i < 4; i++) {
            lower[i] = (char)tolower((unsigned char)lexeme[i]);
        }
        lower[4] = '\0';
        if (strcmp(lower, TRIGGER_JSON) == 0) {
            return MODE_JSON;
        }
    }

    /* Exact match for EDN trigger */
    if (len >= 2 && lexeme[0] == '{' && lexeme[1] == ':') {
        return MODE_EDN;
    }

    return MODE_SQL;
}

/*
** Quick check: could this token potentially trigger a context switch?
** This is the fast-path predicate.  If it returns false, no further
** mode-detection work is needed.
**
**   token_code - the token just lexed
**   lexeme     - the lexeme text (may be NULL)
**
** Returns true if the token MIGHT be a language boundary trigger.
** False positives are acceptable; false negatives are not.
*/
bool context_switch_needed(const GrammarContextStack *stack, int token_code, const char *lexeme) {
    (void)token_code;

    if (stack == NULL) return false;

    /* Fast path: if we're at the root and the lexeme doesn't look like
    ** any trigger, skip detection entirely. */
    if (grammar_context_is_root_only(stack)) {
        if (lexeme == NULL) return false;

        /* Quick first-character check to avoid string comparisons */
        char c = (char)tolower((unsigned char)lexeme[0]);
        switch (c) {
        case 'x': /* xmlquery, xpath */
        case 'j': /* json */
        case '{': /* {: (EDN) */
            return true;
        default:
            return false;
        }
    }

    /* When we're inside an embedded context, always check for exit */
    return true;
}

/*
** Detect exit patterns for embedded language regions.
** Returns the number of bracket levels to close, or 0 if no exit
** pattern is detected.
**
** Exit patterns:
**   XQuery / XPath: closing ')' that matches the xmlquery( / xpath(
**   EDN: ':}' sequence
*/
int context_switch_detect_exit(const char *lexeme) {
    if (lexeme == NULL) return 0;

    /* EDN close marker */
    if (strlen(lexeme) >= 2 && lexeme[0] == ':' && lexeme[1] == '}') {
        return 1;
    }

    return 0;
}

/*
** Register the standard built-in mode triggers with a context stack.
** This is a convenience function that sets up the well-known triggers
** for SQL -> XQuery, SQL -> XPath, SQL -> EDN, and SQL -> JSON.
**
** The caller must provide snapshots for each mode they want to enable;
** NULL snapshots are skipped.
*/
bool context_switch_register_defaults(GrammarContextStack *stack, ParserSnapshot *xquery_snapshot,
                                      ParserSnapshot *xpath_snapshot, ParserSnapshot *edn_snapshot,
                                      ParserSnapshot *json_snapshot) {
    if (stack == NULL) return false;

    bool ok = true;

    if (xquery_snapshot != NULL) {
        GrammarModeInfo info = {
            .mode = MODE_XQUERY,
            .name = "xquery",
            .snapshot = xquery_snapshot,
            .trigger_token = -1,
            .trigger_lexeme = TRIGGER_XQUERY,
            .exit_token = -1, /* exit on bracket depth */
        };
        ok = ok && grammar_context_register_mode(stack, &info);
    }

    if (xpath_snapshot != NULL) {
        GrammarModeInfo info = {
            .mode = MODE_XPATH,
            .name = "xpath",
            .snapshot = xpath_snapshot,
            .trigger_token = -1,
            .trigger_lexeme = TRIGGER_XPATH,
            .exit_token = -1, /* exit on bracket depth */
        };
        ok = ok && grammar_context_register_mode(stack, &info);
    }

    if (edn_snapshot != NULL) {
        GrammarModeInfo info = {
            .mode = MODE_EDN,
            .name = "edn",
            .snapshot = edn_snapshot,
            .trigger_token = -1,
            .trigger_lexeme = TRIGGER_EDN,
            .exit_token = -1, /* exit on bracket depth */
        };
        ok = ok && grammar_context_register_mode(stack, &info);
    }

    if (json_snapshot != NULL) {
        GrammarModeInfo info = {
            .mode = MODE_JSON,
            .name = "json",
            .snapshot = json_snapshot,
            .trigger_token = -1,
            .trigger_lexeme = TRIGGER_JSON,
            .exit_token = -1,
        };
        ok = ok && grammar_context_register_mode(stack, &info);
    }

    return ok;
}
