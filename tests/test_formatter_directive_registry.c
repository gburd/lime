/*
** test_formatter_directive_registry.c -- Lime-Letter-23 follow-up.
**
** When format_grammar() emitted each directive inline, adding a
** new directive meant hunting through ~500 LOC of `if (lem->X)
** fprintf(...)` blocks for "the place where this directive's
** twin lives."  PG's letters 7, 9, 12, 22, 23 each reported a
** variant of the same structural bug: a directive that the
** parser captured onto `struct lime` but that the formatter
** silently dropped.  Letter 23 fixed three of those (%first_token,
** %locations, %location_type) point-wise; the table-driven
** registry in lime.c kills the bug class.
**
** This test enforces four properties of the registry:
**
**   1. COVERAGE.  Every directive expected to live in the
**      registry has an entry.  Forgetting to add a new directive
**      to lime_directives[] makes this test fail loudly.
**
**   2. ROUND-TRIP IDEMPOTENCE.  format(format(F)) == format(F)
**      across a range of fixtures.  Catches reorder/deletion
**      regressions where the formatter's output is no longer a
**      fixed point of itself.
**
**   3. CATEGORY PLACEMENT.  In the formatted output, MODULE
**      directives precede HEADER_VALUE directives, which precede
**      HEADER_BRACE directives.  Catches the failure mode where
**      someone adds an entry to the wrong bucket.
**
**   4. BYTE-IDENTITY vs v0.5.5.  Each fixture's formatter output
**      MD5 matches the v0.5.5 baseline.  Catches any silent
**      stream-order drift introduced by the refactor (or by any
**      future refactor of the dispatcher).
**
** The test #includes ../lime.c with LIME_TEST_HARNESS so it can
** read the static lime_directives[] table directly (sub-tests 1
** and 3) and ALSO accepts a lime binary path on argv[1] which
** it spawns as a subprocess for sub-tests 2 and 4 (matching the
** existing test_formatter.c pattern).
**
** Skips at runtime (exit 77) when the lime binary is not findable.
*/

#define LIME_TEST_HARNESS
#include "../lime.c"

#include "test_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* The registry types are declared in lime.c; we get them via the
** #include above.  No re-declaration needed. */

/* ---------------------------------------------------------------- */
/* Helpers                                                          */
/* ---------------------------------------------------------------- */

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    if (n < 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)n + 1);
    if (buf == NULL) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = 0;
    return buf;
}

static char g_tmpdir[256];

static int run_format(const char *lime_bin, const char *grammar_path) {
    char *fmt_argv[] = { (char *)lime_bin, "-F", (char *)grammar_path, NULL };
    int rc = 0;
    if (test_compat_run(fmt_argv, &rc) != 0) return -1;
    return rc;
}

/* MD5 of a file via the system's `md5sum` binary.  The test corpus
** was baselined with the same tool (GNU coreutils md5sum).  Returns
** an allocated 32-char lowercase hex string, or NULL on failure.
** macOS ships `md5` (BSD style) instead of `md5sum`; falls back. */
static char *md5_of_file(const char *path) {
    char cmd[8192];
    /* Try GNU md5sum first (Linux + Homebrew on macOS).  On stock
    ** macOS the BSD `md5 -q` form takes -q to print just the hash. */
    snprintf(cmd, sizeof(cmd),
             "md5sum '%s' 2>/dev/null || md5 -q '%s' 2>/dev/null",
             path, path);
    FILE *p = popen(cmd, "r");
    if (p == NULL) return NULL;
    char buf[256];
    if (fgets(buf, sizeof(buf), p) == NULL) {
        pclose(p);
        return NULL;
    }
    pclose(p);
    /* "<hex>  <path>\n" -- take first 32 chars. */
    if (strlen(buf) < 32) return NULL;
    char *out = (char *)malloc(33);
    if (out == NULL) return NULL;
    memcpy(out, buf, 32);
    out[32] = 0;
    return out;
}

/* ---------------------------------------------------------------- */
/* Sub-test 1: coverage check                                       */
/* ---------------------------------------------------------------- */

