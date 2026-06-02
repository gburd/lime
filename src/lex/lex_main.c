/*
** src/lex/lex_main.c -- driver for lime's `-X` mode.
**
** Reads a .lex source file, runs the M1 + M2 + M3.1 pipeline
** (tokenize -> parse -> resolve -> compile per-state DFAs ->
** emit C source), and writes `<basename>_lex.c` and
** `<basename>_lex.h` to either the current directory or the
** directory specified by `-d`.
**
** Exit status:
**   0  success
**   1  parse / resolve / compile errors (count printed to stderr)
**   2  I/O or alloc error
*/

#include "lex_ast.h"
#include "lex_compile.h"
#include "lex_emit.h"
#include "lex_parse.h"
#include "lex_pretty.h"
#include "lex_resolve.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* v0.8.10: --lex-vectorize default ON.  The CLI driver (lime.c)
** latches its parsed value into this global; src/lex/lex_emit.c
** reads it to decide whether to emit the multiversion-at-tokenize
** SIMD architecture.  Defined in lex_emit.c itself so test
** programs that link against liblime_lex_compiler.a without
** pulling lex_main.c (which transitively pulls emit_rust_lex.c
** with its own unresolved externs) still resolve the symbol. */

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
        fprintf(stderr, "lime: short read on '%s' (%zu of %ld bytes)\n", path, got, sz);
        free(buf);
        return 2;
    }
    buf[sz] = '\0';
    *out_buf = buf;
    *out_len = (size_t)sz;
    return 0;
}

/* Compute the input file's stem (basename minus `.lex` extension
** if present).  Caller frees. */
static char *input_stem(const char *path) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    const char *ext = strrchr(base, '.');
    size_t n = ext ? (size_t)(ext - base) : strlen(base);
    char *out = malloc(n + 1);
    if (!out) return NULL;
    memcpy(out, base, n);
    out[n] = '\0';
    return out;
}

/* Construct an output path: `<output_dir>/<stem>_lex.<suffix>` or
** `<stem>_lex.<suffix>` when output_dir is NULL.  Caller frees. */
static char *make_output_path(const char *output_dir, const char *stem, const char *suffix) {
    size_t dn = output_dir ? strlen(output_dir) + 1 : 0;
    size_t sn = strlen(stem);
    size_t xn = strlen(suffix);
    size_t total = dn + sn + 4 /* "_lex" */ + 1 /* "." */ + xn + 1;
    char *out = malloc(total);
    if (!out) return NULL;
    out[0] = '\0';
    if (output_dir) {
        strcat(out, output_dir);
        strcat(out, "/");
    }
    strcat(out, stem);
    strcat(out, "_lex.");
    strcat(out, suffix);
    return out;
}

