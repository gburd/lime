/*
 * lsp_rename.c -- textDocument/rename + textDocument/prepareRename.
 *
 * prepareRename validates that the cursor sits on a renameable
 * symbol -- a terminal or non-terminal whose definition is
 * present in the open document.  Directives, keywords, and
 * positions inside string / comment / brace bodies all return
 * NULL with a populated reason buffer; the caller surfaces those
 * as JSON-RPC error responses.
 *
 * rename produces a WorkspaceEdit whose `changes` map carries one
 * TextEdit per occurrence (definition + every reference) with
 * the same `newText`.  The editor applies the patch.  We do not
 * emit `documentChanges` (which carry version stamps) because
 * the v0.5.0 capability advertisement does not promise
 * `workspace.workspaceEdit.documentChanges` support; plain
 * `changes` is the LSP-spec backward-compatible form.
 *
 * Validation logic:
 *
 *   - new name must match `[A-Za-z_][A-Za-z0-9_]*`
 *   - new name must not already be in use as a different symbol
 *     (collision guard).  The same name as the symbol being
 *     renamed is rejected too -- a no-op rename is a UI bug.
 *   - new name must not be a known %-directive (e.g. trying to
 *     rename a token to `token`).
 */

#include "lsp_navigation.h"
#include "lsp_documents.h"
#include "lsp_json.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int valid_identifier(const char *s) {
    if (!s || !*s) return 0;
    unsigned char c = (unsigned char)s[0];
    if (!(isalpha(c) || c == '_')) return 0;
    for (size_t i = 1; s[i]; i++) {
        unsigned char ch = (unsigned char)s[i];
        if (!(isalnum(ch) || ch == '_')) return 0;
    }
    return 1;
}

static int is_known_directive(const char *name) {
    const lsp_directive_info *d = lsp_known_directives();
    for (size_t i = 0; d[i].name; i++) {
        if (strcmp(d[i].name, name) == 0) return 1;
    }
    return 0;
}

static json_value *make_range(long long sl, long long sc,
                              long long el, long long ec) {
    json_value *r = json_make_object();
    json_value *s = json_make_object();
    json_object_set(s, "line",      json_make_int(sl));
    json_object_set(s, "character", json_make_int(sc));
    json_value *e = json_make_object();
    json_object_set(e, "line",      json_make_int(el));
    json_object_set(e, "character", json_make_int(ec));
    json_object_set(r, "start", s);
    json_object_set(r, "end",   e);
    return r;
}

json_value *lsp_navigation_prepare_rename(const char *text, size_t text_len,
                                          long long line, long long character,
                                          char *out_err, size_t err_cap) {
    if (out_err && err_cap) out_err[0] = 0;

    size_t off = lsp_position_to_offset(text, text_len, line, character);
    size_t ws, we;
    lsp_word_at_offset(text, text_len, off, &ws, &we);
    if (ws == we) {
        if (out_err) snprintf(out_err, err_cap,
                              "no symbol at this position");
        return NULL;
    }
    if (text[ws] == '%') {
        if (out_err) snprintf(out_err, err_cap,
                              "directives cannot be renamed");
        return NULL;
    }

    lsp_symtab st;
    lsp_symtab_build(&st, text, text_len);
    const lsp_symbol *sym = lsp_symtab_lookup(&st, text + ws, we - ws);
    if (!sym ||
        (sym->kind != LSP_SYM_TERMINAL &&
         sym->kind != LSP_SYM_NONTERMINAL)) {
        lsp_symtab_free(&st);
        if (out_err) snprintf(out_err, err_cap,
                              "symbol not found in this document");
        return NULL;
    }

    long long sl, sc, eline, ec;
    lsp_offset_to_position(text, text_len, ws, &sl, &sc);
    lsp_offset_to_position(text, text_len, we, &eline, &ec);

    json_value *out = json_make_object();
    json_object_set(out, "range",       make_range(sl, sc, eline, ec));
    json_object_set(out, "placeholder", json_make_string(sym->name));
    lsp_symtab_free(&st);
    return out;
}

json_value *lsp_navigation_rename(const char *uri,
                                  const char *text, size_t text_len,
                                  long long line, long long character,
                                  const char *new_name,
                                  char *out_err, size_t err_cap) {
    if (out_err && err_cap) out_err[0] = 0;

    if (!new_name || !valid_identifier(new_name)) {
        if (out_err) snprintf(out_err, err_cap,
                              "`%s` is not a valid lime identifier",
                              new_name ? new_name : "");
        return NULL;
    }
    if (is_known_directive(new_name)) {
        if (out_err) snprintf(out_err, err_cap,
                              "`%s` collides with a known %%-directive",
                              new_name);
        return NULL;
    }

    size_t off = lsp_position_to_offset(text, text_len, line, character);
    size_t ws, we;
    lsp_word_at_offset(text, text_len, off, &ws, &we);
    if (ws == we || text[ws] == '%') {
        if (out_err) snprintf(out_err, err_cap,
                              "no renameable symbol at this position");
        return NULL;
    }

    lsp_symtab st;
    lsp_symtab_build(&st, text, text_len);
    const lsp_symbol *sym = lsp_symtab_lookup(&st, text + ws, we - ws);
    if (!sym ||
        (sym->kind != LSP_SYM_TERMINAL &&
         sym->kind != LSP_SYM_NONTERMINAL)) {
        lsp_symtab_free(&st);
        if (out_err) snprintf(out_err, err_cap,
                              "symbol not found in this document");
        return NULL;
    }

    /* Trivial-rename / collision guard.  Same name -> no-op; a
     * different existing symbol with that name -> collision. */
    if (strcmp(sym->name, new_name) == 0) {
        lsp_symtab_free(&st);
        if (out_err) snprintf(out_err, err_cap,
                              "new name `%s` is the same as the old name",
                              new_name);
        return NULL;
    }
    const lsp_symbol *clash = lsp_symtab_lookup(&st, new_name,
                                                 strlen(new_name));
    if (clash && clash != sym) {
        lsp_symtab_free(&st);
        if (out_err) snprintf(out_err, err_cap,
                              "new name `%s` is already in use",
                              new_name);
        return NULL;
    }

    /* Build the WorkspaceEdit.  Single-file: changes[uri] -> [TextEdit]. */
    json_value *edits = json_make_array();
    for (size_t i = 0; i < sym->ref_count; i++) {
        const lsp_symref *r = &sym->refs[i];
        json_value *te = json_make_object();
        json_object_set(te, "range",
                        make_range(r->line, r->col, r->line, r->end_col));
        json_object_set(te, "newText", json_make_string(new_name));
        json_array_push(edits, te);
    }
    json_value *changes = json_make_object();
    json_object_set(changes, uri, edits);
    json_value *result = json_make_object();
    json_object_set(result, "changes", changes);
    lsp_symtab_free(&st);
    return result;
}
