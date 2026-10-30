/*
** snapshot_create.c -- grammar file / text -> ParserSnapshot, in-process.
**
** create_base_snapshot("foo.y") and lime_compile_grammar_text(text) read
** the grammar and build its tables with the in-process LALR(1) compiler
** (lime_compile_grammar_in_process, lime.c).  No `lime` subprocess, no C
** compiler, no shared object.  This file is part of liblime_compiler:
** a consumer that builds snapshots at runtime links it explicitly, and
** a missing link is a link error, never a runtime fallback.
**
** Works the same on every platform (no fork/dlopen, so Windows too).
*/
#include "snapshot.h"
#include "lime_compiler.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ParserSnapshot *lime_compile_grammar_text(const char *grammar_text, size_t len, char **error) {
    ParserSnapshot *snap = NULL;
    if (error) *error = NULL;
    if (grammar_text == NULL || len == 0) {
        if (error) *error = strdup("lime_compile_grammar_text: grammar_text is empty");
        return NULL;
    }
    if (lime_compile_grammar_in_process(grammar_text, len, &snap, error) != 0) return NULL;
    return snap;
}

ParserSnapshot *create_base_snapshot(const char *grammar_file, char **error) {
    if (error) *error = NULL;
    if (grammar_file == NULL) {
        if (error) *error = strdup("create_base_snapshot: grammar_file is NULL");
        return NULL;
    }
    FILE *f = fopen(grammar_file, "rb");
    if (f == NULL) {
        if (error) {
            const char *why = strerror(errno);
            size_t n = strlen(grammar_file) + strlen(why) + 64;
            *error = malloc(n);
            if (*error) snprintf(*error, n, "create_base_snapshot: cannot open %s: %s",
                                 grammar_file, why);
        }
        return NULL;
    }
    char *buf = NULL;
    size_t len = 0, cap = 0;
    for (;;) {
        if (len + 4096 > cap) {
            size_t nc = cap ? cap * 2 : 65536;
            char *nb = realloc(buf, nc);
            if (nb == NULL) {
                free(buf);
                fclose(f);
                if (error) *error = strdup("create_base_snapshot: out of memory");
                return NULL;
            }
            buf = nb;
            cap = nc;
        }
        size_t got = fread(buf + len, 1, cap - len, f);
        len += got;
        if (got == 0) break;
    }
    int rd_err = ferror(f);
    fclose(f);
    if (rd_err || len == 0) {
        free(buf);
        if (error) *error = strdup(rd_err ? "create_base_snapshot: read error"
                                          : "create_base_snapshot: grammar file is empty");
        return NULL;
    }
    ParserSnapshot *snap = lime_compile_grammar_text(buf, len, error);
    free(buf);
    return snap;
}

ParserSnapshot *lime_snapshot_create(const char *grammar_file, char **error) {
    return create_base_snapshot(grammar_file, error);
}
