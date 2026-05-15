/*
** src/lex/lex_main.c -- driver for lime's `-X` mode.
**
** Reads a .lex source file, runs the M1 pipeline (tokenize ->
** parse -> resolve), and emits the pretty-printed canonical
** form to stdout.  This is the M1 ship-gate end-to-end
** invocation: a one-shot lex-compiler frontend ready to be
** consumed by the M2 DFA compiler in a subsequent milestone.
**
** Exit status:
**   0  success
**   1  parse / resolve errors (count printed to stderr)
**   2  I/O or alloc error
*/

#include "lex_ast.h"
#include "lex_parse.h"
#include "lex_pretty.h"
#include "lex_resolve.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Slurp the entire file into a heap buffer.  Returns 0 on
** success; sets *out_buf and *out_len.  Caller frees *out_buf. */
static int slurp(const char *path, char **out_buf, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "lime: cannot open '%s': %s\n", path, strerror(errno));
        return 2;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fprintf(stderr, "lime: seek failed on '%s'\n", path);
        fclose(f);
        return 2;
    }
    long sz = ftell(f);
    if (sz < 0) {
        fprintf(stderr, "lime: ftell failed on '%s'\n", path);
        fclose(f);
        return 2;
    }
    rewind(f);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fprintf(stderr, "lime: out of memory reading '%s'\n", path);
        fclose(f);
        return 2;
    }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) {
        fprintf(stderr, "lime: short read on '%s' (%zu of %ld bytes)\n",
                path, got, sz);
        free(buf);
        return 2;
    }
    buf[sz] = '\0';
    *out_buf = buf;
    *out_len = (size_t)sz;
    return 0;
}

int lime_lex_run_compiler(const char *input_path) {
    char *src = NULL;
    size_t src_len = 0;
    int rc = slurp(input_path, &src, &src_len);
    if (rc != 0) return rc;

    LimeLexSpec *spec = lime_lex_parse(input_path, src, src_len);
    if (!spec) {
        free(src);
        fprintf(stderr, "lime: parse returned NULL (alloc failure)\n");
        return 2;
    }
    if (spec->error_count > 0) {
        fprintf(stderr, "lime: %d parse error(s) in '%s'\n",
                spec->error_count, input_path);
        lime_lex_spec_free(spec);
        free(src);
        return 1;
    }

    int resolve_rc = lime_lex_resolve_patterns(spec);
    if (resolve_rc != 0 || spec->error_count > 0) {
        fprintf(stderr, "lime: %d resolve error(s) in '%s'\n",
                spec->error_count, input_path);
        lime_lex_spec_free(spec);
        free(src);
        return 1;
    }

    char *out = lime_lex_spec_to_text(spec);
    if (!out) {
        fprintf(stderr, "lime: pretty-print returned NULL (alloc failure)\n");
        lime_lex_spec_free(spec);
        free(src);
        return 2;
    }
    fputs(out, stdout);
    free(out);

    lime_lex_spec_free(spec);
    free(src);
    return 0;
}