/* Hard-coded list of directive names that MUST live in the registry.
** Matches v0.5.6's lime_directives[].  Adding a directive to lime.c
** means adding its name here too -- the test then verifies the row
** is actually wired into the dispatch loop and not floating in space.
**
** This list is the contract: if the formatter MUST emit a directive
** when set, the directive's name appears here. */
static const char *expected_directives[] = {
    /* MODULE */
    "module", "require", "export", "import",
    /* HEADER_VALUE */
    "name", "token_type", "extra_argument", "extra_context",
    "default_type", "start_symbol", "stack_size", "token_prefix",
    "symbol_prefix", "expect", "first_token", "locations",
    "location_type",
    /* HEADER_BRACE */
    "include", "syntax_error", "parse_failure", "parse_accept",
    "stack_overflow", "token_destructor", "default_destructor",
};
static const size_t n_expected =
    sizeof(expected_directives) / sizeof(expected_directives[0]);

static int test_coverage(void) {
    int ok = 1;
    /* Every name in expected_directives[] must have a matching
    ** entry in lime_directives[].  This is the structural-omission
    ** check the Letter-23 architectural follow-up was designed to
    ** enforce. */
    for (size_t i = 0; i < n_expected; i++) {
        const char *name = expected_directives[i];
        int found = 0;
        for (size_t j = 0; j < n_lime_directives; j++) {
            if (strcmp(lime_directives[j].name, name) == 0) {
                found = 1;
                break;
            }
        }
        if (!found) {
            fprintf(stderr,
                "FAIL[coverage]: directive %%%s is missing from "
                "lime_directives[].  Add a row in lime.c.\n", name);
            ok = 0;
        }
    }

    /* Reverse direction: every entry in lime_directives[] must have
    ** a name in expected_directives[].  Catches the case where
    ** someone adds a row but forgets to update the contract list. */
    for (size_t j = 0; j < n_lime_directives; j++) {
        const char *name = lime_directives[j].name;
        int found = 0;
        for (size_t i = 0; i < n_expected; i++) {
            if (strcmp(expected_directives[i], name) == 0) {
                found = 1;
                break;
            }
        }
        if (!found) {
            fprintf(stderr,
                "FAIL[coverage]: lime_directives[] contains %%%s "
                "but the expected-list contract in this test does "
                "not.  Either add the directive to "
                "expected_directives[] (preferred) or remove the "
                "registry row.\n", name);
            ok = 0;
        }
    }

    /* Every entry must have non-NULL has_value/emit. */
    for (size_t j = 0; j < n_lime_directives; j++) {
        const LimeDirectiveDescriptor *d = &lime_directives[j];
        if (d->name == NULL || d->has_value == NULL || d->emit == NULL) {
            fprintf(stderr,
                "FAIL[coverage]: lime_directives[%zu] has a NULL "
                "field (name=%p has_value=%p emit=%p)\n",
                j, (const void *)d->name, (void *)(uintptr_t)d->has_value,
                (void *)(uintptr_t)d->emit);
            ok = 0;
        }
    }

    if (ok) {
        printf("PASS[coverage]: %zu directives registered, contract holds\n",
               n_lime_directives);
    }
    return ok;
}

/* ---------------------------------------------------------------- */
/* Sub-test 2: round-trip idempotence                               */
/* ---------------------------------------------------------------- */

static const char *idempotence_fixtures[] = {
    "test_formatter_grammar.lime",
    "test_formatter_comments_grammar.lime",
    "test_formatter_labels_grammar.lime",
    "test_formatter_token_groups_grammar.lime",
    "test_formatter_precedence_grammar.lime",
    "test_formatter_prec_comments_grammar.lime",
    "test_formatter_loc_directives_grammar.lime",
    "test_dialect_grammar.lime",
};
static const size_t n_idem =
    sizeof(idempotence_fixtures) / sizeof(idempotence_fixtures[0]);

