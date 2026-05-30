/*
** test_formatter_token_groups.c -- regression test for Lime-Letter-21
** (PG team v0.3.5 closeout).
**
** v0.3.4 emitted %token / %type sections in symbol-index order
** with no comment preservation -- PG's gram.lime lost ~120-150
** lines across 12 keyword-section banners + 5 type-block banners
** on every `lime -F` round-trip.  PG accepted the cheap variant:
** capture one leading-comment slot per RUN of contiguous same-kind
** directives.  Inter-symbol comments INSIDE a run are dropped.
**
** This driver runs `lime -F` twice on the fixture and asserts:
**
**   1. Header survives (regression guard for v0.3.2 work).
**   2. First %token group banner survives.
**   3. Second %token group banner survives.
**   4. Third %token group banner survives.
**   5. First %type  group banner survives.
**   6. Second %type group banner survives.
**   7. Symbols emit in declaration-order groups, not index-order
**      shuffle (banners appear in source order; ICONST precedes
**      IDENT precedes ABORT_P; stmt precedes stmt_list).
**   8. Idempotence: format(format(F)) == format(F), byte-equal.
**
** Skips at runtime (exit 77) when the lime binary is not findable
** at the expected meson path.
*/

#include "test_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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

static int contains(const char *hay, const char *needle, const char *tag) {
    if (strstr(hay, needle) == NULL) {
        fprintf(stderr, "FAIL: %s -- did not find marker `%s`\n",
                tag, needle);
        return 0;
    }
    return 1;
}

/* Assert that `before` appears strictly before `after` in `hay`.
** Used to verify declaration-order emit (the third group's banner
** should come after the second's, etc.). */
static int ordered(const char *hay, const char *before, const char *after,
                   const char *tag) {
    const char *pb = strstr(hay, before);
    const char *pa = strstr(hay, after);
    if (pb == NULL) {
        fprintf(stderr, "FAIL: %s -- `before` marker `%s` not found\n",
                tag, before);
        return 0;
    }
    if (pa == NULL) {
        fprintf(stderr, "FAIL: %s -- `after` marker `%s` not found\n",
                tag, after);
        return 0;
    }
    if (pb >= pa) {
        fprintf(stderr,
                "FAIL: %s -- `%s` should precede `%s` but does not\n",
                tag, before, after);
        return 0;
    }
    return 1;
}

