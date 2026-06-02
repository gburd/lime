/*
 * tests/test_emit_rust_safe.c -- coverage + behavioural regression
 * for the --enable=safe / --target=rust,unsafe feature flag (v0.9.3).
 *
 * The flag controls whether the Rust lexer emit wraps scalar DFA
 * dispatch loops in `unsafe { ... }` and uses `get_unchecked`
 * indexing.  Default ON for --target=rust (safe Rust), OFF when the
 * user opts out via:
 *   --target=rust,unsafe
 *   --disable=safe
 *
 * What this test asserts:
 *   1. The default `--target=rust -X` output contains ZERO
 *      `unsafe { while p < bytes.len() {` blocks (Cat-1 unsafe
 *      eliminated).  SIMD intrinsic helpers (Cat 2/3) are still
 *      `unsafe fn` -- those are forced by Rust language rules and
 *      stay regardless of the safe flag.
 *   2. `--target=rust,unsafe -X` brings back the unsafe wrappers
 *      (default v0.9.2 behaviour preserved as opt-in).
 *   3. `--disable=safe` is equivalent to `--target=rust,unsafe`.
 *   4. The default-safe Rust output compiles (we don't have rustc in
 *      every CI lane; the existing test_emit_rust_cargo path covers
 *      the cargo build and is not duplicated here -- this test is
 *      structural only, asserting unsafe-block presence/absence).
 *
 * Skipped at runtime when LIME_BIN isn't on $PATH.
 */

#include "test_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Minimal lex grammar covering all Cat-1 unsafe sites:
**   - scan_self_loop_*   (whitespace, identifier)
**   - main DFA dispatch loop (every rule traverses it)
**   - TokenIter::next loop (built unconditionally)
**
** The fixture is identical to the one used by test_emit_rust_lex,
** intentionally -- we want the same emit shape so the unsafe
** count comparison is meaningful. */
static const char *kLexFixture =
    "%name_prefix Safety.\n"
    "rule kw matches /[a-zA-Z_][a-zA-Z0-9_]*/ { /* */ }\n"
    "rule num matches /[0-9]+/ { /* */ }\n"
    "rule str matches /\"([^\"\\\\]|\\\\.)*\"/ { /* */ }\n"
    "rule ws matches /[ \\t\\n\\r]+/ { /* */ }\n"
    "rule lp matches /\\(/ { /* */ }\n"
    "rule rp matches /\\)/ { /* */ }\n";

static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

/* Read whole file into a heap-allocated NUL-terminated buffer.
** Caller frees.  Returns NULL on any I/O failure. */
static char *slurp(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    rewind(f);
    char *buf = (char *)malloc((size_t)sz + 1);
    if (buf == NULL) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) { free(buf); return NULL; }
    buf[sz] = 0;
    if (out_len) *out_len = (size_t)sz;
    return buf;
}

/* Count occurrences of needle (literal) in haystack. */
static int count_substr(const char *hay, const char *needle) {
    int n = 0;
    size_t need_len = strlen(needle);
    if (need_len == 0) return 0;
    const char *p = hay;
    while ((p = strstr(p, needle)) != NULL) {
        n++;
        p += need_len;
    }
    return n;
}

/* Run lime with the given target/enable flags; return 0 on success. */
static int run_lime(const char *lime_bin, const char *target_arg,
                    const char *extra_arg, const char *fixture) {
    char *argv[8] = { (char *)lime_bin, "-X", "-d.", (char *)target_arg };
    int n = 4;
    if (extra_arg) argv[n++] = (char *)extra_arg;
    argv[n++] = (char *)fixture;
    argv[n] = NULL;
    int rc = 0;
    if (test_compat_run(argv, &rc) != 0) {
        fprintf(stderr, "  spawn failed for target=%s extra=%s\n",
                target_arg, extra_arg ? extra_arg : "(none)");
        return -1;
    }
    return rc;
}

