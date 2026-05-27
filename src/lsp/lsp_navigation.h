/*
 * lsp_navigation.h -- in-process .lime tokenizer + symbol table.
 *
 * Definition / hover / documentSymbol all need to know:
 *
 *   - which non-terminal is defined on which line (LHS of `::=`)
 *   - which terminals are declared by `%token NAME.`
 *   - which directives (top-level %name, %type, etc.) appear and
 *     where
 *
 * We do not invoke lime.c's full parser for this -- we just walk
 * the buffer with a tiny scanner that handles C-style block
 * comments, `//` line comments, double-quoted strings, and
 * brace-balanced action blocks (so that `LHS ::= ... { code with
 * ::= inside }` doesn't confuse us).  The model is rebuilt on
 * every request; documents are typically a few thousand lines so
 * this is cheap.
 *
 * The model is intentionally over-permissive: even a partially-
 * broken grammar that the real parser rejects produces a usable
 * outline, so editor features keep working while the user is
 * mid-edit.
 */
#ifndef LIME_LSP_NAVIGATION_H
#define LIME_LSP_NAVIGATION_H

#include "lsp_json.h"

#include <stddef.h>

typedef enum {
    LSP_SYM_NONTERMINAL = 1,   /* LHS of a `::=` rule          */
    LSP_SYM_TERMINAL    = 2,   /* declared by `%token NAME.`   */
    LSP_SYM_DIRECTIVE   = 3    /* %name, %type, %include, ...  */
} lsp_sym_kind;

/* One occurrence of a symbol in the source.  May be either a
 * definition site or a reference site; the `is_definition` flag
 * disambiguates.  The line / column fields are 0-based and pre-
 * computed at scan time so callers don't pay the offset->position
 * cost on every emit.
 */
typedef struct {
    size_t    start;
    size_t    end;
    long long line;
    long long col;
    long long end_col;
    int       is_definition;
} lsp_symref;

typedef struct {
    char        *name;       /* heap, NUL-terminated   */
    lsp_sym_kind kind;
    size_t       def_offset; /* byte offset of decl in text */
    size_t       def_end;    /* byte offset of end of decl   */
    long long    def_line;   /* 0-based line                 */
    long long    def_col;    /* 0-based column               */
    long long    def_end_col;/* 0-based column of end        */
    long          uses;      /* number of references seen    */
    /* Every occurrence (def + refs) recorded by the scanner, in
     * source order.  Used by references / rename / semantic
     * tokens.  Owned by the symbol; freed in lsp_symtab_free.
     */
    lsp_symref  *refs;
    size_t       ref_count;
    size_t       ref_cap;
} lsp_symbol;

typedef struct {
    lsp_symbol *items;
    size_t      count;
    size_t      cap;
} lsp_symtab;

/* Scan the buffer and build the symbol table.  Caller frees with
 * lsp_symtab_free.
 */
void lsp_symtab_build(lsp_symtab *st, const char *text, size_t text_len);
void lsp_symtab_free(lsp_symtab *st);

/* Find the identifier the cursor is on.  Returns the [start,end)
 * byte offsets, or 0,0 if the cursor is not on an identifier.
 */
void lsp_word_at_offset(const char *text, size_t text_len, size_t offset,
                        size_t *out_start, size_t *out_end);

/* Lookup a symbol by name.  Returns NULL if unknown. */
const lsp_symbol *lsp_symtab_lookup(const lsp_symtab *st,
                                    const char *name, size_t name_len);

/* Build a textDocument/definition response for cursor at line/col
 * inside text.  Returns:
 *   - JSON Location object on hit
 *   - JSON null on miss (LSP allows null for "no definition")
 */
json_value *lsp_navigation_definition(const char *uri,
                                      const char *text, size_t text_len,
                                      long long line, long long character);

/* Build a textDocument/hover response.  Returns:
 *   - { contents: "...", range: ... } object on hit
 *   - JSON null on miss
 */
json_value *lsp_navigation_hover(const char *text, size_t text_len,
                                 long long line, long long character);

/* Build a textDocument/documentSymbol response (LSP
 * DocumentSymbol[] form).  Returns a JSON array.
 */
json_value *lsp_navigation_document_symbol(const char *text, size_t text_len);

/* Build a textDocument/completion response.  Returns a CompletionList
 * `{ isIncomplete: false, items: [...] }`.  `trigger` may be NUL.
 */
json_value *lsp_navigation_completion(const char *text, size_t text_len,
                                      long long line, long long character,
                                      char trigger);

/* Build a textDocument/references response (Location[]).  Returns a
 * JSON array, possibly empty.  `include_declaration` toggles inclusion
 * of the symbol's defining occurrence.
 */
json_value *lsp_navigation_references(const char *uri,
                                      const char *text, size_t text_len,
                                      long long line, long long character,
                                      int include_declaration);

/* Build a textDocument/prepareRename response.  On success returns an
 * object `{ range, placeholder }`.  On a non-renameable position
 * returns NULL and (when `out_err` is non-NULL) populates a short
 * human-readable reason there.
 */
json_value *lsp_navigation_prepare_rename(const char *text, size_t text_len,
                                          long long line, long long character,
                                          char *out_err, size_t err_cap);

/* Build a textDocument/rename response.  Returns a WorkspaceEdit on
 * success, NULL on failure (with `out_err` populated).
 */
json_value *lsp_navigation_rename(const char *uri,
                                  const char *text, size_t text_len,
                                  long long line, long long character,
                                  const char *new_name,
                                  char *out_err, size_t err_cap);

/* Build a textDocument/semanticTokens/full response
 * `{ data: [deltaLine, deltaStart, length, tokenType, tokenMods]* }`.
 */
json_value *lsp_navigation_semantic_tokens(const char *text, size_t text_len);

/* Static legend used in the initialize response so the editor knows
 * what the integer token-type / token-modifier ids mean.  The arrays
 * are NULL-terminated.  Indices match the encoding in
 * lsp_navigation_semantic_tokens.
 */
extern const char *const lsp_semantic_token_types[];
extern const char *const lsp_semantic_token_modifiers[];

/* One entry in the curated list of known %-directives.  Used by
 * hover (already) and completion (new in v0.5.7).
 */
typedef struct {
    const char *name;   /* without leading '%' */
    const char *doc;    /* one-line markdown description */
} lsp_directive_info;

/* NULL-terminated list of known directives, in spec order. */
const lsp_directive_info *lsp_known_directives(void);

/* Build a textDocument/codeLens response (CodeLens[]).  Returns a
 * JSON array, one entry per non-terminal definition with title
 * "N references".  Always returns a non-NULL array (possibly empty).
 */
json_value *lsp_navigation_code_lens(const char *text, size_t text_len);

/* Build a textDocument/signatureHelp response.  When cursor sits in
 * an argument position of a known %-directive, returns a SignatureHelp
 * object with the directive's documented synopsis.  Otherwise returns
 * JSON null.
 */
json_value *lsp_navigation_signature_help(const char *text, size_t text_len,
                                          long long line, long long character);

/* Build a textDocument/codeAction response (CodeAction[]).  v0.6.x
 * ships with a single "Format document" action that delegates to the
 * formatting capability.  Always returns a non-NULL array.
 */
json_value *lsp_navigation_code_actions(const char *text, size_t text_len,
                                        long long line, long long character);

#endif /* LIME_LSP_NAVIGATION_H */