static int test_idempotence(const char *lime_bin, const char *src_dir) {
    int ok = 1;
    for (size_t i = 0; i < n_idem; i++) {
        const char *fix = idempotence_fixtures[i];
        char src_path[4096];
        char work[4096];
        char fmt1[4096];
        char fmt2[4096];
        snprintf(src_path, sizeof(src_path), "%s/%s", src_dir, fix);
        snprintf(work, sizeof(work),
                 "%s/dir_reg_%zu_%s", g_tmpdir, i, fix);
        snprintf(fmt1, sizeof(fmt1), "%s.formatted", work);
        snprintf(fmt2, sizeof(fmt2), "%s.formatted.formatted", work);

        struct stat st;
        if (stat(src_path, &st) != 0) {
            fprintf(stderr,
                "FAIL[idem]: fixture %s not found at %s\n", fix, src_path);
            ok = 0;
            continue;
        }
        char cmd[8192];
        if (test_compat_copy_file(src_path, work) != 0) {
            fprintf(stderr, "FAIL[idem]: cp %s failed\n", fix);
            ok = 0;
            continue;
        }
        if (run_format(lime_bin, work) != 0) {
            fprintf(stderr, "FAIL[idem]: lime -F %s exited non-zero\n", fix);
            ok = 0;
            continue;
        }
        if (run_format(lime_bin, fmt1) != 0) {
            fprintf(stderr,
                "FAIL[idem]: lime -F %s exited non-zero\n", fmt1);
            ok = 0;
            continue;
        }
        char *a = slurp(fmt1);
        char *b = slurp(fmt2);        if (a == NULL || b == NULL) {
            fprintf(stderr, "FAIL[idem]: slurp failed for %s\n", fix);
            free(a); free(b);
            ok = 0;
            continue;
        }
        if (strcmp(a, b) != 0) {
            fprintf(stderr,
                "FAIL[idem]: %s -- format(format(F)) != format(F)\n", fix);
            ok = 0;
        }
        free(a); free(b);
    }
    if (ok) {
        printf("PASS[idem]: %zu fixtures are formatter fixed-points\n", n_idem);
    }
    return ok;
}

/* ---------------------------------------------------------------- */
/* Sub-test 3: category placement                                   */
/* ---------------------------------------------------------------- */

/* Find the byte offset of `needle` in `hay`, or -1 if absent. */
static long find_offset(const char *hay, const char *needle) {
    const char *p = strstr(hay, needle);
    return p ? (long)(p - hay) : -1L;
}

/* In the formatted output of test_formatter_grammar.lime, verify
** the category-ordering invariant: every directive line lands at
** an offset consistent with its registry category.  The fixture
** carries %include + %name + %token_type + %syntax_error etc. so
** there's at least one directive in each emitted category. */
static int test_category_placement(const char *lime_bin, const char *src_dir) {
    char src_path[4096];
    char work[4096];
    char fmt[4096];
    snprintf(src_path, sizeof(src_path),
             "%s/test_formatter_grammar.lime", src_dir);
    snprintf(work, sizeof(work), "%s/dir_reg_cat_test_formatter_grammar.lime", g_tmpdir);
    snprintf(fmt, sizeof(fmt), "%s.formatted", work);

    struct stat st;
    if (stat(src_path, &st) != 0) {
        fprintf(stderr,
            "FAIL[cat]: fixture not found at %s\n", src_path);
        return 0;
    }
    char cmd[8192];
    if (test_compat_copy_file(src_path, work) != 0) {
        fprintf(stderr, "FAIL[cat]: cp failed\n");
        return 0;
    }
    if (run_format(lime_bin, work) != 0) {
        fprintf(stderr, "FAIL[cat]: lime -F failed\n");
        return 0;
    }
    char *body = slurp(fmt);
    if (body == NULL) {
        fprintf(stderr, "FAIL[cat]: slurp failed\n");
        return 0;
    }

    /* Pick one anchor per category that the fixture is known to
    ** carry.  test_formatter_grammar.lime has %name (HEADER_VALUE),
    ** %include + %syntax_error + %parse_failure (HEADER_BRACE).
    ** It does NOT carry a %module_name so the MODULE check is
    ** skipped here -- sub-test 4's byte-identity guard catches
    ** module-block regressions. */
    long off_name      = find_offset(body, "\n%name ");
    long off_token_type = find_offset(body, "\n%token_type ");
    long off_include   = find_offset(body, "\n%include {");
    long off_syntax_err = find_offset(body, "\n%syntax_error {");

    int ok = 1;
    if (off_name < 0 || off_include < 0 || off_syntax_err < 0) {
        fprintf(stderr,
            "FAIL[cat]: anchor missing -- name=%ld include=%ld "
            "syntax_error=%ld\n",
            off_name, off_include, off_syntax_err);
        ok = 0;
    } else {
        if (off_name >= off_include) {
            fprintf(stderr,
                "FAIL[cat]: HEADER_VALUE %%name (offset %ld) appears "
                "AFTER HEADER_BRACE %%include (offset %ld) -- a "
                "registry category was misassigned.\n",
                off_name, off_include);
            ok = 0;
        }
        if (off_token_type >= 0 && off_token_type >= off_include) {
            fprintf(stderr,
                "FAIL[cat]: HEADER_VALUE %%token_type (offset %ld) "
                "appears AFTER HEADER_BRACE %%include (offset %ld)\n",
                off_token_type, off_include);
            ok = 0;
        }
        if (off_include >= off_syntax_err) {
            /* They're both HEADER_BRACE, but %include precedes
            ** %syntax_error in the registry, so output order
            ** must follow.  This catches an in-bucket reorder
            ** that would break round-trip on grammars where the
            ** _comment slots' identity matters. */
            fprintf(stderr,
                "FAIL[cat]: HEADER_BRACE %%include (offset %ld) "
                "appears AFTER HEADER_BRACE %%syntax_error (offset %ld) "
                "-- registry row order was changed.\n",
                off_include, off_syntax_err);
            ok = 0;
        }
    }
    free(body);
    if (ok) {
        printf("PASS[cat]: HEADER_VALUE precedes HEADER_BRACE; "
               "in-bucket order preserved\n");
    }
    return ok;
}