int main(int argc, char **argv) {
    const char *lime_bin = (argc > 1) ? argv[1] : getenv("LIME_BIN");
    if (lime_bin == NULL || lime_bin[0] == 0) {
        fprintf(stderr, "SKIP: no LIME_BIN\n");
        return 77;
    }

    char tmpdir[256];
    if (test_compat_tmpdir("lime_emit_rust_safe", tmpdir, sizeof(tmpdir)) != 0) {
        fprintf(stderr, "FAIL: tmpdir\n");
        return 1;
    }
    if (chdir(tmpdir) != 0) { perror("chdir"); return 1; }

    FILE *f = fopen("safety.lex", "wb");
    if (f == NULL) { perror("fopen"); return 1; }
    fputs(kLexFixture, f);
    fclose(f);

    const char *out_path = "safety_lex.rs";
    int total = 0, pass = 0;

    /* ------------------------------------------------------------------
    ** Variant 1: default --target=rust  -- expect SAFE (no unsafe { while)
    ** ------------------------------------------------------------------ */
    total++;
    unlink(out_path);
    int rc = run_lime(lime_bin, "--target=rust", NULL, "safety.lex");
    if (rc != 0 || !file_exists(out_path)) {
        printf("  FAIL: --target=rust default emit rc=%d file=%s\n",
               rc, file_exists(out_path) ? "present" : "missing");
    } else {
        size_t len = 0;
        char *body = slurp(out_path, &len);
        if (body == NULL) {
            printf("  FAIL: slurp %s\n", out_path);
        } else {
            int unsafe_while = count_substr(body, "unsafe {\n");
            /* Some `unsafe {` openings remain for SIMD/target_feature
            ** dispatch (return unsafe { self.tokenize_avx2(bytes) };
            ** etc.) -- those are Cat 3 and IRREDUCIBLE.  We assert the
            ** Cat-1 shape `unsafe {\n` followed by `while p` is GONE. */
            int dfa_loop_unsafe = 0;
            const char *p = body;
            while ((p = strstr(p, "unsafe {\n")) != NULL) {
                /* Look at the next line; if it begins (ignoring indent)
                ** with `while p < bytes.len()` or `while q < n` or
                ** `while p < self.bytes.len()`, it's a Cat-1 DFA loop. */
                const char *line = strchr(p, '\n');
                if (line) {
                    line++;
                    while (*line == ' ' || *line == '\t') line++;
                    if (strncmp(line, "while p < bytes.len()", 21) == 0
                     || strncmp(line, "while q < n",          11) == 0
                     || strncmp(line, "while p < self.bytes.len()", 26) == 0) {
                        dfa_loop_unsafe++;
                    }
                }
                p += strlen("unsafe {\n");
            }
            int get_unchecked = count_substr(body, "get_unchecked");
            /* The default-safe emit MUST have zero Cat-1 DFA-loop
            ** unsafe wrappers.  get_unchecked may still appear inside
            ** SIMD scan_avx2 / scan_neon helpers (Cat 2) -- those are
            ** counted but not asserted-zero. */
            if (dfa_loop_unsafe == 0) {
                printf("  PASS: --target=rust default has 0 DFA-loop "
                       "unsafe wrappers (%d total `unsafe {`, "
                       "%d get_unchecked)\n",
                       unsafe_while, get_unchecked);
                pass++;
            } else {
                printf("  FAIL: --target=rust default has %d DFA-loop "
                       "unsafe wrappers (expected 0)\n", dfa_loop_unsafe);
            }
            free(body);
        }
    }

    /* ------------------------------------------------------------------
    ** Variant 2: --target=rust,unsafe  -- expect Cat-1 unsafe RESTORED
    ** ------------------------------------------------------------------ */
    total++;
    unlink(out_path);
    rc = run_lime(lime_bin, "--target=rust,unsafe", NULL, "safety.lex");
    if (rc != 0 || !file_exists(out_path)) {
        printf("  FAIL: --target=rust,unsafe rc=%d file=%s\n",
               rc, file_exists(out_path) ? "present" : "missing");
    } else {
        char *body = slurp(out_path, NULL);
        if (body == NULL) {
            printf("  FAIL: slurp %s\n", out_path);
        } else {
            int dfa_loop_unsafe = 0;
            const char *p = body;
            while ((p = strstr(p, "unsafe {\n")) != NULL) {
                const char *line = strchr(p, '\n');
                if (line) {
                    line++;
                    while (*line == ' ' || *line == '\t') line++;
                    if (strncmp(line, "while p < bytes.len()", 21) == 0
                     || strncmp(line, "while q < n",          11) == 0
                     || strncmp(line, "while p < self.bytes.len()", 26) == 0) {
                        dfa_loop_unsafe++;
                    }
                }
                p += strlen("unsafe {\n");
            }
            /* Default emit has at least 2 DFA-loop unsafe blocks:
            ** the simd-mode tokenize_impl loop and TokenIter::next.
            ** (Plain non-simd `tokenize` is gated on !simd, which is
            ** the default-on simd flag for --target=rust.) */
            if (dfa_loop_unsafe >= 2) {
                printf("  PASS: --target=rust,unsafe has %d DFA-loop "
                       "unsafe wrappers (>=2 expected)\n", dfa_loop_unsafe);
                pass++;
            } else {
                printf("  FAIL: --target=rust,unsafe has %d DFA-loop "
                       "unsafe wrappers (>=2 expected)\n", dfa_loop_unsafe);
            }
            free(body);
        }
    }

    /* ------------------------------------------------------------------
    ** Variant 3: --target=rust --disable=safe  -- equivalent to ,unsafe
    ** ------------------------------------------------------------------ */
    total++;
    unlink(out_path);
    rc = run_lime(lime_bin, "--target=rust", "--disable=safe", "safety.lex");
    if (rc != 0 || !file_exists(out_path)) {
        printf("  FAIL: --disable=safe rc=%d file=%s\n",
               rc, file_exists(out_path) ? "present" : "missing");
    } else {
        char *body = slurp(out_path, NULL);
        if (body == NULL) {
            printf("  FAIL: slurp %s\n", out_path);
        } else {
            int dfa_loop_unsafe = 0;
            const char *p = body;
            while ((p = strstr(p, "unsafe {\n")) != NULL) {
                const char *line = strchr(p, '\n');
                if (line) {
                    line++;
                    while (*line == ' ' || *line == '\t') line++;
                    if (strncmp(line, "while p < bytes.len()", 21) == 0
                     || strncmp(line, "while q < n",          11) == 0
                     || strncmp(line, "while p < self.bytes.len()", 26) == 0) {
                        dfa_loop_unsafe++;
                    }
                }
                p += strlen("unsafe {\n");
            }
            if (dfa_loop_unsafe >= 2) {
                printf("  PASS: --disable=safe has %d DFA-loop "
                       "unsafe wrappers (alias for ,unsafe)\n", dfa_loop_unsafe);
                pass++;
            } else {
                printf("  FAIL: --disable=safe has %d DFA-loop "
                       "unsafe wrappers (>=2 expected)\n", dfa_loop_unsafe);
            }
            free(body);
        }
    }

    /* ------------------------------------------------------------------
    ** Variant 4: --target=rust --enable=per-token-dfa (default safe)
    ** -- per-rule DFA loops should also drop their unsafe { } wrappers
    ** ------------------------------------------------------------------ */
    total++;
    unlink(out_path);
    rc = run_lime(lime_bin, "--target=rust", "--enable=per-token-dfa",
                  "safety.lex");
    if (rc != 0 || !file_exists(out_path)) {
        printf("  FAIL: --enable=per-token-dfa rc=%d file=%s\n",
               rc, file_exists(out_path) ? "present" : "missing");
    } else {
        char *body = slurp(out_path, NULL);
        if (body == NULL) {
            printf("  FAIL: slurp %s\n", out_path);
        } else {
            int dfa_loop_unsafe = 0;
            const char *p = body;
            while ((p = strstr(p, "unsafe {\n")) != NULL) {
                const char *line = strchr(p, '\n');
                if (line) {
                    line++;
                    while (*line == ' ' || *line == '\t') line++;
                    if (strncmp(line, "while p < bytes.len()", 21) == 0
                     || strncmp(line, "while q < n",          11) == 0
                     || strncmp(line, "while p < self.bytes.len()", 26) == 0) {
                        dfa_loop_unsafe++;
                    }
                }
                p += strlen("unsafe {\n");
            }
            /* Per-token-dfa adds per-rule walks (`while q < n`); under
            ** the default safe flag those should also be unwrapped. */
            if (dfa_loop_unsafe == 0) {
                printf("  PASS: --enable=per-token-dfa default safe has 0 "
                       "DFA-loop unsafe wrappers\n");
                pass++;
            } else {
                printf("  FAIL: --enable=per-token-dfa default safe has %d "
                       "DFA-loop unsafe wrappers (expected 0)\n",
                       dfa_loop_unsafe);
            }
            free(body);
        }
    }

    test_compat_chdir_temp();
    test_compat_rmdir_recursive(tmpdir);

    printf("\nResults: %d/%d passed\n", pass, total);
    return pass == total ? 0 : 1;
}
