/*
** multi_driver.c -- driver for the SQL+JSON multi-grammar example.
**
** Demonstrates Lime's runtime-registered context-switch trigger
** registry by parsing a SELECT statement that contains an embedded
** `json '{...}'` literal.
**
** Architecture:
**   - The host (SQL) is recognised by a tiny hand-rolled state
**     machine in this file.  The point of the example is the
**     context-switch boundary detection, not SQL parsing per se;
**     substitute any Lime-generated parser to plug it in.
**   - The embedded language (JSON) is parsed by the Lime-generated
**     parser from examples/json/json_grammar.lime.
**   - A GrammarContextStack holds the registered triggers.  When the
**     SQL tokenizer sees the lexeme `json`, it consults
**     context_switch_classify_lexeme() and -- on a match -- pauses
**     SQL tokenisation, scans the following `'...'` body as JSON, and
**     drives the JSON parser to completion.  The resulting
**     JsonValue * is attached to the SQL AST as a JSON_LITERAL
**     column.  Tokenisation then resumes in SQL mode.
**
** The example registers exactly one trigger ("json" -> JSON
** snapshot) but the registry handles N triggers; multiple
** dialects (XPath, MongoDB query docs, ...) would register
** alongside.
*/
#include "multi.h"

#include "grammar_context.h"
#include "snapshot.h"
#include "snapshot_build.h"

/* JSON parser -- generated from ../json/json_grammar.lime */
#include "json.h"
#include "json_grammar.h"
#include "json_tokenize.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void *JsonAlloc(void *(*)(size_t));
extern void  JsonFree(void *, void (*)(void *));
extern void  Json(void *, int, void *, JsonValue **);

/* ------------------------------------------------------------------ */
/*  Mock embedded snapshot                                             */
/*                                                                      */
/*  In a real integration the embedded snapshot would be built from    */
/*  the JSON grammar's tables (snapshot_build_from_tables).  For this  */
/*  example we don't drive Lime's runtime push parser on the embedded  */
/*  side -- we drive the generator's typed Json() entry directly so    */
/*  we get a JsonValue * AST.  The snapshot is therefore only used as  */
/*  an identity tag for the trigger registry.                          */
/* ------------------------------------------------------------------ */

#include <stdatomic.h>

static ParserSnapshot *make_tag_snapshot(uint64_t version) {
    ParserSnapshot *s = calloc(1, sizeof(ParserSnapshot));
    if (s == NULL) return NULL;
    atomic_init(&s->refcount, 1);
    s->version = version;
    return s;
}

/* ------------------------------------------------------------------ */
/*  SQL tokenizer + recogniser                                         */
/* ------------------------------------------------------------------ */

typedef struct SqlLex {
    const char *cursor;
    const char *end;
    int line;
    int col;
} SqlLex;

static void sql_lex_init(SqlLex *L, const char *input) {
    L->cursor = input;
    L->end = input + strlen(input);
    L->line = 1;
    L->col = 1;
}

static int sql_peek(const SqlLex *L) {
    return L->cursor < L->end ? (unsigned char)*L->cursor : -1;
}

static int sql_consume(SqlLex *L) {
    if (L->cursor >= L->end) return -1;
    int c = (unsigned char)*L->cursor++;
    if (c == '\n') {
        L->line++;
        L->col = 1;
    } else {
        L->col++;
    }
    return c;
}

static void sql_skip_ws(SqlLex *L) {
    while (L->cursor < L->end) {
        int c = sql_peek(L);
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            sql_consume(L);
        else
            break;
    }
}

/* Read an identifier-or-keyword lexeme into out_buf (caller-sized). */
static size_t sql_read_word(SqlLex *L, char *out_buf, size_t cap) {
    size_t n = 0;
    while (L->cursor < L->end && (isalnum((unsigned char)*L->cursor) || *L->cursor == '_')) {
        if (n + 1 < cap) out_buf[n++] = *L->cursor;
        L->cursor++;
        L->col++;
    }
    if (n < cap) out_buf[n] = '\0';
    else out_buf[cap - 1] = '\0';
    return n;
}

