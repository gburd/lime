/*
** test_tagged_tokens_grammar.y -- bison-style %token<field> tagged
** tokens (v0.9.3).  Mirrors the shape of test_skin_bison_union but
** declares the union arm per token via the angle-bracket tag so the
** grammar reads more like a bison file:
**
**   %union { int n; char *s; }
**   %token<n> NUM
**   %token<s> ID
**   %token EQ SEMI
**
** The reduce action accesses K.s and V.n by union arm, exactly as
** the workaround in v0.9.2; the win in v0.9.3 is purely declarative
** (the grammar file documents the per-token field) plus the
** generated bison.h carries `/yylval.<field>/` comments next to
** each enum constant.
**
** A small input ("x = 7") is folded to a known result; the test
** also asserts that an untagged %token (EQ, SEMI) keeps working
** alongside tagged ones in the same translation unit.
*/
%name TaggedCalc
%union {
    int   n;     /* numeric token / expression value */
    char *s;     /* identifier (caller owns the storage) */
}
%type item   {int}
%extra_argument {struct TaggedResult *out}

%include {
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct TaggedResult {
    int  total;        /* running sum of evaluated items */
    int  nitems;       /* number of items folded */
    int  ntagged_n;    /* count of NUM tokens seen via the .n arm */
    int  ntagged_s;    /* count of ID tokens seen via the .s arm */
    char last_id[64];  /* last ID token captured */
};
}

%syntax_error {
    (void)yymajor;
    (void)yyminor;
    void yyerror(const char *);
    yyerror("syntax error");
    out->total = -1;
}

/* Tagged tokens: bison `%token<field> NAME` declares which YYSTYPE
** union arm carries the token's semantic value.  Lime stores the
** tag on struct symbol::union_field; the bison skin emits a
** trailing /yylval.<field>/ comment next to each enum constant in
** the generated header so the user's yylex() and reduce actions
** have the arm documented at the point of use. */
%token<n> NUM.
%token<s> ID.
%token EQ SEMI.

%start_symbol program

program ::= items.

items ::= item.
items ::= items SEMI item.

/* `name = N` records the name and adds N to the running total. */
item(R) ::= ID(K) EQ NUM(V). {
    out->ntagged_s++;
    out->ntagged_n++;
    if (K.s) {
        size_t n = strlen(K.s);
        if (n >= sizeof(out->last_id)) n = sizeof(out->last_id) - 1;
        memcpy(out->last_id, K.s, n);
        out->last_id[n] = 0;
    }
    out->total += V.n;
    out->nitems++;
    R = V.n;
}

/* Bare-number item: just sum it. */
item(R) ::= NUM(V). {
    out->ntagged_n++;
    out->total += V.n;
    out->nitems++;
    R = V.n;
}

/* Bare-identifier item: record the name. */
item(R) ::= ID(K). {
    out->ntagged_s++;
    if (K.s) {
        size_t n = strlen(K.s);
        if (n >= sizeof(out->last_id)) n = sizeof(out->last_id) - 1;
        memcpy(out->last_id, K.s, n);
        out->last_id[n] = 0;
    }
    out->nitems++;
    R = 0;
}
