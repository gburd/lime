/*
** test_nonassoc.c -- a %nonassoc operator must not chain.
**
** `n < n < n` is a syntax error under %nonassoc LT (bison, POSIX yacc).
** Lemon accepted it: CompressTables() turned the state holding the
** nonassoc ERROR action into an auto-reduce state, which reduces without
** looking at the lookahead, so the ERROR was never consulted.  That only
** happens when the state has no SHIFT (no higher-precedence operator can
** follow), hence the CHAIN cases; the LT cases always worked.
*/
#include <stdio.h>
#include <stdlib.h>
#include "test_nonassoc_grammar.h"

void *NAAlloc(void *(*)(size_t));
void NA(void *, int, int, int *);
void NAFree(void *, void (*)(void *));

static int parse(const char *s) {
    int res = 0;
    void *p = NAAlloc(malloc);
    for (; *s && res >= 0; s++) {
        int t = *s == '<' ? LT : *s == '+' ? PLUS : *s == '~' ? CHAIN : N;
        NA(p, t, 0, &res);
    }
    if (res >= 0) NA(p, 0, 0, &res);
    NAFree(p, free);
    return res;
}

int main(void) {
    static const struct { const char *in; int want; } c[] = {
        {"n", 1}, {"n<n", 1}, {"n+n+n", 1}, {"n+n<n+n", 1},
        {"n<n<n", -1}, {"n<n+n<n", -1}, {"n+n<n<n", -1},
        /* CHAIN binds tightest: after `e ~ e` no shift applies, which is
        ** the state CompressTables used to auto-reduce. */
        {"n~n", 1}, {"n~n<n", 1}, {"n~n+n~n", 1}, {"n~n~n", -1}, {"n<n~n~n", -1},
    };
    int fail = 0;
    for (size_t i = 0; i < sizeof c / sizeof c[0]; i++) {
        int got = parse(c[i].in);
        if (got != c[i].want) {
            printf("FAIL %-10s want %s got %s\n", c[i].in,
                   c[i].want > 0 ? "accept" : "reject", got > 0 ? "accept" : "reject");
            fail++;
        }
    }
    printf("nonassoc: %zu cases, %d failures\n", sizeof c / sizeof c[0], fail);
    return fail != 0;
}
