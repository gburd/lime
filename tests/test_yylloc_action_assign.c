/*
** tests/test_yylloc_action_assign.c -- P0-NEW-7 runtime check.
**
** Bison-compatible YYLLOC_DEFAULT pre-action ordering.  Action-body
** `@$ = expr` writes must survive the LHS-yyloc commit at the end
** of the reduce.
**
** The grammar uses %location_type {char *} (ecpg's pattern), with
** YYLLOC_DEFAULT defaulting to Rhs[1].  Each rule's action either
** accepts the default or overrides via cat_str-style concatenation.
** With pre-P0-NEW-7 ordering the action overrides were silently
** clobbered and the captured location collapsed to the first RHS
** alone -- the symptom PG saw as "int x x ;" duplicated tokens.
**
** Sub-tests:
**   1. Single-token list (A) + SEMI -- minimal exercise of the
**      "action overrides default" path.
**   2. Multi-token list (A B C) + SEMI -- discriminator: every
**      list reduction concatenates; final string must be
**      "A B C ;" (or similar).  With the bug, would be just "A".
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_yylloc_action_assign.h"
#include "test_yylloc_action_assign_grammar.h"

void *YactAlloc(void *(*mallocProc)(size_t));
void  YactFree(void *, void (*freeProc)(void *));
void  YactLoc(void *yyp, int yymajor, char *yyminor,
              char *yyloc, struct yact_capture *cap);

/* Test-side cat_str helpers.  Heap-allocate, push into the
** capture's pool so end-of-test cleanup is centralised.  The
** pool mirrors ecpg's palloc-into-parse-context pattern. */
void yact_pool_init(struct yact_pool *p) {
    p->slots = NULL;
    p->n = 0;
    p->cap = 0;
}

void yact_pool_track(struct yact_pool *p, char *ptr) {
    if (p->n == p->cap) {
        size_t newcap = p->cap ? p->cap * 2 : 16;
        char **ns = realloc(p->slots, newcap * sizeof(char *));
        if (!ns) { free(ptr); return; }
        p->slots = ns;
        p->cap = newcap;
    }
    p->slots[p->n++] = ptr;
}

void yact_pool_free(struct yact_pool *p) {
    for (size_t i = 0; i < p->n; i++) free(p->slots[i]);
    free(p->slots);
    p->slots = NULL;
    p->n = p->cap = 0;
}

char *yact_cat3(struct yact_capture *cap,
                const char *a, const char *b, const char *c) {
    size_t la = a ? strlen(a) : 0;
    size_t lb = b ? strlen(b) : 0;
    size_t lc = c ? strlen(c) : 0;
    char *out = malloc(la + lb + lc + 1);
    if (!out) return NULL;
    char *p = out;
    if (a) { memcpy(p, a, la); p += la; }
    if (b) { memcpy(p, b, lb); p += lb; }
    if (c) { memcpy(p, c, lc); p += lc; }
    *p = '\0';
    yact_pool_track(&cap->pool, out);
    return out;
}

char *yact_cat5(struct yact_capture *cap,
                const char *a, const char *b, const char *c,
                const char *d, const char *e) {
    size_t la = a ? strlen(a) : 0;
    size_t lb = b ? strlen(b) : 0;
    size_t lc = c ? strlen(c) : 0;
    size_t ld = d ? strlen(d) : 0;
    size_t le = e ? strlen(e) : 0;
    char *out = malloc(la + lb + lc + ld + le + 1);
    if (!out) return NULL;
    char *p = out;
    if (a) { memcpy(p, a, la); p += la; }
    if (b) { memcpy(p, b, lb); p += lb; }
    if (c) { memcpy(p, c, lc); p += lc; }
    if (d) { memcpy(p, d, ld); p += ld; }
    if (e) { memcpy(p, e, le); p += le; }
    *p = '\0';
    yact_pool_track(&cap->pool, out);
    return out;
}

static int str_eq(const char *got, const char *want, const char *label) {
    if (got == NULL) {
        fprintf(stderr, "  %s: got NULL, want \"%s\"\n", label, want);
        return 1;
    }
    if (strcmp(got, want) != 0) {
        fprintf(stderr, "  %s: got \"%s\", want \"%s\"\n",
                label, got, want);
        return 1;
    }
    return 0;
}

