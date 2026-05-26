/*
 * lsp_completion.c -- textDocument/completion handler.
 *
 * Three context modes drive the suggestion list:
 *
 *   1. Directive context.  The cursor sits on or just after a
 *      leading `%` at the start of a line (whitespace-only
 *      prefix).  We emit every entry from the curated directive
 *      registry exposed by lsp_navigation.h.
 *
 *   2. RHS context.  The cursor follows a `::=` on the current
 *      rule with no rule terminator (`.`) seen since.  We emit
 *      every terminal + non-terminal currently known to the
 *      symbol table, prefix-filtered against the partial word
 *      under the cursor.
 *
 *   3. Token-list context.  The cursor follows one of
 *      `%token` / `%left` / `%right` / `%nonassoc` / `%type` /
 *      `%fallback` / `%wildcard` / `%token_class` and the
 *      directive run has not been terminated by `.`.  We emit
 *      terminals from the symbol table.
 *
 * Outside of those contexts (e.g. inside a brace block, inside a
 * comment, between two rules) we still fall back to the union of
 * directives + symbols so the editor at least has something to
 * show.  The returned list is always a CompletionList object so
 * the editor can disambiguate "no completions" (empty `items`)
 * from "completion not supported" (which never happens because
 * we always advertise `completionProvider`).
 *
 * SymbolKind values for each completion item are fixed:
 *   - directive   -> 14 (Keyword)
 *   - terminal    -> 21 (Constant) [LSP CompletionItemKind.Constant]
 *   - non-term    -> 6  (Variable)
 * Editors render these with kind-specific icons; lime's choices
 * mirror documentSymbol's SymbolKind mapping where possible.
 */

#include "lsp_navigation.h"
#include "lsp_documents.h"
#include "lsp_json.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* CompletionItemKind constants from the LSP spec.  Only the ones
 * we actually emit are listed; everything else stays unused. */
enum {
    CIK_VARIABLE = 6,
    CIK_KEYWORD  = 14,
    CIK_CONSTANT = 21
};

/* Walk back from `offset` over whitespace.  Returns 1 if every
 * byte from the start of the line up to the new position is
 * whitespace -- i.e. the cursor is at the start of a logical
 * grammar statement. */
static int at_line_start(const char *text, size_t offset) {
    while (offset > 0) {
        char c = text[offset - 1];
        if (c == '\n') return 1;
        if (c != ' ' && c != '\t') return 0;
        offset--;
    }
    return 1;
}

/* Scan back from `offset` to detect what sort of position the
 * cursor is in.  Returns one of the four context modes; for
 * MODE_TOKEN_LIST also writes the directive name (without `%`)
 * into `out_dir` for the caller's information.
 */
typedef enum {
    CTX_DIRECTIVE,    /* on a `%` introducing a directive  */
    CTX_RHS,          /* after `::=`, before `.`           */
    CTX_TOKEN_LIST,   /* inside a %token / %left / etc run */
    CTX_TOP_LEVEL     /* fallback: between statements      */
} ctx_kind;

/* True when the byte stream from index `i` upward (toward the
 * start of the buffer) hits a `::=` before it hits a `.` or a
 * `%directive`.  Comments + braces are skipped roughly --
 * imperfect, but the heuristic is good enough for completion
 * context detection.
 */
static int in_rule_rhs(const char *text, size_t offset) {
    /* Walk backwards.  Skip block comments by looking for the
     * closing `*` followed by `/`; we don't bother to do this
     * perfectly -- if we see a `.` or `%` first, we're not in an
     * RHS. */
    size_t i = offset;
    while (i > 0) {
        unsigned char c = (unsigned char)text[i - 1];
        if (c == '.') return 0;
        /* `::=` -- check three bytes */
        if (c == '=' && i >= 3 &&
            text[i - 2] == ':' && text[i - 3] == ':') {
            return 1;
        }
        if (c == '%') {
            /* If immediately preceded by an identifier char it's
             * not the start of a directive (e.g. `100%`); skip
             * so we don't false-trigger.  In practice grammar
             * files don't have `%` mid-identifier. */
            if (i >= 2 && (isalnum((unsigned char)text[i - 2]) ||
                           text[i - 2] == '_')) {
                i--;
                continue;
            }
            return 0;
        }
        i--;
    }
    return 0;
}

/* Walk back to the most recent `%directive` on the current
 * statement.  Returns 1 + writes directive name into `out` (no
 * `%`, NUL-terminated) iff such a directive exists and the
 * statement has not been terminated by `.`.
 */
static int current_directive(const char *text, size_t offset,
                             char *out, size_t cap) {
    size_t i = offset;
    while (i > 0) {
        unsigned char c = (unsigned char)text[i - 1];
        if (c == '.') return 0;
        if (c == '%') {
            /* read directive name forward */
            size_t k = i;
            size_t w = 0;
            while (k < offset && (isalnum((unsigned char)text[k]) ||
                                  text[k] == '_')) {
                if (w + 1 < cap) out[w++] = text[k];
                k++;
            }
            if (w == 0) return 0;
            out[w] = 0;
            return 1;
        }
        i--;
    }
    return 0;
}

