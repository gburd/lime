/*
 * src/snapshot_diff.c -- binary diff/patch for ParserSnapshots.
 *
 * Implements the API declared in include/snapshot_diff.h.  Operates
 * over the action-table arrays in struct ParserSnapshot:
 *
 *   yy_action          (uint16_t[action_count])
 *   yy_lookahead       (uint16_t[lookahead_count])
 *   yy_shift_ofst      (int32_t [nstate])
 *   yy_reduce_ofst     (int32_t [nstate])
 *   yy_default         (uint16_t[nstate])
 *   yy_rule_info_lhs   (int16_t [nrule])
 *   yy_rule_info_nrhs  (int8_t  [nrule])
 *
 * For each table the diff stores a list of (offset, new_value)
 * change records plus the new-side count (so apply knows the
 * resulting size).  The "old" array is used only to skip unchanged
 * entries during diff computation; apply is base-relative.
 *
 * Strategy is intentionally simple: linear scan over the longer of
 * the two arrays.  Better algorithms (Myers diff, content-hashed
 * blocks) buy nothing here -- snapshot tables are large dense
 * integer arrays where most entries are unique and adjacency rarely
 * carries semantic structure that would survive permutation.
 *
 * Round-trip property: for any old, new,
 *   apply_diff(old, diff(old, new)) is functionally equivalent to new.
 *
 * "Functionally equivalent" means: same nstate / nrule / nterminal,
 * same yy_* table contents byte-for-byte.  String fields (grammar
 * source, module identity, etc.) are not in the diff -- this is a
 * pure compact-action-table delta.
 */

#include "snapshot_diff.h"
#include "snapshot.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/*  Internal types                                                    */
/* ------------------------------------------------------------------ */

typedef struct change_u16 {
    uint32_t offset;
    uint16_t new_value;
} change_u16;

typedef struct change_i32 {
    uint32_t offset;
    int32_t  new_value;
} change_i32;

typedef struct change_i16 {
    uint32_t offset;
    int16_t  new_value;
} change_i16;

typedef struct change_i8 {
    uint32_t offset;
    int8_t   new_value;
} change_i8;

struct LimeSnapshotDiff {
    /* Header counts.  -1 in `old_*` indicates the diff was computed
     * from NULL (full-snapshot reconstruction). */
    int64_t old_nstate, new_nstate;
    int64_t old_nrule,  new_nrule;
    int64_t old_nterminal, new_nterminal;
    int64_t old_nsymbol,   new_nsymbol;
    int64_t old_action_count,    new_action_count;
    int64_t old_lookahead_count, new_lookahead_count;

    /* Snapshot-action-constants -- always carried so apply can
     * reconstruct the dispatch range markers without consulting
     * the original snapshot. */
    uint16_t yy_max_shift;
    uint16_t yy_min_shiftreduce;
    uint16_t yy_max_shiftreduce;
    uint16_t yy_error_action;
    uint16_t yy_accept_action;
    uint16_t yy_no_action;
    uint16_t yy_min_reduce;
    uint16_t yy_ntoken;

    /* Per-table change lists. */
    change_u16 *yy_action_changes;       size_t yy_action_n;
    change_u16 *yy_lookahead_changes;    size_t yy_lookahead_n;
    change_i32 *yy_shift_ofst_changes;   size_t yy_shift_ofst_n;
    change_i32 *yy_reduce_ofst_changes;  size_t yy_reduce_ofst_n;
    change_u16 *yy_default_changes;      size_t yy_default_n;
    change_i16 *yy_rule_info_lhs_changes;  size_t yy_rule_info_lhs_n;
    change_i8  *yy_rule_info_nrhs_changes; size_t yy_rule_info_nrhs_n;
};

/* ------------------------------------------------------------------ */
/*  Helpers: diff per typed array                                     */
/* ------------------------------------------------------------------ */

