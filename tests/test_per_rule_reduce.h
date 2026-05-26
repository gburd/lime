/*
** tests/test_per_rule_reduce.h -- shared types between the test
** driver (test_per_rule_reduce.c) and the generated parser
** (test_per_rule_reduce_grammar.c).
*/
#ifndef TEST_PER_RULE_REDUCE_H
#define TEST_PER_RULE_REDUCE_H

#include <stddef.h>

struct prr_ctx {
    int  result;
    char trace[64];
    int  trace_len;
};

static inline void prr_log(struct prr_ctx *c, char tag) {
    if (c->trace_len + 1 < (int)sizeof(c->trace)) {
        c->trace[c->trace_len++] = tag;
        c->trace[c->trace_len] = 0;
    }
}

#endif