static int dir_takes_token_list(const char *name) {
    static const char *kList[] = {
        "token", "left", "right", "nonassoc", "type",
        "fallback", "wildcard", "token_class", "destructor",
        "first_token", "error_sync", NULL
    };
    for (size_t i = 0; kList[i]; i++) {
        if (strcmp(kList[i], name) == 0) return 1;
    }
    return 0;
}

/* Find the partial word the user has already typed at the
 * cursor.  Returns [start,end) byte offsets of the prefix
 * (possibly empty).  Differs from lsp_word_at_offset because we
 * only look BACKWARD: we don't want to claim the rest of an
 * identifier that already extends to the right of the cursor.
 */
static void prefix_at_offset(const char *text, size_t offset,
                             size_t *out_start, size_t *out_end,
                             int *out_has_percent) {
    size_t s = offset;
    while (s > 0) {
        unsigned char c = (unsigned char)text[s - 1];
        if (isalnum(c) || c == '_') { s--; continue; }
        break;
    }
    *out_has_percent = 0;
    if (s > 0 && text[s - 1] == '%') {
        s--;
        *out_has_percent = 1;
    }
    *out_start = s;
    *out_end   = offset;
}

static ctx_kind detect_context(const char *text, size_t text_len,
                               size_t offset, char trigger,
                               char *out_dir, size_t dir_cap) {
    (void)text_len;
    out_dir[0] = 0;

    size_t ps, pe;
    int has_percent;
    prefix_at_offset(text, offset, &ps, &pe, &has_percent);
    /* Trigger `%`: definitely directive context if at line start. */
    if (trigger == '%' || has_percent) {
        if (at_line_start(text, ps)) return CTX_DIRECTIVE;
    }

    if (in_rule_rhs(text, offset)) return CTX_RHS;

    if (current_directive(text, offset, out_dir, dir_cap)) {
        if (dir_takes_token_list(out_dir)) return CTX_TOKEN_LIST;
    }
    return CTX_TOP_LEVEL;
}

/* Build a CompletionItem object. */
static json_value *make_item(const char *label, int kind,
                             const char *detail, const char *doc) {
    json_value *o = json_make_object();
    json_object_set(o, "label", json_make_string(label));
    json_object_set(o, "kind",  json_make_int(kind));
    if (detail) json_object_set(o, "detail",        json_make_string(detail));
    if (doc) {
        json_value *md = json_make_object();
        json_object_set(md, "kind",  json_make_string("markdown"));
        json_object_set(md, "value", json_make_string(doc));
        json_object_set(o, "documentation", md);
    }
    return o;
}

/* Returns 1 if `prefix` is a prefix of `word`.  Empty prefix
 * matches everything. */
static int has_prefix(const char *word, const char *prefix, size_t plen) {
    if (plen == 0) return 1;
    return strncmp(word, prefix, plen) == 0;
}

json_value *lsp_navigation_completion(const char *text, size_t text_len,
                                      long long line, long long character,
                                      char trigger) {
    size_t off = lsp_position_to_offset(text, text_len, line, character);
    char dir_name[64];
    ctx_kind ctx = detect_context(text, text_len, off, trigger,
                                  dir_name, sizeof(dir_name));

    size_t ps, pe;
    int has_percent;
    prefix_at_offset(text, off, &ps, &pe, &has_percent);
    /* Effective prefix excludes the leading `%`. */
    const char *prefix = text + ps + (has_percent ? 1 : 0);
    size_t plen = (pe > ps + (has_percent ? 1 : 0))
                      ? pe - ps - (has_percent ? 1 : 0)
                      : 0;

    json_value *items = json_make_array();

    int emit_directives = (ctx == CTX_DIRECTIVE) || (ctx == CTX_TOP_LEVEL);
    int emit_tokens     = (ctx == CTX_RHS) || (ctx == CTX_TOKEN_LIST) ||
                          (ctx == CTX_TOP_LEVEL);
    int emit_nonterms   = (ctx == CTX_RHS) || (ctx == CTX_TOP_LEVEL);

    if (emit_directives) {
        const lsp_directive_info *d = lsp_known_directives();
        for (size_t i = 0; d[i].name; i++) {
            if (!has_prefix(d[i].name, prefix, plen)) continue;
            char detail[256];
            snprintf(detail, sizeof(detail), "%%%s", d[i].name);
            json_array_push(items, make_item(d[i].name, CIK_KEYWORD,
                                             detail, d[i].doc));
        }
    }

    if (emit_tokens || emit_nonterms) {
        lsp_symtab st;
        lsp_symtab_build(&st, text, text_len);
        for (size_t i = 0; i < st.count; i++) {
            const lsp_symbol *s = &st.items[i];
            if (s->kind == LSP_SYM_DIRECTIVE) continue;
            if (!has_prefix(s->name, prefix, plen)) continue;
            int kind;
            const char *detail;
            if (s->kind == LSP_SYM_TERMINAL) {
                if (!emit_tokens) continue;
                kind   = CIK_CONSTANT;
                detail = "terminal";
            } else {
                if (!emit_nonterms) continue;
                kind   = CIK_VARIABLE;
                detail = "non-terminal";
            }
            json_array_push(items, make_item(s->name, kind, detail, NULL));
        }
        lsp_symtab_free(&st);
    }

    json_value *result = json_make_object();
    json_object_set(result, "isIncomplete", json_make_bool(0));
    json_object_set(result, "items", items);
    return result;
}
