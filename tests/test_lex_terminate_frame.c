/*
** tests/test_lex_terminate_frame.c -- P0-NEW-13 regression test.
**
** Reproduces the ecpg failure mode in isolation: an action body that
** fires LEX_TERMINATE() leaves the bottom frame that LexFeedBytes
** itself pushed alive on yyl->stack.  After 64 iterations the
** TLT_LEX_MAX_INCLUDE_DEPTH=64 ceiling fails subsequent calls with
** "include depth exceeded".  The fix pops back to initial_depth on
** every return path, so the depth invariant on return is
** yyl->depth == initial_depth.
**
** Drive 200 iterations -- well past the ceiling -- and verify each
** call returns LEX_OK, fires exactly one WORD token, and leaves
** LexIncludeDepth(yyl) == 0.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_lex_terminate_frame_grammar_lex.h"

#define ITERATIONS 200

static int fails = 0;

static int last_rule = -1;
static int emit_count = 0;

static void emit_cb(void *user, int rule,
                    const char *text, size_t len) {
    (void)user; (void)text; (void)len;
    last_rule = rule;
    emit_count++;
}

int main(void) {
    TltLexer *yyl = TltLexAlloc(malloc);
    if (!yyl) {
        fprintf(stderr, "TltLexAlloc returned NULL\n");
        return 1;
    }

    const char *input = "abc";
    const size_t n    = 3;

    for (int i = 0; i < ITERATIONS; i++) {
        emit_count = 0;
        last_rule  = -1;
        TltLexResult r = TltLexFeedBytes(yyl, input, n, emit_cb, NULL);
        if (r != TLT_LEX_OK) {
            fprintf(stderr,
                    "iter %d: LexFeedBytes returned %d, err=%s\n",
                    i, (int)r,
                    TltLexErrorMessage(yyl) ? TltLexErrorMessage(yyl) : "(null)");
            fails++;
            break;
        }
        if (TltLexIncludeDepth(yyl) != 0) {
            fprintf(stderr,
                    "iter %d: LexIncludeDepth=%d, want 0 (frame leak)\n",
                    i, TltLexIncludeDepth(yyl));
            fails++;
            break;
        }
        if (emit_count != 1) {
            fprintf(stderr,
                    "iter %d: emit_count=%d, want 1\n",
                    i, emit_count);
            fails++;
        }
        if (last_rule != TLT_RULE_WORD) {
            fprintf(stderr,
                    "iter %d: rule=%d, want TLT_RULE_WORD=%d\n",
                    i, last_rule, TLT_RULE_WORD);
            fails++;
        }
        if (fails > 0) break;
    }

    TltLexFree(yyl, free);

    if (fails == 0) {
        printf("test_lex_terminate_frame: PASS (%d iterations)\n",
               ITERATIONS);
        return 0;
    }
    printf("test_lex_terminate_frame: %d FAIL\n", fails);
    return 1;
}