/* Emit `.c` + `.h` files.  Returns 0 on success, 1 on emit
** error.  `output_dir` may be NULL (current directory). */
static int emit_files(LimeLexCompiled *c, const LimeLexSpec *spec, const char *input_path,
                      const char *output_dir) {
    char *stem = input_stem(input_path);
    if (!stem) {
        fprintf(stderr, "lime: out of memory computing stem\n");
        return 2;
    }
    char *h_path = make_output_path(output_dir, stem, "h");
    char *c_path = make_output_path(output_dir, stem, "c");
    if (!h_path || !c_path) {
        fprintf(stderr, "lime: out of memory computing output paths\n");
        free(stem);
        free(h_path);
        free(c_path);
        return 2;
    }

    char **rule_names = NULL;
    int n_rules = 0;
    if (lime_lex_collect_rule_names(spec, &rule_names, &n_rules) != 0) {
        fprintf(stderr, "lime: out of memory collecting rule names\n");
        free(stem);
        free(h_path);
        free(c_path);
        return 2;
    }

    const char *prefix = spec->name_prefix ? spec->name_prefix : "Lex";

    /* Header. */
    FILE *fh = fopen(h_path, "wb");
    if (!fh) {
        fprintf(stderr, "lime: cannot open '%s' for writing: %s\n", h_path, strerror(errno));
        for (int i = 0; i < n_rules; i++)
            free(rule_names[i]);
        free(rule_names);
        free(stem);
        free(h_path);
        free(c_path);
        return 2;
    }
    int rc = lime_lex_emit_h(c, spec, prefix, (const char *const *)rule_names, n_rules, fh);
    fclose(fh);
    if (rc != 0) {
        fprintf(stderr, "lime: header emit failed for '%s'\n", h_path);
        for (int i = 0; i < n_rules; i++)
            free(rule_names[i]);
        free(rule_names);
        free(stem);
        free(h_path);
        free(c_path);
        return 2;
    }

    /* Source.  The .c file references the .h by basename only
    ** (not the full path), so callers can move the pair around. */
    char *h_basename = malloc(strlen(stem) + 4 + 2 + 1);
    if (h_basename) {
        sprintf(h_basename, "%s_lex.h", stem);
    }
    FILE *fc = fopen(c_path, "wb");
    if (!fc) {
        fprintf(stderr, "lime: cannot open '%s' for writing: %s\n", c_path, strerror(errno));
        free(h_basename);
        for (int i = 0; i < n_rules; i++)
            free(rule_names[i]);
        free(rule_names);
        free(stem);
        free(h_path);
        free(c_path);
        return 2;
    }
    rc = lime_lex_emit_c(c, spec, prefix, h_basename ? h_basename : "lex.h",
                         (const char *const *)rule_names, n_rules, fc);
    fclose(fc);

    int err = (rc != 0);
    if (err) {
        fprintf(stderr, "lime: source emit failed for '%s'\n", c_path);
    } else {
        fprintf(stderr, "lime -X: wrote %s and %s\n", h_path, c_path);
    }

    /* v0.9.3: flex-API skin.  Emit <stem>_flex.{c,h} alongside the
    ** standard <stem>_lex.{c,h} when --target=c:flex is set.  The
    ** standard output above is unchanged whether or not the skin is
    ** requested.  See docs/SKINS.md. */
    extern int g_lime_skin_flex_flag;
    if (!err && g_lime_skin_flex_flag) {
        extern int lime_emit_c_skin_flex(const LimeLexCompiled *compiled,
                                         const LimeLexSpec *spec,
                                         const char *name_prefix,
                                         const char *const *rule_names,
                                         int n_rules,
                                         const char *out_h_path,
                                         const char *out_c_path,
                                         const char *base_id);
        /* Build <output_dir>/<stem>_flex.<ext> by hand: make_output_-
        ** path always inserts "_lex." before the suffix, but we
        ** want "_flex." -- one character longer, so an in-place
        ** rewrite of the cached "_lex." buffer would truncate. */
        size_t dn = output_dir ? strlen(output_dir) + 1 : 0;
        size_t sn = strlen(stem);
        size_t total = dn + sn + 5 /* "_flex" */ + 1 /* "." */ + 1 /* ext */ + 1;
        char *flex_h = (char *)malloc(total);
        char *flex_c = (char *)malloc(total);
        if (flex_h && flex_c) {
            flex_h[0] = 0;
            flex_c[0] = 0;
            if (output_dir) {
                strcat(flex_h, output_dir);
                strcat(flex_h, "/");
                strcat(flex_c, output_dir);
                strcat(flex_c, "/");
            }
            strcat(flex_h, stem);
            strcat(flex_h, "_flex.h");
            strcat(flex_c, stem);
            strcat(flex_c, "_flex.c");
            int frc = lime_emit_c_skin_flex(c, spec, prefix,
                                            (const char *const *)rule_names,
                                            n_rules,
                                            flex_h, flex_c, stem);
            if (frc != 0) {
                err = 1;
            } else {
                fprintf(stderr, "lime -X: wrote %s and %s (flex skin)\n",
                        flex_h, flex_c);
            }
        }
        free(flex_h);
        free(flex_c);
    }

    free(h_basename);
    for (int i = 0; i < n_rules; i++)
        free(rule_names[i]);
    free(rule_names);
    free(stem);
    free(h_path);
    free(c_path);
    return err ? 2 : 0;
}

