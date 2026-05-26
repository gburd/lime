/*
 * lsp_documents.h -- in-memory document store for the Lime LSP.
 *
 * The LSP server tracks every textDocument that is currently open
 * in the editor.  We keep their contents in memory so that
 * `textDocument/didChange` events can apply incremental edits and
 * the running server can run analysis (definitions, hover,
 * outline) without touching the disk.
 *
 * Documents are looked up by URI (`file:///path/to/foo.lime`).
 * The document store is a flat dynamic array; the LSP MVP never
 * has more than a handful of files open simultaneously and a
 * linear scan is fine.
 */
#ifndef LIME_LSP_DOCUMENTS_H
#define LIME_LSP_DOCUMENTS_H

#include <stddef.h>

typedef struct lsp_document {
    char  *uri;          /* heap, NUL-terminated */
    char  *text;         /* heap, NUL-terminated */
    size_t text_len;
    long long version;   /* per textDocument/didChange `version` */
} lsp_document;

typedef struct lsp_documents {
    lsp_document *docs;
    size_t        count;
    size_t        cap;
} lsp_documents;

void          lsp_documents_init(lsp_documents *ds);
void          lsp_documents_free(lsp_documents *ds);

/* Open or replace a document.  `text` is copied. */
int           lsp_documents_open(lsp_documents *ds, const char *uri,
                                 long long version,
                                 const char *text, size_t text_len);

/* Replace document text wholesale.  Returns 0 if URI not found. */
int           lsp_documents_set_text(lsp_documents *ds, const char *uri,
                                     long long version,
                                     const char *text, size_t text_len);

/* Close a document.  Returns 0 if URI not found. */
int           lsp_documents_close(lsp_documents *ds, const char *uri);

/* Lookup by URI.  Returns NULL if not open. */
lsp_document *lsp_documents_get(lsp_documents *ds, const char *uri);

/* Convert an LSP {line, character} position into a byte offset
 * inside the document text.  LSP positions are 0-based.  This
 * implementation treats `character` as a UTF-16 code-unit count
 * (the LSP default) but, since Lime grammar files are 7-bit ASCII
 * in practice, we collapse it to byte counting.  Out-of-range
 * positions clamp to text_len.
 */
size_t        lsp_position_to_offset(const char *text, size_t text_len,
                                     long long line, long long character);

/* Convert a byte offset to {line, character} (0-based, ASCII). */
void          lsp_offset_to_position(const char *text, size_t text_len,
                                     size_t offset,
                                     long long *line, long long *character);

#endif /* LIME_LSP_DOCUMENTS_H */
