/*
** tests/test_skin_flex.c -- round-trip test for --target=c:flex.
**
** Drives the same input two ways:
**
**   1. Lime native push-driven runtime: SfLexAlloc / SfLexFeedBytes /
**      SfLexFeedEOF / SfLexFree, capturing the rule code and matched
**      text on every emit callback.
**
**   2. flex-skin pull-driven yylex() over yy_scan_string(input):
**      collect (rule, text) per call until yylex() returns 0 (EOF).
**
** Asserts:
**   - Both APIs produce the same length, rule sequence, and text
**     sequence for each input.
**   - yylineno tracks newlines in the matched text.
**   - YY_FLEX_INVALID is returned for unmatched input bytes (the
**     skin advances one byte and reports it; the native API has
**     no equivalent so this branch is asserted only on the skin
**     side).
**   - The flex enum values match the documented contract:
**     SF_FLEX_PLUS == 1 (rule_id 0 + 1), increasing in declaration
**     order.
**
** This test guarantees the flex skin tracks Lime's standard lexer
** byte-for-byte over the supported subset (auto-emit; no action
** bodies).
*/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_skin_flex_grammar_flex.h"
#include "test_skin_flex_grammar_lex.h"

/* ------------------------------------------------------------ */
/*  Native-side capture                                          */
/* ------------------------------------------------------------ */

#define MAX_TOKENS 64

struct token_t {
    int   rule;
    char  text[64];
    int   len;
};

struct capture {
    int n;
    struct token_t toks[MAX_TOKENS];
};

static void capture_emit(void *user, int rule, const char *text, size_t len) {
    struct capture *c = (struct capture *)user;
    if (c->n >= MAX_TOKENS) return;
    struct token_t *t = &c->toks[c->n++];
    t->rule = rule;
    if (len >= sizeof t->text) len = sizeof t->text - 1;
    memcpy(t->text, text, len);
    t->text[len] = 0;
    t->len = (int)len;
}

static int run_native(const char *input, struct capture *out) {
    out->n = 0;
    SfLexer *yyl = SfLexAlloc(malloc);
    if (yyl == NULL) return -1;
    SfLexResult r = SfLexFeedBytes(yyl, input, strlen(input),
                                   capture_emit, out);
    if (r == SF_LEX_OK) {
        r = SfLexFeedEOF(yyl, capture_emit, out);
    }
    SfLexFree(yyl, free);
    return (r == SF_LEX_OK) ? 0 : 1;
}

/* ------------------------------------------------------------ */
/*  Skin-side capture                                            */
/* ------------------------------------------------------------ */

static int run_skin(const char *input, struct capture *out) {
    out->n = 0;
    yylineno = 1;  /* reset between cases */
    YY_BUFFER_STATE b = yy_scan_string(input);
    if (b == NULL) return -1;
    int tok;
    while ((tok = yylex()) != 0) {
        if (tok < 0) {
            /* YY_FLEX_INVALID: documented "advanced one byte"
            ** branch.  Stop capture and report the same error rc
            ** the native API returns when no rule matches. */
            yy_delete_buffer(b);
            return 1;
        }
        if (out->n >= MAX_TOKENS) break;
        struct token_t *t = &out->toks[out->n++];
        /* yylex returns rule_id + 1; the native side stores rule_id
        ** so subtract 1 here for direct comparison. */
        t->rule = tok - 1;
        int len = yyleng;
        if (len >= (int)sizeof t->text) len = (int)sizeof t->text - 1;
        memcpy(t->text, yytext, (size_t)len);
        t->text[len] = 0;
        t->len = len;
    }
    yy_delete_buffer(b);
    return 0;
}

/* ------------------------------------------------------------ */
/*  Comparison                                                    */
/* ------------------------------------------------------------ */

static int compare_captures(const char *input,
                            const struct capture *a,
                            const struct capture *b) {
    if (a->n != b->n) {
        fprintf(stderr,
            "  count mismatch: native=%d skin=%d (input=\"%s\")\n",
            a->n, b->n, input);
        return 1;
    }
    for (int i = 0; i < a->n; i++) {
        if (a->toks[i].rule != b->toks[i].rule) {
            fprintf(stderr,
                "  rule[%d] mismatch: native=%d skin=%d (input=\"%s\")\n",
                i, a->toks[i].rule, b->toks[i].rule, input);
            return 1;
        }
        if (strcmp(a->toks[i].text, b->toks[i].text) != 0) {
            fprintf(stderr,
                "  text[%d] mismatch: native=\"%s\" skin=\"%s\" (input=\"%s\")\n",
                i, a->toks[i].text, b->toks[i].text, input);
            return 1;
        }
    }
    return 0;
}

