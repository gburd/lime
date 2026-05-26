/*
 * lsp_documents.c -- track open textDocuments by URI.
 *
 * Storage is a flat dynamic array; the MVP has < 10 files open at
 * a time, so linear scan beats a hash table on cache footprint.
 */

#include "lsp_documents.h"

#include <stdlib.h>
#include <string.h>

void lsp_documents_init(lsp_documents *ds) {
    ds->docs  = NULL;
    ds->count = 0;
    ds->cap   = 0;
}

void lsp_documents_free(lsp_documents *ds) {
    for (size_t i = 0; i < ds->count; i++) {
        free(ds->docs[i].uri);
        free(ds->docs[i].text);
    }
    free(ds->docs);
    ds->docs  = NULL;
    ds->count = 0;
    ds->cap   = 0;
}

static lsp_document *find_by_uri(lsp_documents *ds, const char *uri) {
    for (size_t i = 0; i < ds->count; i++) {
        if (strcmp(ds->docs[i].uri, uri) == 0) return &ds->docs[i];
    }
    return NULL;
}

static int reserve(lsp_documents *ds, size_t need) {
    if (ds->cap >= need) return 1;
    size_t nc = ds->cap ? ds->cap * 2 : 4;
    while (nc < need) nc *= 2;
    lsp_document *nd = (lsp_document *)realloc(ds->docs, nc * sizeof(*nd));
    if (!nd) return 0;
    ds->docs = nd;
    ds->cap  = nc;
    return 1;
}

int lsp_documents_open(lsp_documents *ds, const char *uri,
                       long long version,
                       const char *text, size_t text_len) {
    lsp_document *existing = find_by_uri(ds, uri);
    if (existing) {
        return lsp_documents_set_text(ds, uri, version, text, text_len);
    }
    if (!reserve(ds, ds->count + 1)) return 0;
    char *uc = strdup(uri);
    char *tc = (char *)malloc(text_len + 1);
    if (!uc || !tc) { free(uc); free(tc); return 0; }
    if (text_len) memcpy(tc, text, text_len);
    tc[text_len] = 0;
    ds->docs[ds->count].uri      = uc;
    ds->docs[ds->count].text     = tc;
    ds->docs[ds->count].text_len = text_len;
    ds->docs[ds->count].version  = version;
    ds->count++;
    return 1;
}

int lsp_documents_set_text(lsp_documents *ds, const char *uri,
                           long long version,
                           const char *text, size_t text_len) {
    lsp_document *d = find_by_uri(ds, uri);
    if (!d) return 0;
    char *tc = (char *)malloc(text_len + 1);
    if (!tc) return 0;
    if (text_len) memcpy(tc, text, text_len);
    tc[text_len] = 0;
    free(d->text);
    d->text     = tc;
    d->text_len = text_len;
    d->version  = version;
    return 1;
}

int lsp_documents_close(lsp_documents *ds, const char *uri) {
    for (size_t i = 0; i < ds->count; i++) {
        if (strcmp(ds->docs[i].uri, uri) == 0) {
            free(ds->docs[i].uri);
            free(ds->docs[i].text);
            /* swap-remove */
            ds->docs[i] = ds->docs[ds->count - 1];
            ds->count--;
            return 1;
        }
    }
    return 0;
}

lsp_document *lsp_documents_get(lsp_documents *ds, const char *uri) {
    return find_by_uri(ds, uri);
}

size_t lsp_position_to_offset(const char *text, size_t text_len,
                              long long line, long long character) {
    if (line < 0) line = 0;
    if (character < 0) character = 0;
    size_t i = 0;
    long long cur_line = 0;
    while (i < text_len && cur_line < line) {
        if (text[i] == '\n') cur_line++;
        i++;
    }
    long long col = 0;
    while (i < text_len && text[i] != '\n' && col < character) {
        i++;
        col++;
    }
    return i;
}

void lsp_offset_to_position(const char *text, size_t text_len,
                            size_t offset,
                            long long *line, long long *character) {
    if (offset > text_len) offset = text_len;
    long long ln = 0;
    long long col = 0;
    for (size_t i = 0; i < offset; i++) {
        if (text[i] == '\n') { ln++; col = 0; }
        else                 { col++; }
    }
    *line = ln;
    *character = col;
}