/* Read a JSON body delimited by the leading `'` already consumed.
** Stops at the closing `'`.  Output is heap-allocated, NUL-terminated. */
static char *sql_read_json_body(SqlLex *L, MultiParseStatus *err) {
    /* The opening quote was consumed by the caller. */
    const char *start = L->cursor;
    while (L->cursor < L->end && *L->cursor != '\'') {
        if (*L->cursor == '\n') {
            L->line++;
            L->col = 1;
        } else {
            L->col++;
        }
        L->cursor++;
    }
    if (L->cursor >= L->end) {
        *err = MULTI_LEX_ERROR;
        return NULL; /* unterminated */
    }
    size_t len = (size_t)(L->cursor - start);
    char *body = malloc(len + 1);
    if (body == NULL) {
        *err = MULTI_LEX_ERROR;
        return NULL;
    }
    memcpy(body, start, len);
    body[len] = '\0';
    /* Consume the closing quote */
    sql_consume(L);
    return body;
}

/* ------------------------------------------------------------------ */
/*  Embedded JSON parse                                                */
/* ------------------------------------------------------------------ */

static JsonValue *parse_embedded_json(const char *body) {
    JsonScanner sc;
    json_scanner_init(&sc, body, strlen(body));

    void *parser = JsonAlloc(malloc);
    if (parser == NULL) return NULL;

    JsonValue *root = NULL;
    int tok;
    void *value;
    while ((tok = json_scan(&sc, &value)) > 0) {
        Json(parser, tok, value, &root);
    }
    Json(parser, 0, NULL, &root);
    JsonFree(parser, free);

    if (tok < 0) {
        json_free(root);
        return NULL;
    }
    return root;
}

/* ------------------------------------------------------------------ */
/*  Top-level driver                                                   */
/* ------------------------------------------------------------------ */