/* Run `<lime> -F <path>` and return the slurped <path>.formatted. */
static char *format_once(const char *lime_bin, const char *path) {
    char *fmt_argv[] = { (char *)lime_bin, "-F", (char *)path, NULL };
    int rc = 0;
    if (test_compat_run(fmt_argv, &rc) != 0 || rc != 0) {
        fprintf(stderr, "FAIL: lime -F %s exited non-zero\n", path);
        return NULL;
    }
    char outpath[2048];
    snprintf(outpath, sizeof(outpath), "%s.formatted", path);
    char *got = slurp(outpath);
    if (got == NULL) {
        fprintf(stderr, "FAIL: could not read %s\n", outpath);
    }
    return got;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr,
                "usage: %s <lime-binary> <test-grammar.lime>\n", argv[0]);
        return 2;
    }
    const char *lime_bin = argv[1];
    const char *fixture  = argv[2];

    struct stat st;
    if (stat(lime_bin, &st) != 0) {
        fprintf(stderr, "SKIP: %s not found\n", lime_bin);
        return 77;
    }

    char tmpdir[256];
    if (test_compat_tmpdir("lime_fmttg", tmpdir, sizeof(tmpdir)) != 0) {
        fprintf(stderr, "FAIL: tmpdir creation failed\n");
        return 1;
    }
    char work[512], fmt1[512];
    snprintf(work, sizeof(work), "%s/input.lime", tmpdir);
    snprintf(fmt1, sizeof(fmt1), "%s/input.lime.formatted", tmpdir);

    if (test_compat_copy_file(fixture, work) != 0) {
        fprintf(stderr, "FAIL: could not copy fixture to %s\n", work);
        return 1;
    }

    char *first = format_once(lime_bin, work);
    if (first == NULL) return 1;

    int ok = 1;

    /* Sub-test 1: header preservation (v0.3.2 regression guard). */
    ok &= contains(first,
        "test_formatter_token_groups_grammar.lime -- regression fixture",
        "1. header preservation -- fixture identification line");
    ok &= contains(first,
        "Lime-Letter-21 (PostgreSQL migration team) v0.3.5",
        "1. header preservation -- letter reference");

    /* Sub-test 2: first %token group banner survives. */
    ok &= contains(first,
        "First section: numeric literals",
        "2. %token group 1 banner -- survives round-trip");

    /* Sub-test 3: second %token group banner survives. */
    ok &= contains(first,
        "Second section: string literals (block separator, banner B)",
        "3. %token group 2 banner -- survives round-trip");

    /* Sub-test 4: third %token group banner survives. */
    ok &= contains(first,
        "Third section: keywords -- single directive, multiple names",
        "4. %token group 3 banner -- survives round-trip");

    /* Sub-test 5: first %type group banner survives. */
    ok &= contains(first,
        "Productions returning Node-tree shapes (banner T1)",
        "5. %type group 1 banner -- survives round-trip");

    /* Sub-test 6: second %type group banner survives. */
    ok &= contains(first,
        "Productions returning Lists (banner T2)",
        "6. %type group 2 banner -- survives round-trip");

    /* Sub-test 7: declaration-order emit, not symbol-index shuffle.
    ** Symbol-index order would interleave alphabetically (ABORT_P,
    ** ABSENT, ABSOLUTE_P, FCONST, ICONST, IDENT, PARAM); we want
    ** the source's group order (ICONST/PARAM, IDENT/FCONST,
    ** keywords).  Assert via banner ordering -- if banners stay
    ** in source order, the symbols between them must too. */
    ok &= ordered(first,
        "First section: numeric literals",
        "Second section: string literals",
        "7a. group 1 banner precedes group 2 banner");
    ok &= ordered(first,
        "Second section: string literals",
        "Third section: keywords",
        "7b. group 2 banner precedes group 3 banner");
    ok &= ordered(first,
        "Productions returning Node-tree shapes",
        "Productions returning Lists",
        "7c. type group 1 banner precedes type group 2 banner");
    /* And cross-section: every %token group banner precedes every
    ** %type group banner (the formatter emits all %token before
    ** any %type). */
    ok &= ordered(first,
        "Third section: keywords",
        "Productions returning Node-tree shapes",
        "7d. last %token banner precedes first %type banner");
    /* Symbol-level ordering check: ICONST line precedes IDENT line
    ** precedes ABORT_P line.  If groups were emitted in symbol-
    ** index (alphabetical) order instead, ABORT_P would come first. */
    ok &= ordered(first, "%token ICONST", "%token IDENT",
        "7e. ICONST precedes IDENT in declaration-order emit");
    ok &= ordered(first, "%token IDENT", "%token ABORT_P",
        "7f. IDENT precedes ABORT_P in declaration-order emit");
    ok &= ordered(first, "%type stmt {int}", "%type stmt_list {int}",
        "7g. stmt precedes stmt_list in declaration-order emit");

    /* Sub-test 8: idempotence -- format(format(F)) == format(F). */
    char *second = format_once(lime_bin, fmt1);
    if (second == NULL) { free(first); return 1; }
    if (strcmp(first, second) != 0) {
        fprintf(stderr,
                "FAIL: 8. idempotence -- format(format(F)) != format(F)\n");
        ok = 0;
    }

    free(first);
    free(second);

    if (!ok) return 1;
    printf("PASS: 8/8 sub-tests -- per-token-group + per-type-group "
           "leading-comment preservation (Lime-Letter-21)\n");
    return 0;
}
