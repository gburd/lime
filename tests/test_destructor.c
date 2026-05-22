/*
** test_destructor.c -- driver that proves Lime's %destructor directive
** actually fires on stack values popped during error recovery.
**
** Two cases:
**   1. Well-formed input: every value is consumed by a reduction,
**      the destructors fire as part of the action code.  alloc == free.
**   2. Malformed input: the parser pops partially-built items off
**      the stack as part of error recovery.  Without %destructor
**      these would leak; with %destructor they are freed.  Test
**      asserts alloc == free even in the error case.
*/

#include "test_destructor.h"
#include "test_destructor_grammar.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* Generated parser entry points. */
void *DtorAlloc(void *(*mallocProc)(size_t));
void DtorFree(void *p, void (*freeProc)(void *));
void Dtor(void *yyp, int yymajor, void *yyminor, dtor_tracker *trk);

static int n_pass = 0, n_fail = 0;
#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        if (cond) {                                                                                \
            printf("  [PASS] %s\n", msg);                                                          \
            n_pass++;                                                                              \
        } else {                                                                                   \
            printf("  [FAIL] %s\n", msg);                                                          \
            n_fail++;                                                                              \
        }                                                                                          \
    } while (0)

static char *xstrdup(dtor_tracker *trk, const char *s) {
    size_t n = strlen(s) + 1;
    char *r = malloc(n);
    if (r != NULL) {
        memcpy(r, s, n);
        dtor_track_alloc(trk, s);
    }
    return r;
}

typedef struct {
    int code;
    const char *text;
} Tok;

static int run(const Tok *toks, int ntoks, dtor_tracker *trk) {
    void *p = DtorAlloc(malloc);
    if (p == NULL) return -1;
    for (int i = 0; i < ntoks; i++) {
        char *val = NULL;
        if (toks[i].text != NULL) {
            val = xstrdup(trk, toks[i].text);
            if (val == NULL) {
                DtorFree(p, free);
                return -1;
            }
        }
        Dtor(p, toks[i].code, val, trk);
    }
    Dtor(p, 0, NULL, trk); /* EOF */
    DtorFree(p, free);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test 1: happy path                                                  */
/* ------------------------------------------------------------------ */

static void test_happy_path(void) {
    /* foo("a", "b")    -- one well-formed call. */
    Tok seq[] = {
        { DTOR_IDENT, "foo" }, { DTOR_LP, NULL },    { DTOR_STRLIT, "a" },
        { DTOR_COMMA, NULL },  { DTOR_STRLIT, "b" }, { DTOR_RP, NULL },
    };
    dtor_tracker trk = { 0 };
    run(seq, sizeof(seq) / sizeof(seq[0]), &trk);

    printf("  happy path : alloc=%d  free=%d\n", trk.alloc_count, trk.free_count);
    CHECK(trk.alloc_count == trk.free_count, "happy path: alloc count == free count");
    CHECK(trk.alloc_count == 3, "happy path: 3 string allocations (1 IDENT + 2 STRLITs)");
}

/* ------------------------------------------------------------------ */
/*  Test 2: error path -- syntax error mid-arglist                     */
/* ------------------------------------------------------------------ */

static void test_error_path(void) {
    /* foo("a", "b" RP)  -- missing comma turns this into a syntax
    ** error after we've allocated three strings.  Without %destructor
    ** that memory leaks; with it, the parser frees each popped
    ** stack slot. */
    Tok seq[] = {
        { DTOR_IDENT, "foo" },
        { DTOR_LP, NULL },
        { DTOR_STRLIT, "a" },
        /* No comma here -- syntax error. */
        { DTOR_STRLIT, "b" },
        { DTOR_RP, NULL },
    };
    dtor_tracker trk = { 0 };
    run(seq, sizeof(seq) / sizeof(seq[0]), &trk);

    printf("  error path : alloc=%d  free=%d\n", trk.alloc_count, trk.free_count);
    CHECK(trk.alloc_count == trk.free_count, "error path: alloc count == free count "
                                             "(destructors fire on stack pop)");
}

/* ------------------------------------------------------------------ */

int main(void) {
    printf("Lime %%destructor smoke test\n");
    printf("===========================\n");

    test_happy_path();
    test_error_path();

    printf("\n=== Summary ===  Pass: %d  Fail: %d\n", n_pass, n_fail);
    return n_fail == 0 ? 0 : 1;
}
