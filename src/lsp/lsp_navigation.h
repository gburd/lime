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

typedef struct {
    char        *name;       /* heap, NUL-terminated   */
    lsp_sym_kind kind;
    size_t       def_offset; /* byte offset of decl in text */
    size_t       def_end;    /* byte offset of end of decl   */
    long long    def_line;   /* 0-based line                 */
    long long    def_col;    /* 0-based column               */
    long long    def_end_col;/* 0-based column of end        */
    long          uses;      /* number of references seen    */
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

#endif /* LIME_LSP_NAVIGATION_H */
