/*
** tests/test_host_reduce.c -- Letter 30 regression: the runtime push
** parser runs a statically-generated grammar's BASE reduce actions
** over a ParserSnapshot, with no C compiler, and propagates semantic
** values $1..$N -> $$ back through the reduce.
**
** A snapshot built by `lime -n` from a value-building grammar gets
** its host_reduce wired to the generated <Name>HostReduce wrapper.
** Driving parse_token with integer leaf values must reproduce the
** computed expression value via parse_result() -- proving the base
** actions fired AND values propagated, not merely that the automaton
** accepted.
**
** Also asserts the back-compat contract: a snapshot whose host_reduce
** is NULL still accepts/rejects identically (recognition-only).
*/
#include "test_compat.h"

#include "parse_context.h"
#include "snapshot.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define CHECK(cond, msg) do {                                            \
    if (!(cond)) {                                                       \
        fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__);          \
        fail++;                                                          \
    }                                                                    \
} while (0)

/* The generated builder for the hr grammar (see tests/meson.build:
** hr_grammar.lime is run through `lime -n` at build time). */
extern ParserSnapshot *HrBuildSnapshot(void);

/* The He grammar adds a %extra_argument {long *bias}; each NUM action
** computes N + *bias, proving the wrapper delivers `user` into the
** extra-argument slot (Letter 31). */
extern ParserSnapshot *HeBuildSnapshot(void);

/* The Hs grammar has %token_type {const char *}; its action reads $1
** as a char* DIRECTLY and returns it as $$.  Proves rhs_values[i] is
** the symbol's value-by-value (a pointer-width payload), not a pointer
** to a slot holding it -- the Letter 33 ABI confirmation. */
extern ParserSnapshot *HsBuildSnapshot(void);

/* Token codes from the generated hr_grammar.h (declaration order). */
enum { HR_PLUS = 1, HR_STAR = 2, HR_NUM = 3 };

/* Token codes from he_grammar.h (PLUS, NUM in declaration order). */
enum { HE_PLUS = 1, HE_NUM = 2 };

/* Box / unbox an int as the opaque value slot.  The grammar's
** %token_type is intptr_t so the value is pointer-representable -- the
** contract the host-reduce wrapper bridges. */
static void *boxi(intptr_t v) { return (void *)v; }

