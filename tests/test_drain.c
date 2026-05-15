/*
** tests/test_drain.c -- P0-NEW-8 runtime check.
**
** Bison-equivalent eager default-reduce timing via Parse_drain.
** The grammar's actions append a single character to cap->buf;
** the driver appends a space between Parse() calls to simulate
** ecpg's lexer echo_text.  Token-then-space ordering is the
** discriminator:
**
**   Without drain (the bug):
**      shift A, push B (triggers reduce A) -> output "A " then
**      lex space -> output " " then push C (triggers reduce B)
**      -> output " AB " (wrong)
**
**   With drain (the fix):
**      shift A, drain (fires reduce A -> "A"), echo " " -> "A "
**      shift B, drain (fires reduce B -> "B"), echo " " -> "A B "
**      ...
**      result: "A B C "
**
** Sub-tests:
**   1. Without Parse_drain: output is wrong (the bug PG saw).
**   2. With Parse_drain: output is correct.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_drain.h"
#include "test_drain_grammar.h"

void *DrainAlloc(void *(*mallocProc)(size_t));
void  DrainFree(void *, void (*freeProc)(void *));
void  Drain    (void *yyp, int yymajor, int yyminor,
                struct drain_capture *cap);
void  Drain_drain(void *yyp);

void drain_emit(struct drain_capture *cap, const char *s, size_t n) {
    if (cap->buf_len + n + 1 >= sizeof(cap->buf)) return;
    memcpy(cap->buf + cap->buf_len, s, n);
    cap->buf_len += n;
    cap->buf[cap->buf_len] = '\0';
}

static int run(int use_drain, char *out_buf, size_t out_cap) {
    struct drain_capture cap = {0};
    void *p = DrainAlloc(malloc);
    if (!p) return 1;

    /* Token A. */
    Drain(p, DRAIN_A, 0, &cap);
    if (use_drain) Drain_drain(p);
    drain_emit(&cap, " ", 1);

    /* Token B. */
    Drain(p, DRAIN_B, 0, &cap);
    if (use_drain) Drain_drain(p);
    drain_emit(&cap, " ", 1);

    /* Token C. */
    Drain(p, DRAIN_C, 0, &cap);
    if (use_drain) Drain_drain(p);
    drain_emit(&cap, " ", 1);

    /* EOF flushes any pending reduces (whether drain is opt-in
    ** or not). */
    Drain(p, 0, 0, &cap);
    DrainFree(p, free);

    if (cap.buf_len < out_cap) {
        memcpy(out_buf, cap.buf, cap.buf_len + 1);
    }
    return 0;
}

static int test_without_drain(void) {
    char buf[256] = {0};
    if (run(0, buf, sizeof(buf)) != 0) return 1;
    /* Without drain, the reduce for `a_stmt ::= A` fires only
    ** when B is pushed.  So:
    **   - Push A, append " " (channel 1). buf=" "
    **   - Push B (triggers reduce A: append "A"), append " " (channel 1). buf=" A "
    **   - Push C (triggers reduce B: append "B"), append " " (channel 1). buf=" A B "
    **   - Push EOF (triggers reduce C: append "C", reduce prog). buf=" A B C"
    ** Discriminator: the leading space precedes the first letter.
    ** With drain, the leading character is "A". */
    if (buf[0] != ' ') {
        fprintf(stderr,
                "  test_without_drain: expected leading ' ' (bug), got \"%s\"\n",
                buf);
        return 1;
    }
    printf("test_without_drain: PASS  (buf=\"%s\")\n", buf);
    return 0;
}

static int test_with_drain(void) {
    char buf[256] = {0};
    if (run(1, buf, sizeof(buf)) != 0) return 1;
    /* Expected: "A B C ". */
    if (strcmp(buf, "A B C ") != 0) {
        fprintf(stderr,
                "  test_with_drain: expected \"A B C \", got \"%s\"\n", buf);
        return 1;
    }
    printf("test_with_drain: PASS  (buf=\"%s\")\n", buf);
    return 0;
}

static int test_drain_idempotent(void) {
    /* Calling Drain_drain multiple times in a row past the
    ** quiescent point must be a no-op. */
    void *p = DrainAlloc(malloc);
    struct drain_capture cap = {0};
    Drain(p, DRAIN_A, 0, &cap);
    Drain_drain(p);
    size_t after_first = cap.buf_len;
    Drain_drain(p);
    Drain_drain(p);
    Drain_drain(p);
    if (cap.buf_len != after_first) {
        fprintf(stderr,
                "  test_drain_idempotent: buf changed after redundant drains "
                "(%zu -> %zu)\n",
                after_first, cap.buf_len);
        DrainFree(p, free);
        return 1;
    }
    Drain(p, 0, 0, &cap);
    DrainFree(p, free);
    printf("test_drain_idempotent: PASS\n");
    return 0;
}

int main(void) {
    int fails = 0;
    fails += test_without_drain();
    fails += test_with_drain();
    fails += test_drain_idempotent();
    if (fails == 0) {
        printf("\ntest_drain: all sub-tests PASS\n");
        return 0;
    }
    fprintf(stderr, "\ntest_drain: %d sub-test(s) FAILED\n", fails);
    return 1;
}