#define DIFF_TYPED(TYPE, REC)                                                \
static int diff_##TYPE(const TYPE *old_arr, size_t old_n,                    \
                       const TYPE *new_arr, size_t new_n,                    \
                       REC **out, size_t *out_n) {                           \
    *out = NULL; *out_n = 0;                                                 \
    if (new_n == 0) return 0;                                                \
    REC *buf = (REC *)malloc(sizeof(REC) * new_n);                           \
    if (buf == NULL) return -1;                                              \
    size_t k = 0;                                                            \
    size_t common = (old_n < new_n ? old_n : new_n);                         \
    for (size_t i = 0; i < common; i++) {                                    \
        if (old_arr == NULL || old_arr[i] != new_arr[i]) {                   \
            buf[k].offset = (uint32_t)i;                                     \
            buf[k].new_value = new_arr[i];                                   \
            k++;                                                             \
        }                                                                    \
    }                                                                        \
    for (size_t i = common; i < new_n; i++) {                                \
        buf[k].offset = (uint32_t)i;                                         \
        buf[k].new_value = new_arr[i];                                       \
        k++;                                                                 \
    }                                                                        \
    if (k == 0) { free(buf); buf = NULL; }                                   \
    *out = buf;                                                              \
    *out_n = k;                                                              \
    return 0;                                                                \
}

DIFF_TYPED(uint16_t, change_u16)
DIFF_TYPED(int32_t,  change_i32)
DIFF_TYPED(int16_t,  change_i16)
DIFF_TYPED(int8_t,   change_i8)

/* ------------------------------------------------------------------ */
/*  Helpers: apply per typed array                                    */
/* ------------------------------------------------------------------ */

#define APPLY_TYPED(TYPE, REC)                                               \
static int apply_##TYPE(TYPE **out_arr, size_t out_n,                        \
                        const TYPE *base_arr, size_t base_n,                 \
                        const REC *changes, size_t n_changes) {              \
    if (out_n == 0) { *out_arr = NULL; return 0; }                           \
    TYPE *buf = (TYPE *)calloc(out_n, sizeof(TYPE));                         \
    if (buf == NULL) return -1;                                              \
    size_t copy_n = (base_n < out_n ? base_n : out_n);                       \
    if (base_arr != NULL && copy_n > 0) {                                    \
        memcpy(buf, base_arr, copy_n * sizeof(TYPE));                        \
    }                                                                        \
    for (size_t i = 0; i < n_changes; i++) {                                 \
        if (changes[i].offset >= out_n) { free(buf); return -1; }            \
        buf[changes[i].offset] = changes[i].new_value;                       \
    }                                                                        \
    *out_arr = buf;                                                          \
    return 0;                                                                \
}

APPLY_TYPED(uint16_t, change_u16)
APPLY_TYPED(int32_t,  change_i32)
APPLY_TYPED(int16_t,  change_i16)
APPLY_TYPED(int8_t,   change_i8)

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

static char *xstrdup_err(const char *s) {
    size_t n = strlen(s);
    char  *o = (char *)malloc(n + 1);
    if (o) memcpy(o, s, n + 1);
    return o;
}