int main(void) {
    /* Locked-in flex enum values.  Rule ids assigned in declaration
    ** order; +1 keeps 0 reserved for EOF. */
    if (SF_FLEX_PLUS != 1) {
        fprintf(stderr, "expected SF_FLEX_PLUS=1, got %d\n", SF_FLEX_PLUS);
        return 1;
    }
    if (SF_FLEX_NL != 5) {
        fprintf(stderr, "expected SF_FLEX_NL=5, got %d\n", SF_FLEX_NL);
        return 1;
    }
    if (YY_FLEX_EOF != 0) {
        fprintf(stderr, "expected YY_FLEX_EOF=0, got %d\n", YY_FLEX_EOF);
        return 1;
    }

    static const char *cases[] = {
        "hello 123",
        "+ + +",
        "abc 1 def 2 ghi 3",
        "  leading",
        "single",
        "",  /* empty buffer -> immediate EOF on both sides */
    };
    int ncase = (int)(sizeof cases / sizeof cases[0]);
    int nfail = 0;
    for (int i = 0; i < ncase; i++) {
        struct capture nat = {0}, skn = {0};
        int nr = run_native(cases[i], &nat);
        int sr = run_skin(cases[i], &skn);
        if (nr != sr) {
            fprintf(stderr,
                "case %d: rc mismatch (native=%d skin=%d) input=\"%s\"\n",
                i, nr, sr, cases[i]);
            nfail++;
            continue;
        }
        if (compare_captures(cases[i], &nat, &skn) != 0) {
            nfail++;
        } else {
            printf("case %d: %d tokens [OK]  input=\"%s\"\n",
                   i, nat.n, cases[i]);
        }
    }

    /* yylineno: feed two newlines, expect yylineno to advance by 2.
    ** Starting yylineno is 1 (matches flex contract). */
    {
        yylineno = 1;
        YY_BUFFER_STATE b = yy_scan_string("a\nb\nc");
        if (b == NULL) {
            fprintf(stderr, "yylineno test: scan_string returned NULL\n");
            nfail++;
        } else {
            while (yylex() != 0) { /* drain */ }
            if (yylineno != 3) {
                fprintf(stderr,
                    "yylineno test: expected 3 after \"a\\nb\\nc\", got %d\n",
                    yylineno);
                nfail++;
            } else {
                printf("yylineno: 3 after \"a\\nb\\nc\" [OK]\n");
            }
            yy_delete_buffer(b);
        }
    }

    /* YY_FLEX_INVALID branch: feed a character outside any rule.
    ** The skin should advance one byte and return YY_FLEX_INVALID;
    ** subsequent yylex() resumes normal matching. */
    {
        yylineno = 1;
        YY_BUFFER_STATE b = yy_scan_string("@abc");
        if (b == NULL) {
            fprintf(stderr, "invalid-token test: scan_string NULL\n");
            nfail++;
        } else {
            int t = yylex();
            if (t != YY_FLEX_INVALID) {
                fprintf(stderr,
                    "invalid-token test: expected %d, got %d\n",
                    YY_FLEX_INVALID, t);
                nfail++;
            }
            int t2 = yylex();
            if (t2 != SF_FLEX_IDENT) {
                fprintf(stderr,
                    "invalid-token test: expected IDENT after recovery, "
                    "got %d (text=\"%.*s\")\n", t2, yyleng, yytext);
                nfail++;
            } else {
                printf("YY_FLEX_INVALID recovery: %d -> IDENT [OK]\n",
                       YY_FLEX_INVALID);
            }
            yy_delete_buffer(b);
        }
    }

    if (nfail) {
        fprintf(stderr, "\n%d failure(s)\n", nfail);
        return 1;
    }
    printf("\nAll cases passed (flex-skin / native-API equivalence verified).\n");
    return 0;
}