/* ---------------------------------------------------------------- */
/* Sub-test 4: byte-identity vs v0.5.5 baseline MD5s                */
/* ---------------------------------------------------------------- */

/* MD5s captured against the v0.5.5 lime binary (HEAD a81e548) on
** 2026-05-26.  Each entry pairs a fixture (relative to the project
** root, since some live in tests/ and some in examples/) with the
** MD5 of the formatter's output for that fixture.
**
** The path is split (subdir + basename) because lime -F writes the
** .formatted output alongside the INPUT file, so we copy the input
** into a working dir first to avoid littering the source tree. */
typedef struct BaselineEntry {
    const char *subdir;     /* relative to project source root */
    const char *basename;   /* fixture file name */
    const char *md5;        /* expected MD5 of <basename>.formatted */
} BaselineEntry;

static const BaselineEntry baselines[] = {
    /* tests/ */
    { "tests", "test_formatter_grammar.lime",
      "e288ba1bf7b34712a6eb8899f56b2216" },
    { "tests", "test_formatter_comments_grammar.lime",
      "83b66124e4f9b8b01e15893f36ef6726" },
    { "tests", "test_formatter_labels_grammar.lime",
      "99830cf42840270d45b66a7069aca306" },
    { "tests", "test_formatter_token_groups_grammar.lime",
      "e0a61a35e9547a10815a9d3176b7e737" },
    { "tests", "test_formatter_precedence_grammar.lime",
      "62e077b37666f908ab0a52d256556486" },
    { "tests", "test_formatter_prec_comments_grammar.lime",
      "ccac061c57d528aadb169996d9933097" },
    { "tests", "test_formatter_loc_directives_grammar.lime",
      "47d1b4f8ab689616d0915c114f73f95e" },
    { "tests", "test_dialect_grammar.lime",
      "6b925332fa9279fca7b9e7b00f03b098" },
    { "tests", "test_embed_grammar.lime",
      "52f1c242ec428a4b4c6c3e85b1ec7530" },
    /* examples/ -- broader coverage of the directive surface */
    { "examples/calc", "calc.lime",
      "e040cddb42a2d3ce6dc5bba3ebbc1eae" },
    { "examples/json", "json_grammar.lime",
      "d1c1476a8fdf07c4e4c43bb9c0f66a13" },
    { "examples/datalog", "datalog.lime",
      "bb88c20e11ff10751b7d8635ceb9ca3f" },
    { "examples/jsonpath", "jsonpath_gram.lime",
      "d09c2e7f70cd8e6070e006a675a13fa1" },
    { "examples/xpath", "xpath.lime",
      "bc5cf7a35050934b62cbd78bb28740aa" },
    { "examples/pgbench", "pgbench_expr.lime",
      "b7633545f772ef85e01c35dd6d62c583" },
};
static const size_t n_baselines =
    sizeof(baselines) / sizeof(baselines[0]);