int lime_snapshot_diff(const struct ParserSnapshot *old_snap,
                       const struct ParserSnapshot *new_snap,
                       LimeSnapshotDiff **out_diff,
                       char **error) {
    if (out_diff == NULL || new_snap == NULL) {
        if (error) *error = xstrdup_err("lime_snapshot_diff: bad arguments");
        if (out_diff) *out_diff = NULL;
        return -1;
    }
    if (error) *error = NULL;
    *out_diff = NULL;

    LimeSnapshotDiff *d = (LimeSnapshotDiff *)calloc(1, sizeof(*d));
    if (d == NULL) {
        if (error) *error = xstrdup_err("lime_snapshot_diff: out of memory");
        return -1;
    }

    /* Header counts.  Use -1 for fields not present in old. */
    d->new_nstate         = new_snap->nstate;
    d->new_nrule          = new_snap->nrule;
    d->new_nterminal      = new_snap->nterminal;
    d->new_nsymbol        = new_snap->nsymbol;
    d->new_action_count   = new_snap->action_count;
    d->new_lookahead_count= new_snap->lookahead_count;
    if (old_snap != NULL) {
        d->old_nstate          = old_snap->nstate;
        d->old_nrule           = old_snap->nrule;
        d->old_nterminal       = old_snap->nterminal;
        d->old_nsymbol         = old_snap->nsymbol;
        d->old_action_count    = old_snap->action_count;
        d->old_lookahead_count = old_snap->lookahead_count;
    } else {
        d->old_nstate = d->old_nrule = d->old_nterminal = -1;
        d->old_nsymbol = -1;
        d->old_action_count = d->old_lookahead_count = -1;
    }

    /* Action-table dispatch constants -- always copied verbatim. */
    d->yy_max_shift        = new_snap->yy_max_shift;
    d->yy_min_shiftreduce  = new_snap->yy_min_shiftreduce;
    d->yy_max_shiftreduce  = new_snap->yy_max_shiftreduce;
    d->yy_error_action     = new_snap->yy_error_action;
    d->yy_accept_action    = new_snap->yy_accept_action;
    d->yy_no_action        = new_snap->yy_no_action;
    d->yy_min_reduce       = new_snap->yy_min_reduce;
    d->yy_ntoken           = new_snap->yy_ntoken;

    /* Per-table diffs. */
#define DO(field, diff_call, n_field)                                        \
    do {                                                                     \
        if (diff_call(                                                       \
                old_snap ? old_snap->field : NULL,                           \
                old_snap ? old_snap->n_field : 0,                            \
                new_snap->field, new_snap->n_field,                          \
                &d->field##_changes, &d->field##_n) != 0) {                  \
            lime_snapshot_diff_release(d);                                   \
            if (error) *error = xstrdup_err("lime_snapshot_diff: oom");      \
            return -1;                                                       \
        }                                                                    \
    } while (0)

    DO(yy_action,         diff_uint16_t, action_count);
    DO(yy_lookahead,      diff_uint16_t, lookahead_count);
    DO(yy_shift_ofst,     diff_int32_t,  nstate);
    DO(yy_reduce_ofst,    diff_int32_t,  nstate);
    DO(yy_default,        diff_uint16_t, nstate);
    DO(yy_rule_info_lhs,  diff_int16_t,  nrule);
    DO(yy_rule_info_nrhs, diff_int8_t,   nrule);
#undef DO

    *out_diff = d;
    return 0;
}

int lime_snapshot_apply_diff(const struct ParserSnapshot *base,
                             const LimeSnapshotDiff *diff,
                             struct ParserSnapshot **out_snap,
                             char **error) {
    if (diff == NULL || out_snap == NULL) {
        if (error) *error = xstrdup_err("lime_snapshot_apply_diff: bad args");
        if (out_snap) *out_snap = NULL;
        return -1;
    }
    if (error) *error = NULL;
    *out_snap = NULL;

    struct ParserSnapshot *snap =
        (struct ParserSnapshot *)calloc(1, sizeof(*snap));
    if (snap == NULL) {
        if (error) *error = xstrdup_err("lime_snapshot_apply_diff: oom");
        return -1;
    }

    /* Initialize refcount to 1 -- caller owns one reference. */
    atomic_init(&snap->refcount, 1);

    /* Header counts come from the diff. */
    snap->nstate          = (uint32_t)diff->new_nstate;
    snap->nrule           = (uint32_t)diff->new_nrule;
    snap->nterminal       = (uint32_t)diff->new_nterminal;
    snap->nsymbol         = (uint32_t)diff->new_nsymbol;
    snap->action_count    = (uint32_t)diff->new_action_count;
    snap->lookahead_count = (uint32_t)diff->new_lookahead_count;

    /* Dispatch constants. */
    snap->yy_max_shift       = diff->yy_max_shift;
    snap->yy_min_shiftreduce = diff->yy_min_shiftreduce;
    snap->yy_max_shiftreduce = diff->yy_max_shiftreduce;
    snap->yy_error_action    = diff->yy_error_action;
    snap->yy_accept_action   = diff->yy_accept_action;
    snap->yy_no_action       = diff->yy_no_action;
    snap->yy_min_reduce      = diff->yy_min_reduce;
    snap->yy_ntoken          = diff->yy_ntoken;

#define APPLY(field, apply_call, base_count, new_count)                      \
    do {                                                                     \
        if (apply_call(                                                      \
                &snap->field, (size_t)diff->new_##new_count,                 \
                base ? base->field : NULL,                                   \
                base ? base->base_count : 0,                                 \
                diff->field##_changes, diff->field##_n) != 0) {              \
            snap->refcount = 0;                                              \
            free(snap->yy_action);                                           \
            free(snap->yy_lookahead);                                        \
            free(snap->yy_shift_ofst);                                       \
            free(snap->yy_reduce_ofst);                                      \
            free(snap->yy_default);                                          \
            free(snap->yy_rule_info_lhs);                                    \
            free(snap->yy_rule_info_nrhs);                                   \
            free(snap);                                                      \
            if (error) *error =                                              \
                xstrdup_err("lime_snapshot_apply_diff: apply failed");       \
            return -1;                                                       \
        }                                                                    \
    } while (0)

    APPLY(yy_action,         apply_uint16_t, action_count,    action_count);
    APPLY(yy_lookahead,      apply_uint16_t, lookahead_count, lookahead_count);
    APPLY(yy_shift_ofst,     apply_int32_t,  nstate,          nstate);
    APPLY(yy_reduce_ofst,    apply_int32_t,  nstate,          nstate);
    APPLY(yy_default,        apply_uint16_t, nstate,          nstate);
    APPLY(yy_rule_info_lhs,  apply_int16_t,  nrule,           nrule);
    APPLY(yy_rule_info_nrhs, apply_int8_t,   nrule,           nrule);
#undef APPLY

    *out_snap = snap;
    return 0;
}

void lime_snapshot_diff_release(LimeSnapshotDiff *d) {
    if (d == NULL) return;
    free(d->yy_action_changes);
    free(d->yy_lookahead_changes);
    free(d->yy_shift_ofst_changes);
    free(d->yy_reduce_ofst_changes);
    free(d->yy_default_changes);
    free(d->yy_rule_info_lhs_changes);
    free(d->yy_rule_info_nrhs_changes);
    free(d);
}

size_t lime_snapshot_diff_summary(const LimeSnapshotDiff *d,
                                  char *buf, size_t buflen) {
    if (d == NULL) {
        if (buf && buflen > 0) buf[0] = 0;
        return 0;
    }

    /* Compute total bytes in the change records. */
    size_t total =
        d->yy_action_n        * sizeof(change_u16) +
        d->yy_lookahead_n     * sizeof(change_u16) +
        d->yy_shift_ofst_n    * sizeof(change_i32) +
        d->yy_reduce_ofst_n   * sizeof(change_i32) +
        d->yy_default_n       * sizeof(change_u16) +
        d->yy_rule_info_lhs_n * sizeof(change_i16) +
        d->yy_rule_info_nrhs_n* sizeof(change_i8);

    int n = snprintf(buf, buflen,
                     "snapshot diff: nstate %lld->%lld, "
                     "nrule %lld->%lld, "
                     "nterminal %lld->%lld; "
                     "actions: %zu changes, "
                     "lookahead: %zu changes, "
                     "shift_ofst: %zu changes, "
                     "reduce_ofst: %zu changes, "
                     "default: %zu changes, "
                     "rule_lhs: %zu changes, "
                     "rule_nrhs: %zu changes; "
                     "total %zu bytes",
                     (long long)d->old_nstate, (long long)d->new_nstate,
                     (long long)d->old_nrule,  (long long)d->new_nrule,
                     (long long)d->old_nterminal, (long long)d->new_nterminal,
                     d->yy_action_n, d->yy_lookahead_n,
                     d->yy_shift_ofst_n, d->yy_reduce_ofst_n,
                     d->yy_default_n,
                     d->yy_rule_info_lhs_n, d->yy_rule_info_nrhs_n,
                     total);
    return (n < 0) ? 0 : (size_t)n;
}