int main(void) {
    int fail = 0;

    /* --- 1. base actions fire + values propagate ---------------- */
    {
        ParserSnapshot *snap = HrBuildSnapshot();
        CHECK(snap != NULL, "HrBuildSnapshot");
        CHECK(snap->host_reduce != NULL, "snapshot carries host_reduce");

        /* Parse "2 + 3 * 4" = 14 (grammar encodes * binding tighter). */
        ParseContext *ctx = parse_begin(snap);
        CHECK(ctx != NULL, "parse_begin");
        CHECK(parse_token(ctx, HR_NUM, boxi(2), 0) == 0, "NUM 2");
        CHECK(parse_token(ctx, HR_PLUS, NULL, 1) == 0, "PLUS");
        CHECK(parse_token(ctx, HR_NUM, boxi(3), 2) == 0, "NUM 3");
        CHECK(parse_token(ctx, HR_STAR, NULL, 3) == 0, "STAR");
        CHECK(parse_token(ctx, HR_NUM, boxi(4), 4) == 0, "NUM 4");
        int rc = parse_token(ctx, 0, NULL, -1); /* EOF */
        CHECK(rc == 1, "EOF accepts");

        intptr_t result = (intptr_t)parse_result(ctx);
        CHECK(result == 14, "2 + 3 * 4 == 14 (base actions ran + propagated)");

        parse_end(ctx);
        snapshot_release(snap);
    }

    /* --- 2. session override via parse_set_host_reduce ---------- */
    {
        ParserSnapshot *snap = HrBuildSnapshot();
        ParseContext *ctx = parse_begin(snap);
        /* Clear the snapshot hook on this session -> recognition only.
        ** The parse must still accept, but produce no value. */
        parse_set_host_reduce(ctx, NULL, NULL);
        /* NULL override does NOT clear the snapshot hook (NULL means
        ** "fall back to snapshot"), so to prove recognition-only we
        ** instead drive a snapshot whose hook we never set -- covered
        ** by sub-test 3.  Here we just confirm the setter is callable
        ** and a normal parse still works. */
        CHECK(parse_token(ctx, HR_NUM, boxi(7), 0) == 0, "NUM 7");
        CHECK(parse_token(ctx, 0, NULL, -1) == 1, "EOF accepts (single NUM)");
        CHECK((intptr_t)parse_result(ctx) == 7, "single NUM value == 7");
        parse_end(ctx);
        snapshot_release(snap);
    }

    /* --- 3. recognition-only back-compat (host_reduce NULL) ----- */
    {
        ParserSnapshot *snap = HrBuildSnapshot();
        /* Force recognition-only by clearing the snapshot's hook AND
        ** binding a NULL session override is not enough (NULL = fall
        ** back); so null the field directly to model a snapshot built
        ** by pre-host-reduce lime.  The snapshot is mutable here
        ** because we own the only reference. */
        snap->host_reduce = NULL;
        snap->host_reduce_user = NULL;

        ParseContext *ctx = parse_begin(snap);
        CHECK(parse_token(ctx, HR_NUM, boxi(99), 0) == 0, "NUM (recog-only)");
        CHECK(parse_token(ctx, HR_PLUS, NULL, 1) == 0, "PLUS (recog-only)");
        CHECK(parse_token(ctx, HR_NUM, boxi(1), 2) == 0, "NUM (recog-only)");
        CHECK(parse_token(ctx, 0, NULL, -1) == 1, "EOF accepts (recog-only)");
        /* No host_reduce ran, so no result value. */
        CHECK(parse_result(ctx) == NULL, "recognition-only: no result value");

        /* And a malformed stream still rejects. */
        parse_end(ctx);
        ParseContext *ctx2 = parse_begin(snap);
        int bad = parse_token(ctx2, HR_PLUS, NULL, 0); /* leading PLUS */
        CHECK(bad != 0, "recognition-only: leading PLUS rejected");
        parse_end(ctx2);

        snapshot_release(snap);
    }

    /* --- 4. %extra_argument: `user` reaches the action (Letter 31) - */
    {
        ParserSnapshot *snap = HeBuildSnapshot();
        CHECK(snap != NULL, "HeBuildSnapshot");
        CHECK(snap->host_reduce != NULL, "He snapshot carries host_reduce");

        long bias = 10;
        ParseContext *ctx = parse_begin(snap);
        /* user = &bias; each NUM action computes N + *bias. */
        parse_set_host_reduce(ctx, snap->host_reduce, &bias);
        /* Parse "2 + 3": (2+10) + (3+10) = 25. */
        CHECK(parse_token(ctx, HE_NUM, boxi(2), 0) == 0, "He NUM 2");
        CHECK(parse_token(ctx, HE_PLUS, NULL, 1) == 0, "He PLUS");
        CHECK(parse_token(ctx, HE_NUM, boxi(3), 2) == 0, "He NUM 3");
        CHECK(parse_token(ctx, 0, NULL, -1) == 1, "He EOF accepts");
        intptr_t r = (intptr_t)parse_result(ctx);
        CHECK(r == 25, "(2+bias) + (3+bias) == 25 (extra-arg reached action)");
        parse_end(ctx);

        /* Change the bias -> result tracks it, proving it's read live. */
        bias = 100;
        ParseContext *ctx2 = parse_begin(snap);
        parse_set_host_reduce(ctx2, snap->host_reduce, &bias);
        CHECK(parse_token(ctx2, HE_NUM, boxi(5), 0) == 0, "He NUM 5");
        CHECK(parse_token(ctx2, 0, NULL, -1) == 1, "He EOF accepts (single)");
        CHECK((intptr_t)parse_result(ctx2) == 105, "5 + bias(100) == 105");
        parse_end(ctx2);

        snapshot_release(snap);
    }

    /* --- 5. value-by-value ABI for a pointer/string %token_type ---
    ** (Letter 33).  Pass a char* through parse_token; the action reads
    ** it as `(const char *) $1` directly and returns it as $$.  The
    ** SAME pointer must come back -- proving rhs_values[i] IS the value
    ** (no extra indirection). ----------------------------------------- */
    {
        ParserSnapshot *snap = HsBuildSnapshot();
        CHECK(snap != NULL, "HsBuildSnapshot");
        CHECK(snap->host_reduce != NULL, "Hs snapshot carries host_reduce");

        static const char kWord[] = "name";
        /* Hs token codes: WORD is the first (only) terminal -> 1. */
        ParseContext *ctx = parse_begin(snap);
        CHECK(parse_token(ctx, 1 /*WORD*/, (void *)kWord, 0) == 0, "Hs WORD");
        CHECK(parse_token(ctx, 0, NULL, -1) == 1, "Hs EOF accepts");
        const char *got = (const char *)parse_result(ctx);
        /* Same pointer (value-by-value, not a deref of a slot). */
        CHECK(got == kWord, "Hs: parse_result is the SAME char* passed in");
        CHECK(got != NULL && strcmp(got, "name") == 0,
              "Hs: the char* points at the original bytes");
        parse_end(ctx);
        snapshot_release(snap);
    }

    printf("test_host_reduce: %s\n", fail ? "FAIL" : "PASS");
    return fail ? 1 : 0;
}