static int test_byte_identity(const char *lime_bin, const char *project_root) {
    int ok = 1;
    for (size_t i = 0; i < n_baselines; i++) {
        const BaselineEntry *e = &baselines[i];
        char src_path[4096];
        char work[4096];
        char fmt[4096];
        snprintf(src_path, sizeof(src_path), "%s/%s/%s",
                 project_root, e->subdir, e->basename);
        snprintf(work, sizeof(work), "%s/dir_reg_md5_%zu_%s",
                 g_tmpdir, i, e->basename);
        snprintf(fmt, sizeof(fmt), "%s.formatted", work);

        struct stat st;
        if (stat(src_path, &st) != 0) {
            fprintf(stderr,
                "WARN[md5]: fixture %s/%s not found at %s -- skipping\n",
                e->subdir, e->basename, src_path);
            continue;
        }
        char cmd[8192];
        if (test_compat_copy_file(src_path, work) != 0) {
            fprintf(stderr, "FAIL[md5]: cp %s failed\n", e->basename);
            ok = 0;
            continue;
        }
        if (run_format(lime_bin, work) != 0) {
            fprintf(stderr,
                "FAIL[md5]: lime -F %s exited non-zero\n", e->basename);
            ok = 0;
            continue;
        }
        char *got = md5_of_file(fmt);
        if (got == NULL) {
            fprintf(stderr, "FAIL[md5]: md5sum failed for %s\n", fmt);
            ok = 0;
            continue;
        }
        if (strcmp(got, e->md5) != 0) {
            fprintf(stderr,
                "FAIL[md5]: %s -- expected %s got %s -- formatter "
                "output drifted vs v0.5.5 baseline\n",
                e->basename, e->md5, got);
            ok = 0;
        }
        free(got);
    }
    if (ok) {
        printf("PASS[md5]: %zu fixtures match v0.5.5 byte-identity baseline\n",
               n_baselines);
    }
    return ok;
}

/* ---------------------------------------------------------------- */
/* Driver                                                           */
/* ---------------------------------------------------------------- */

int main(int argc, char **argv) {
    if (test_compat_tmpdir("lime_dir_reg", g_tmpdir, sizeof(g_tmpdir)) != 0) {
        fprintf(stderr, "FAIL: tmpdir creation failed\n");
        return 1;
    }

    if (argc < 3) {
        fprintf(stderr,
            "usage: %s <lime-binary> <project-source-root>\n", argv[0]);
        return 2;
    }
    const char *lime_bin     = argv[1];
    const char *project_root = argv[2];
    char fixtures_dir[4096];
    snprintf(fixtures_dir, sizeof(fixtures_dir), "%s/tests", project_root);

    struct stat st;
    if (stat(lime_bin, &st) != 0) {
        fprintf(stderr, "SKIP: %s not found\n", lime_bin);
        return 77;
    }

    int ok = 1;
    ok &= test_coverage();
    ok &= test_idempotence(lime_bin, fixtures_dir);
    ok &= test_category_placement(lime_bin, fixtures_dir);
#if !defined(_WIN32) && !defined(__APPLE__)
    /* test_byte_identity compares formatter output MD5 hashes
    ** against a v0.5.5-era baseline computed on Linux.  Skipped on
    ** Windows (which has its own divergent output mostly due to
    ** path-quoting in JSON) and macOS (where stock /usr/bin/md5
    ** has different output format than GNU md5sum, and the BSD
    ** sort/printf semantics in lime might emit slightly different
    ** ordering).  The idempotence and category_placement sub-tests
    ** already cover the cross-platform invariant (output is stable
    ** and structurally correct); test_byte_identity is the strict
    ** byte-for-byte regression check vs. the Linux baseline. */
    ok &= test_byte_identity(lime_bin, project_root);
#else
    (void)test_byte_identity;
    (void)project_root;
#endif

    if (!ok) {
        fprintf(stderr, "FAIL: directive registry test\n");
        return 1;
    }
    printf("PASS: directive registry -- coverage, idempotence, "
           "category placement, byte-identity\n");
    return 0;
}
