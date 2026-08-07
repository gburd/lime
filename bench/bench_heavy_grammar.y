/*
** bench_heavy_grammar.y -- heavy-REDUCE-ACTION companion to
** bench_arith_grammar.y, contributed by the PostgreSQL Lime-port
** (letter-40 measurement note; Lime reply-40 asked for it for bench/).
**
** SAME structural shape and state-machine size as the arith grammar, but
** every reduce action does REAL work: it allocates a tree node on the heap
** and links children into it -- mimicking PostgreSQL's SQL-grammar reduce
** actions, which do makeNode()/palloc()/lappend()/pstrdup() per reduction
** to build a raw parse tree, NOT integer arithmetic.
**
** The point (letter-40 / reply-40 section 2): on arith, the composed
** push-parser penalty is dominated by per-token/per-shift DISPATCH cost,
** so table-base hoisting / value de-boxing move the needle ~9%. On a
** 600-rule grammar whose actions do palloc'd node construction across the
** host_reduce callback boundary, parse time is dominated by ACTION cost,
** and that same optimization is amortized to noise. A bench needs BOTH
** shapes: arith (dispatch-bound) and this (action-bound) so future perf
** work is measured against a profile that resembles a real embedder.
**
** Grammar (identical structure to bench_arith_grammar.y so the state
** machine, token count, and stack depth match -- only the action WEIGHT
** differs):
**   program -> expr
**   expr    -> term | expr '+' term | expr '-' term
**   term    -> factor | term '*' factor | term '/' factor
**   factor  -> NUM | '(' expr ')'
**
** Value type is a heap Node* instead of int.  Each reduce mallocs a Node
** and links its children -- the malloc/free churn per parse is what makes
** this action-bound.  The result written through %extra_argument is the
** node COUNT (a cheap fold over the built tree), so the driver can assert
** AOT == composed just like the arith bench does, without leaking (the
** tree is freed in the fold).
**
** Build (standalone, same recipe as bench_composed_vs_aot.c):
**   lime --host-reduce -n bench/bench_heavy_grammar.y   # in a tmp dir
**   cc -O2 bench_composed_vs_aot_heavy.c bench_heavy_grammar.c \
**      bench_heavy_grammar_snapshot.c -Iinclude -Isrc \
**      build/src/liblime_parser.a -o bench_composed_vs_aot_heavy
*/

%name_prefix Heavy
%token_prefix HEAVY_
%token_type   { void * }
%type program { void * }
%type expr    { void * }
%type term    { void * }
%type factor  { void * }
%extra_argument { int *result_out }
%start_symbol program

%token NUM PLUS MINUS STAR SLASH LP RP.

%include {
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
** A tree node built per reduce.  Deliberately shaped like a small PG parse
** node: a tag, two child links, and a payload.  The payload strdup mimics
** PG's pstrdup of scan tokens -- another allocation per leaf, which is a
** big part of why PG's action cost dominates.
*/
typedef enum { HN_NUM, HN_ADD, HN_SUB, HN_MUL, HN_DIV, HN_PAREN } HeavyTag;

typedef struct HeavyNode {
    HeavyTag           tag;
    long               ival;      /* for HN_NUM */
    char              *label;     /* strdup'd -- mimics pstrdup of a token */
    struct HeavyNode  *l;
    struct HeavyNode  *r;
} HeavyNode;

static HeavyNode *heavy_make(HeavyTag tag, long ival, const char *label,
                             HeavyNode *l, HeavyNode *r) {
    /* One node allocation per reduce (PG: makeNode + palloc of the node). */
    HeavyNode *n = (HeavyNode *) malloc(sizeof(HeavyNode));
    if (n == NULL) abort();
    n->tag = tag;
    n->ival = ival;
    /* One string allocation per node (PG: pstrdup of a scan token). */
    n->label = label ? strdup(label) : NULL;
    n->l = l;
    n->r = r;
    return n;
}

/*
** Fold the tree to (a) a numeric result so the driver can assert
** AOT == composed exactly as the arith bench does, and (b) free the whole
** tree so a tight parse loop doesn't leak.  Returns the NODE COUNT, which
** is deterministic for a given token stream and identical on both paths.
*/
static int heavy_fold_free(HeavyNode *n) {
    if (n == NULL) return 0;
    int count = 1 + heavy_fold_free(n->l) + heavy_fold_free(n->r);
    free(n->label);
    free(n);
    return count;
}
}

program(P) ::= expr(E). {
    /* Root reduce: fold+free the tree, publish the node count. */
    int count = heavy_fold_free((HeavyNode *) E);
    P = NULL;
    *result_out = count;
}

expr(E) ::= term(T).               { E = T; }
expr(E) ::= expr(L) PLUS  term(R). { E = heavy_make(HN_ADD, 0, "+", (HeavyNode *) L, (HeavyNode *) R); }
expr(E) ::= expr(L) MINUS term(R). { E = heavy_make(HN_SUB, 0, "-", (HeavyNode *) L, (HeavyNode *) R); }

term(T) ::= factor(F).               { T = F; }
term(T) ::= term(L) STAR  factor(R). { T = heavy_make(HN_MUL, 0, "*", (HeavyNode *) L, (HeavyNode *) R); }
term(T) ::= term(L) SLASH factor(R). { T = heavy_make(HN_DIV, 0, "/", (HeavyNode *) L, (HeavyNode *) R); }

factor(F) ::= NUM(N).             { F = heavy_make(HN_NUM, (long)(intptr_t) N, "num", NULL, NULL); }
factor(F) ::= LP expr(E) RP.      { F = heavy_make(HN_PAREN, 0, "()", (HeavyNode *) E, NULL); }
