/*
 * lsp_references.c -- textDocument/references handler.
 *
 * Implementation strategy:
 *
 *   1. Resolve the symbol under the cursor using the same
 *      lsp_word_at_offset / lsp_symtab_lookup pair the
 *      definition handler uses.
 *   2. Walk the symbol's pre-computed `refs[]` array (populated
 *      by lsp_symtab_build's record_use / record_definition) and
 *      emit one Location per occurrence.
 *   3. If `include_declaration` is false, drop the entry whose
 *      `is_definition` flag is set.
 *
 * Multi-file resolution (across `%extends` / `%import`) is
 * currently scoped to the open document.  When the LSP grows a
 * cross-document model -- it will be the same store the existing
 * definition handler reads -- the references handler will inherit
 * that for free; we already produce Location objects with `uri`
 * fields that can be substituted on emit.
 */

#include "lsp_navigation.h"
#include "lsp_documents.h"
#include "lsp_json.h"

#include <stdlib.h>
#include <string.h>

static json_value *make_location(const char *uri,
                                 long long sl, long long sc,
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

    json_value *loc = json_make_object();
    json_object_set(loc, "uri",   json_make_string(uri));
    json_object_set(loc, "range", r);
    return loc;
}

json_value *lsp_navigation_references(const char *uri,
                                      const char *text, size_t text_len,
                                      long long line, long long character,
                                      int include_declaration) {
    size_t off = lsp_position_to_offset(text, text_len, line, character);
    size_t ws, we;
    lsp_word_at_offset(text, text_len, off, &ws, &we);
    json_value *arr = json_make_array();
    if (ws == we) return arr;
    if (text[ws] == '%') return arr;  /* directives have no references */

    lsp_symtab st;
    lsp_symtab_build(&st, text, text_len);
    const lsp_symbol *sym = lsp_symtab_lookup(&st, text + ws, we - ws);
    if (!sym) {
        lsp_symtab_free(&st);
        return arr;
    }

    for (size_t i = 0; i < sym->ref_count; i++) {
        const lsp_symref *r = &sym->refs[i];
        if (r->is_definition && !include_declaration) continue;
        json_array_push(arr, make_location(uri,
                                            r->line, r->col,
                                            r->line, r->end_col));
    }
    lsp_symtab_free(&st);
    return arr;
}
