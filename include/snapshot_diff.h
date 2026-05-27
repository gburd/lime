/*
 * @file snapshot_diff.h
 * @brief Binary diff/patch for ParserSnapshots.
 *
 * Enables incremental composition: when a multi-extension daemon
 * startup composes the same set of grammars repeatedly with one
 * extension swapped, the diff between consecutive composed
 * snapshots is small.  Computing and applying the diff is faster
 * than rebuilding the full LALR table from grammar source.
 *
 * Available since v0.6.x.  No production consumer wires this into
 * compose_snapshots() yet; the API ships as a building block for
 * future incremental-composition work.
 */

#ifndef LIME_SNAPSHOT_DIFF_H
#define LIME_SNAPSHOT_DIFF_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ParserSnapshot;

typedef struct LimeSnapshotDiff LimeSnapshotDiff;

/**
 * Compute a binary diff from `old_snap` to `new_snap`.
 *
 * @param old_snap Source snapshot.  May be NULL for "diff from
 *                 empty" -- the resulting diff applied to NULL
 *                 reconstructs `new_snap` from scratch.
 * @param new_snap Target snapshot.  Must be non-NULL.
 * @param out_diff On success, *out_diff receives a heap-allocated
 *                 diff struct.  Caller releases with
 *                 lime_snapshot_diff_release().
 * @param error    On failure, *error receives a heap-allocated
 *                 string the caller free()s.  May be NULL on
 *                 success.  Pass NULL to suppress error reporting.
 *
 * @retval 0 on success; *out_diff is non-NULL.
 * @retval -1 on bad arguments or out-of-memory; *out_diff is NULL.
 */
int lime_snapshot_diff(const struct ParserSnapshot *old_snap,
                       const struct ParserSnapshot *new_snap,
                       LimeSnapshotDiff           **out_diff,
                       char                       **error);

/**
 * Apply a diff to a base snapshot, producing a new snapshot
 * functionally equivalent to the one the diff was computed against.
 *
 * @param base       Base snapshot.  Pass NULL when the diff was
 *                   computed against NULL (full snapshot).
 * @param diff       Diff to apply.  Must be non-NULL.
 * @param out_snap   *out_snap receives a heap-allocated
 *                   ParserSnapshot.  Caller calls
 *                   snapshot_release() to free.
 * @param error      Failure reason; same convention as
 *                   lime_snapshot_diff above.
 *
 * @retval 0 on success.
 * @retval -1 on failure with *error populated.
 */
int lime_snapshot_apply_diff(const struct ParserSnapshot *base,
                             const LimeSnapshotDiff      *diff,
                             struct ParserSnapshot      **out_snap,
                             char                       **error);

/**
 * Release a diff and its owned tables.  Pass NULL is a no-op.
 */
void lime_snapshot_diff_release(LimeSnapshotDiff *diff);

/**
 * Diagnostic: write a human-readable summary of a diff into `buf`.
 *
 * Format:
 *   "snapshot diff: nstate %d->%d, nrule %d->%d, nterminal %d->%d,
 *    actions: %zu changes, lookahead: %zu changes,
 *    rule_lhs: %zu changes, rule_nrhs: %zu changes,
 *    total bytes %zu"
 *
 * @return Number of bytes that would have been written (snprintf
 *         convention).  May exceed buflen if the buffer was too
 *         small; caller checks for that.
 */
size_t lime_snapshot_diff_summary(const LimeSnapshotDiff *diff,
                                  char                   *buf,
                                  size_t                  buflen);

#ifdef __cplusplus
}
#endif

#endif /* LIME_SNAPSHOT_DIFF_H */