int lime_lex_run_compiler(const char *input_path, const char *output_dir) {
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
        fprintf(stderr, "lime: %d parse error(s) in '%s'\n", spec->error_count, input_path);
        lime_lex_spec_free(spec);
        free(src);
        return 1;
    }

    int resolve_rc = lime_lex_resolve_patterns(spec);
    if (resolve_rc != 0 || spec->error_count > 0) {
        fprintf(stderr, "lime: %d resolve error(s) in '%s'\n", spec->error_count, input_path);
        lime_lex_spec_free(spec);
        free(src);
        return 1;
    }

    LimeLexCompiled *c = lime_lex_compile(spec);
    if (!c) {
        fprintf(stderr, "lime: compile returned NULL\n");
        lime_lex_spec_free(spec);
        free(src);
        return 2;
    }
    if (c->error_count > 0) {
        fprintf(stderr, "lime: %d compile error(s) in '%s'\n", c->error_count, input_path);
        lime_lex_compiled_free(c);
        lime_lex_spec_free(spec);
        free(src);
        return 1;
    }

    int emit_rc = emit_files(c, spec, input_path, output_dir);

    /* v0.8.x feat/rust-output stage 7: also emit a Rust mirror when
    ** --rustlex is set on the lime CLI.  The flag is exposed via a
    ** weak global so this translation unit doesn't need to know
    ** about lime.c's option-parser internals. */
    extern int g_lime_rustlex_flag;
    if (g_lime_rustlex_flag && emit_rc == 0) {
        extern int emit_rust_lex(const LimeLexSpec *spec,
                                  const LimeLexCompiled *compiled,
                                  const char *src_path,
                                  const char *out_path, char **error);
        /* Derive output path: <stem>_lex.rs alongside the .c/.h. */
        char *stem = strdup(input_path);
        if (stem) {
            /* Use only the basename's last `.` as the suffix split, so a
            ** dotted directory like /tmp/foo.d/json.lex still trims `.lex`
            ** rather than `.d/json.lex`. */
            char *bare = strrchr(stem, '/');
            bare = bare ? bare + 1 : stem;
            char *dot = strrchr(bare, '.');
            if (dot) *dot = 0;
            size_t plen = strlen(stem) + strlen("_lex_logos.rs") + 1;
            char *rust_path = (char *)malloc(plen);
            if (rust_path) {
                snprintf(rust_path, plen, "%s_lex.rs", stem);
                char *err = NULL;
                if (emit_rust_lex(spec, c, input_path, rust_path, &err) != 0) {
                    fprintf(stderr, "lime --rustlex: %s\n",
                            err ? err : "emit failed");
                    free(err);
                    emit_rc = 2;
                } else {
                    fprintf(stderr, "lime -X --rustlex: wrote %s\n", rust_path);
                }
                /* --target=rust:logos: also emit a sibling logos-API skin
                ** that wraps the just-written <stem>_lex.rs.  Skipped if
                ** the standard emit failed (the skin would dangle). */
                extern int g_lime_skin_logos_flag;
                if (g_lime_skin_logos_flag && emit_rc == 0) {
                    extern int lime_emit_rust_skin_logos(const LimeLexSpec *spec,
                                                         const char *src_path,
                                                         const char *out_path,
                                                         const char *lex_module_name,
                                                         char **error);
                    char *logos_path = (char *)malloc(plen);
                    if (logos_path) {
                        snprintf(logos_path, plen, "%s_lex_logos.rs", stem);
                        /* Derive the sibling lex module name from the basename:
                        ** /a/b/json -> json_lex.  The skin emits
                        ** `use super::<lex_module_name> as lime_lex;`. */
                        const char *bare2 = strrchr(stem, '/');
                        bare2 = bare2 ? bare2 + 1 : stem;
                        size_t mlen = strlen(bare2) + strlen("_lex") + 1;
                        char *mod_name = (char *)malloc(mlen);
                        if (mod_name) {
                            snprintf(mod_name, mlen, "%s_lex", bare2);
                            char *lerr = NULL;
                            if (lime_emit_rust_skin_logos(spec, input_path,
                                                          logos_path, mod_name,
                                                          &lerr) != 0) {
                                fprintf(stderr, "lime --target=rust:logos: %s\n",
                                        lerr ? lerr : "emit failed");
                                free(lerr);
                                emit_rc = 2;
                            } else {
                                fprintf(stderr,
                                  "lime -X --target=rust:logos: wrote %s\n",
                                  logos_path);
                            }
                            free(mod_name);
                        }
                        free(logos_path);
                    }
                }
                free(rust_path);
            }
            free(stem);
        }
    }

    lime_lex_compiled_free(c);
    lime_lex_spec_free(spec);
    free(src);
    return emit_rc;
}
