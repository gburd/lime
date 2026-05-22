/*
** snapshot_build.c -- construct a runtime ParserSnapshot from a
** generated parser's static tables.
**
** This is the bridge that lets `parse_begin / parse_token / parse_end`
** actually drive any Lime-generated parser without changing the
** generator output, simply by registering its tables with a snapshot.
*/
#include "snapshot_build.h"
#include "snapshot.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                    */
/* ------------------------------------------------------------------ */

static uint16_t *dup_u16(const uint16_t *src, uint32_t count) {
    if (src == NULL || count == 0) return NULL;
    size_t sz = count * sizeof(uint16_t);
    uint16_t *copy = malloc(sz);
    if (copy != NULL) memcpy(copy, src, sz);
    return copy;
}

static int16_t *dup_i16(const int16_t *src, uint32_t count) {
    if (src == NULL || count == 0) return NULL;
    size_t sz = count * sizeof(int16_t);
    int16_t *copy = malloc(sz);
    if (copy != NULL) memcpy(copy, src, sz);
    return copy;
}

static int8_t *dup_i8(const int8_t *src, uint32_t count) {
    if (src == NULL || count == 0) return NULL;
    size_t sz = count * sizeof(int8_t);
    int8_t *copy = malloc(sz);
    if (copy != NULL) memcpy(copy, src, sz);
    return copy;
}

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                          */
/* ------------------------------------------------------------------ */

ParserSnapshot *snapshot_build_from_tables(const LimeParserTables *t) {
    if (t == NULL) return NULL;

    ParserSnapshot *snap = calloc(1, sizeof(ParserSnapshot));
    if (snap == NULL) return NULL;

    atomic_init(&snap->refcount, 1);
    snap->version = 1;
    snap->create_time_ns = now_ns();

    snap->nstate = t->nstate;
    snap->nrule = t->nrule;
    snap->nsymbol = t->nsymbol;
    snap->nterminal = t->nterminal;

    snap->action_count = t->yy_action_count;
    snap->lookahead_count = t->yy_lookahead_count;

    snap->yy_action = dup_u16(t->yy_action, t->yy_action_count);
    snap->yy_lookahead = dup_u16(t->yy_lookahead, t->yy_lookahead_count);
    snap->yy_shift_ofst = dup_i16(t->yy_shift_ofst, t->nstate);
    snap->yy_reduce_ofst = dup_i16(t->yy_reduce_ofst, t->nstate);
    snap->yy_default = dup_u16(t->yy_default, t->nstate);

    snap->yy_rule_info_lhs = dup_i16(t->yy_rule_info_lhs, t->nrule);
    snap->yy_rule_info_nrhs = dup_i8(t->yy_rule_info_nrhs, t->nrule);

    if (t->yy_fallback && t->nfallback > 0) {
        snap->yy_fallback = dup_u16(t->yy_fallback, t->nfallback);
        snap->nfallback = t->nfallback;
    }

    /* Deep-copy the original grammar source text so the snapshot
    ** outlives the LimeParserTables struct (and the .so it came
    ** from, in the dlopen path).  publish_modified_snapshot reads
    ** this to drive the subprocess rebuild path. */
    if (t->grammar_source != NULL && t->grammar_source_len > 0) {
        snap->grammar_source = malloc(t->grammar_source_len + 1);
        if (snap->grammar_source != NULL) {
            memcpy(snap->grammar_source, t->grammar_source, t->grammar_source_len);
            snap->grammar_source[t->grammar_source_len] = '\0';
            snap->grammar_source_len = t->grammar_source_len;
        }
    }

    snap->yy_ntoken = t->ntoken;
    snap->yy_max_shift = t->yy_max_shift;
    snap->yy_min_shiftreduce = t->yy_min_shiftreduce;
    snap->yy_max_shiftreduce = t->yy_max_shiftreduce;
    snap->yy_error_action = t->yy_error_action;
    snap->yy_accept_action = t->yy_accept_action;
    snap->yy_no_action = t->yy_no_action;
    snap->yy_min_reduce = t->yy_min_reduce;

    /* Sanity: anything that should have been duplicated and wasn't
    ** indicates an OOM during table copy.  Roll back. */
    bool ok =
        (t->yy_action_count == 0 || snap->yy_action != NULL) &&
        (t->yy_lookahead_count == 0 || snap->yy_lookahead != NULL) &&
        (t->nstate == 0 || (snap->yy_shift_ofst != NULL && snap->yy_reduce_ofst != NULL &&
                            snap->yy_default != NULL)) &&
        (t->nrule == 0 || (snap->yy_rule_info_lhs != NULL && snap->yy_rule_info_nrhs != NULL));
    if (!ok) {
        snapshot_release(snap);
        return NULL;
    }

    return snap;
}