static int test_single_token(void) {
    /* Tokens: A=str("A") loc=str("A"), SEMI=str(";") loc=str(";").
    ** Reductions:
    **   list ::= A           -- L = "A", @L default = "A" (Rhs[1])
    **   e ::= list           -- E = "A", @E default = "A"
    **   s ::= e SEMI         -- @S overridden to cat3(@E, " ", @T)
    **                          = "A" + " " + ";" = "A ;"
    **   top ::= s            -- @T captured. */
    struct yact_capture cap = {0};
    yact_pool_init(&cap.pool);
    void *p = YactAlloc(malloc);
    if (!p) { yact_pool_free(&cap.pool); return 1; }
    YactLoc(p, YACT_A,    (char *)"A",  (char *)"A",  &cap);
    YactLoc(p, YACT_SEMI, (char *)";",  (char *)";",  &cap);
    YactLoc(p, 0,         (char *)"$",  (char *)"$",  &cap);
    YactFree(p, free);

    int fails = 0;
    if (cap.list_seen != 1) {
        fprintf(stderr, "  list_seen=%d expected 1\n", cap.list_seen);
        fails++;
    }
    /* @L for list ::= A: default Rhs[1] = "A".  Action does not
    ** override (it just sets L). */
    fails += str_eq(cap.list_loc, "A", "list_loc");
    fails += str_eq(cap.e_loc,    "A", "e_loc");
    /* @S for s ::= e SEMI was overridden in the action body. */
    fails += str_eq(cap.final_loc, "A ;", "final_loc (action override)");

    yact_pool_free(&cap.pool);
    if (fails == 0) printf("test_single_token: PASS\n");
    return fails;
}

static int test_multi_token_list(void) {
    /* Tokens: A B C SEMI.
    ** Reductions:
    **   list ::= A             -- L = "A",       @L default = "A"
    **   list ::= list B        -- @L OVERRIDDEN: cat3("A", " ", "B") = "A B"
    **   list ::= list C        -- @L OVERRIDDEN: cat5("A B", " ", "C", "", "") = "A B C"
    **   e ::= list             -- @E default = "A B C"
    **   s ::= e SEMI           -- @S OVERRIDDEN: cat3("A B C", " ", ";") = "A B C ;"
    **
    ** The discriminator: with pre-P0-NEW-7 ordering, every action's
    ** @L/@S override gets silently overwritten by YYLLOC_DEFAULT's
    ** Rhs[1] post-action commit, so list_loc collapses to "A" and
    ** final_loc collapses to "A". */
    struct yact_capture cap = {0};
    yact_pool_init(&cap.pool);
    void *p = YactAlloc(malloc);
    if (!p) { yact_pool_free(&cap.pool); return 1; }
    YactLoc(p, YACT_A,    (char *)"A",  (char *)"A",  &cap);
    YactLoc(p, YACT_B,    (char *)"B",  (char *)"B",  &cap);
    YactLoc(p, YACT_C,    (char *)"C",  (char *)"C",  &cap);
    YactLoc(p, YACT_SEMI, (char *)";",  (char *)";",  &cap);
    YactLoc(p, 0,         (char *)"$",  (char *)"$",  &cap);
    YactFree(p, free);

    int fails = 0;
    if (cap.list_seen != 3) {
        fprintf(stderr, "  list_seen=%d expected 3 (A, B, C)\n",
                cap.list_seen);
        fails++;
    }
    fails += str_eq(cap.list_loc,  "A B C",   "list_loc (THE override test)");
    fails += str_eq(cap.e_loc,     "A B C",   "e_loc (default Rhs[1])");
    fails += str_eq(cap.final_loc, "A B C ;", "final_loc (s action override)");

    yact_pool_free(&cap.pool);
    if (fails == 0) printf("test_multi_token_list: PASS\n");
    return fails;
}

int main(void) {
    int fails = 0;
    fails += test_single_token();
    fails += test_multi_token_list();
    if (fails == 0) {
        printf("\ntest_yylloc_action_assign: all sub-tests PASS\n");
        return 0;
    }
    fprintf(stderr,
            "\ntest_yylloc_action_assign: %d sub-test(s) FAILED\n", fails);
    return 1;
}