MultiParseStatus multi_parse_sql(const char *input, bool register_json_trigger, SqlSelect **out) {
    if (out == NULL) return MULTI_PARSE_ERROR;
    *out = NULL;
    if (input == NULL) return MULTI_PARSE_ERROR;

    /* Build the host context stack and register the JSON trigger. */
    ParserSnapshot *root_snap = make_tag_snapshot(1);
    ParserSnapshot *json_tag = make_tag_snapshot(2);
    if (root_snap == NULL || json_tag == NULL) {
        snapshot_release(root_snap);
        snapshot_release(json_tag);
        return MULTI_PARSE_ERROR;
    }

    GrammarContextStack *stack = grammar_context_create(root_snap);
    if (stack == NULL) {
        snapshot_release(root_snap);
        snapshot_release(json_tag);
        return MULTI_PARSE_ERROR;
    }

    if (register_json_trigger) {
        if (!context_switch_register_trigger(stack, "json", json_tag, "json")) {
            grammar_context_destroy(stack);
            snapshot_release(root_snap);
            snapshot_release(json_tag);
            return MULTI_PARSE_ERROR;
        }
    }

    SqlSelect *sel = sql_select_new();
    if (sel == NULL) {
        grammar_context_destroy(stack);
        snapshot_release(root_snap);
        snapshot_release(json_tag);
        return MULTI_PARSE_ERROR;
    }

    SqlLex L;
    sql_lex_init(&L, input);

    /* Expect: SELECT <columns> FROM <table> WHERE <ident> = <number> ; */
    char buf[64];
    sql_skip_ws(&L);
    sql_read_word(&L, buf, sizeof(buf));
    if (strcmp(buf, "SELECT") != 0) goto parse_err;

    /* Columns -- comma-separated list of IDENT or  json '...' */
    for (;;) {
        sql_skip_ws(&L);
        int line0 = L.line, col0 = L.col;

        if (!isalpha((unsigned char)sql_peek(&L)) && sql_peek(&L) != '_') goto parse_err;
        sql_read_word(&L, buf, sizeof(buf));

        /* Context-switch decision: consult the trigger registry. */
        GrammarMode m = context_switch_classify_lexeme(stack, buf);
        if (m != MODE_NONE && register_json_trigger) {
            /* Trigger fired -- enter the embedded grammar mode. */
            grammar_context_push(stack, m, (uint32_t)(L.cursor - input));

            sql_skip_ws(&L);
            if (sql_peek(&L) != '\'') {
                MultiParseStatus err = MULTI_LEX_ERROR;
                sql_select_destroy(sel);
                grammar_context_destroy(stack);
                snapshot_release(root_snap);
                snapshot_release(json_tag);
                return err;
            }
            sql_consume(&L); /* consume opening ' */

            MultiParseStatus jerr = MULTI_OK;
            char *body = sql_read_json_body(&L, &jerr);
            if (body == NULL) {
                sql_select_destroy(sel);
                grammar_context_destroy(stack);
                snapshot_release(root_snap);
                snapshot_release(json_tag);
                return jerr;
            }

            JsonValue *root = parse_embedded_json(body);
            free(body);
            if (root == NULL) {
                sql_select_destroy(sel);
                grammar_context_destroy(stack);
                snapshot_release(root_snap);
                snapshot_release(json_tag);
                return MULTI_JSON_ERROR;
            }

            /* Pop the embedded mode -- back to host SQL. */
            grammar_context_pop(stack);

            sql_select_add_column(sel, sql_col_json(root, line0, col0));
        } else if (m != MODE_NONE && !register_json_trigger) {
            /* Lexeme matches a registered trigger -- but the caller
            ** asked us to NOT register triggers, so this branch is
            ** unreachable in practice. */
            sql_select_destroy(sel);
            grammar_context_destroy(stack);
            snapshot_release(root_snap);
            snapshot_release(json_tag);
            return MULTI_NO_TRIGGER;
        } else {
            /* Plain identifier column. */
            sql_select_add_column(sel, sql_col_ident(buf, line0, col0));
        }

        sql_skip_ws(&L);
        if (sql_peek(&L) == ',') {
            sql_consume(&L);
            continue;
        }
        break;
    }

    sql_skip_ws(&L);
    sql_read_word(&L, buf, sizeof(buf));
    if (strcmp(buf, "FROM") != 0) goto parse_err;

    sql_skip_ws(&L);
    sql_read_word(&L, buf, sizeof(buf));
    if (buf[0] == '\0') goto parse_err;
    sel->table = strdup(buf);

    sql_skip_ws(&L);
    sql_read_word(&L, buf, sizeof(buf));
    if (strcmp(buf, "WHERE") != 0) goto parse_err;

    sql_skip_ws(&L);
    sql_read_word(&L, buf, sizeof(buf));
    if (buf[0] == '\0') goto parse_err;
    sel->where_lhs = strdup(buf);

    sql_skip_ws(&L);
    if (sql_peek(&L) != '=') goto parse_err;
    sql_consume(&L);

    sql_skip_ws(&L);
    char *endp = NULL;
    sel->where_rhs = strtol(L.cursor, &endp, 10);
    if (endp == L.cursor) goto parse_err;
    L.col += (int)(endp - L.cursor);
    L.cursor = endp;

    sql_skip_ws(&L);
    if (sql_peek(&L) == ';') sql_consume(&L);

    grammar_context_destroy(stack);
    snapshot_release(root_snap);
    snapshot_release(json_tag);

    *out = sel;
    return MULTI_OK;

parse_err:
    sql_select_destroy(sel);
    grammar_context_destroy(stack);
    snapshot_release(root_snap);
    snapshot_release(json_tag);
    return MULTI_PARSE_ERROR;
}
