/*
** test_skin_bison_union_grammar.y -- round-trip test grammar for the
** bison-API skin's %union { ... } support.
**
** Drives a simple key-value parser that needs two distinct semantic
** types -- int (counts, computed sums) and char* (string keys) --
** and verifies the bison skin emits a YYSTYPE union whose arms can
** be addressed by name (yylval.n / yylval.s) exactly as in bison.
**
** The grammar deliberately tracks distinct fields per token: NUMBER
** carries an int, NAME carries a char*.  yylex() writes yylval.n or
** yylval.s as appropriate before returning -- no tagged-token magic
** required (Lime's parser does not understand `%token<n> NUMBER`).
**
** %extra_argument {struct UnionResult *out} threads a result struct
** through every reduce.  The test compares native-API and bison-skin
** outputs for the same inputs and asserts equivalence.
*/
%name UnionCalc
%union {
    int   n;     /* numeric token / expression value */
    char *s;     /* identifier / lookup key (caller owns the storage) */
}
%type item   {int}
%extra_argument {struct UnionResult *out}

%include {
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct UnionResult {
    int  total;       /* running sum of evaluated items */
    int  nitems;      /* number of items folded */
    char last_name[64]; /* last NAME token seen (for the test to inspect) */
};
}

%syntax_error {
    (void)yymajor;
    (void)yyminor;
    void yyerror(const char *);
    yyerror("syntax error");
    out->total = -1;
}

%token NUMBER NAME EQ SEMI.

%start_symbol program

program ::= items.

items ::= item.
items ::= items SEMI item.

/* `name = N` records the name and adds N to the running total. */
item(R) ::= NAME(K) EQ NUMBER(V). {
    /* K is YYSTYPE (the %union); access the .s arm.  V is YYSTYPE
    ** too -- access the .n arm.  Lime does NOT support bison's
    ** angle-bracketed token tags (%token<n> NUMBER), so the user's
    ** action body picks the union arm by name -- exactly the
    ** workflow documented in docs/SKINS.md. */
    if (K.s) {
        size_t n = strlen(K.s);
        if (n >= sizeof(out->last_name)) n = sizeof(out->last_name) - 1;
        memcpy(out->last_name, K.s, n);
        out->last_name[n] = 0;
    }
    out->total += V.n;
    out->nitems++;
    R = V.n;
}

/* Bare-number item: just sum it. */
item(R) ::= NUMBER(V). {
    out->total += V.n;
    out->nitems++;
    R = V.n;
}

/* Bare-name item: the lookup result is stubbed at 0; just record. */
item(R) ::= NAME(K). {
    if (K.s) {
        size_t n = strlen(K.s);
        if (n >= sizeof(out->last_name)) n = sizeof(out->last_name) - 1;
        memcpy(out->last_name, K.s, n);
        out->last_name[n] = 0;
    }
    out->nitems++;
    R = 0;
}
