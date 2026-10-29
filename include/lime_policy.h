/**
 * @file lime_policy.h
 * @brief Compose policy: who wins when extensions conflict.
 *
 * Composition rebuilds the LALR(1) tables from the base grammar text
 * plus each extension's text.  Any conflict the rebuild finds that the
 * base compiled alone does NOT have is "extension-introduced", and it
 * must be resolved explicitly by the host's policy, or the compose
 * fails.  The base grammar's %expect never absorbs it.
 *
 * Conflict identity is symbolic, never a state number:
 *   SR  "SR;<reduce rule>;<lookahead>"
 *   RR  "RR;<rule>;<rule>;<lookahead>"   (rules sorted)
 *   SS  "SS;<lookahead>"
 * where a rule is its canonical identity "lhs ::= rhs1 rhs2" (the text
 * snapshot->rule_ids[] carries).
 *
 * A policy only chooses among actions the LALR builder already has
 * (keep the shift, keep a reduce).  The chosen action lands in the
 * tables like any precedence-resolved action; nothing of the policy
 * survives into the snapshot and nothing runs per token.
 *
 * Everything here runs at compose time and lives in liblime_compiler.
 */
#ifndef LIME_POLICY_H
#define LIME_POLICY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ParserSnapshot;

/* ------------------------------------------------------------------ */
/*  Policy                                                             */
/* ------------------------------------------------------------------ */

/** What one `conflict` line chooses. */
typedef enum LimePolicyPrefer {
    LIME_PREFER_SHIFT = 1, /**< SR: keep the shift */
    LIME_PREFER_REDUCE,    /**< SR: keep the reduce; RR: keep the reduce by `rule` */
    LIME_PREFER_OWNER      /**< keep the side contributed by `owner` */
} LimePolicyPrefer;

/** `conflict sr|rr "<rule>" [on LOOKAHEAD] prefer shift|reduce|<owner>.` */
typedef struct LimePolicyConflict {
    int kind;              /**< LIME_CK_SR or LIME_CK_RR (lime_compiler.h) */
    const char *rule;      /**< SR: the reduce rule; RR: one of the two rules */
    const char *lookahead; /**< NULL = any lookahead */
    int prefer;            /**< LimePolicyPrefer */
    const char *owner;     /**< LIME_PREFER_OWNER only */
} LimePolicyConflict;

/** The host's policy.  All pointers are borrowed for the compose call. */
typedef struct LimeComposePolicy {
    const LimePolicyConflict *conflicts;
    uint32_t nconflicts;
} LimeComposePolicy;

/* ------------------------------------------------------------------ */
/*  Report                                                             */
/* ------------------------------------------------------------------ */

typedef struct LimeComposeSide {
    char *owner; /**< "base" or the extension name */
    char *rule;  /**< rule identity, NULL for SS */
    char *file;  /**< owner's file/display name */
    int line;    /**< line within the owner's own text, 0 if unknown */
} LimeComposeSide;

/** One extension-introduced conflict, aggregated over every state it occurs in. */
typedef struct LimeComposeEntry {
    int kind;            /**< LIME_CK_SR / LIME_CK_RR / LIME_CK_SS */
    char *key;           /**< symbolic identity (see top of file) */
    char *lookahead;
    LimeComposeSide a;   /**< SR: a shift-side rule; RR: the rule Lemon keeps by default */
    LimeComposeSide b;   /**< SR: the reduce rule; RR: the other rule */
    char *example;       /**< shortest symbol path to a conflicting state */
    uint32_t nstates;    /**< states this key occurs in */
    char *resolution;    /**< the policy line that resolved it, or NULL = unresolved */
} LimeComposeEntry;

typedef struct LimeComposeReport {
    LimeComposeEntry *entries; /**< sorted by key */
    uint32_t n;
    uint32_t nunresolved;
    uint32_t nresolved;
    int base_nconflict; /**< conflicts of the base alone (lime's count) */
    int base_nexpect;   /**< the base's %expect, -1 if none */
} LimeComposeReport;

void lime_compose_report_free(LimeComposeReport *r);

/** Human-readable report (malloc'd).  Golden-tested by `lime --explain-compose`. */
char *lime_compose_report_format(const LimeComposeReport *r);

/* ------------------------------------------------------------------ */
/*  Compose                                                            */
/* ------------------------------------------------------------------ */

/** One extension's grammar text. */
typedef struct LimeComposeUnit {
    const char *name; /**< owner name (unique) */
    const char *file; /**< display name in reports; NULL = name */
    const char *text;
    size_t len;
} LimeComposeUnit;

/**
 * @brief Compose base + extensions into a snapshot, under @p policy.
 *
 * The extensions are concatenated in NAME order (the result is a
 * function of the extension set, not of the array order).  Fails if any
 * extension-introduced conflict is left unresolved; then *out is NULL
 * and nothing the caller holds changes.
 *
 * @param policy  NULL = the default: every new conflict is an error.
 * @param out     receives the snapshot (refcount 1) on success.
 * @param report  optional; receives the report on success AND failure.
 * @param error   optional; malloc'd message on failure.
 * @retval 0 success; 1 unresolved conflicts; -1 any other failure.
 */
int lime_compose_text(const char *base_text, size_t base_len, const LimeComposeUnit *units,
                      uint32_t nunits, const LimeComposePolicy *policy,
                      struct ParserSnapshot **out, LimeComposeReport **report, char **error);

/* ---- Lower level: the snapshot-free half (also used by the CLI) ---- */

typedef struct LimeComposeSession LimeComposeSession;
struct LimeCompileHook;

/** Build the merged text and the base's conflict-key set (cached per base text). */
LimeComposeSession *lime_compose_begin(const char *base_text, size_t base_len,
                                       const char *base_file, const LimeComposeUnit *units,
                                       uint32_t nunits, const LimeComposePolicy *policy,
                                       char **error);
const char *lime_compose_merged_text(const LimeComposeSession *s, size_t *len);
/** Pass to lime_compile_grammar_in_process_ex2() for the merged text. */
const struct LimeCompileHook *lime_compose_hook(LimeComposeSession *s);
/** Number of unresolved extension-introduced conflicts; *report optional. */
uint32_t lime_compose_finish(LimeComposeSession *s, LimeComposeReport **report);
void lime_compose_session_free(LimeComposeSession *s);

#ifdef __cplusplus
}
#endif

#endif /* LIME_POLICY_H */
