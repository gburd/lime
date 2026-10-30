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
/*
** Text form (a file, or the body of `%policy { ... }`), tokenised like
** .lime; every statement ends with '.':
**
**   rank timescale 50.                       base has rank 0
**   default reject.                          reject (default) | prefer-base | prefer-rank
**   prefer-base.  prefer-extension.          shorthand for the two default modes
**   token JSON_ARROW prec-like OP assoc left.
**   token JSON_ARROW prec 12 assoc right.    raw level (counts precedence lines)
**   conflict sr "upsert_cols ::=" on LPAREN prefer shift.
**   conflict rr "a ::= B" prefer reduce.     keep the reduce by that rule
**   conflict sr "a_expr ::= a_expr DPIPE a_expr" prefer pgvector.
**   reject "stmt ::= showExtStmt" from pgvector.
**   reject token SHOW_EXT_K from pgvector.
**   trust-extension-proposals mysql_compat.  accept its on_conflict answers
**   keyword "connect" owner oracle_compat soft.   (lexeme ownership; C4)
**
** Order of authority for one conflict: a `conflict` line > rank > `default`.
** Rank decides extension-vs-extension conflicts whose owners have different
** ranks; it decides base-vs-extension conflicts (base = rank 0) only under
** `default prefer-rank`.  Equal ranks on a real conflict between two
** extensions are an error, even under prefer-rank.  A rule that loses
** every conflict it is in becomes dead (never reduced): it is reported,
** not an error.  Comments are C style.
*/

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

/** `rank NAME N.` */
typedef struct LimePolicyRank {
    const char *owner;
    int rank;
} LimePolicyRank;

/** `token T prec-like U|prec N [assoc left|right|nonassoc].` */
typedef struct LimePolicyToken {
    const char *token;
    const char *like;  /**< NULL = use `level` */
    int level;
    int assoc;         /**< LIME_ASSOC_* (lime_compiler.h) */
} LimePolicyToken;

/** `reject "<rule>" from EXT.` / `reject token T from EXT.` */
typedef struct LimePolicyReject {
    int is_token;
    const char *what;  /**< rule identity or token name */
    const char *owner;
} LimePolicyReject;

/** `keyword "lexeme" owner EXT soft|reserved.` (see C4 / lime_lex_compose) */
typedef struct LimePolicyKeyword {
    const char *lexeme;
    const char *owner;
    int soft;
} LimePolicyKeyword;

typedef enum LimePolicyDefault {
    LIME_DEFAULT_REJECT = 0,     /**< every new conflict is an error (the default) */
    LIME_DEFAULT_PREFER_BASE,    /**< base vs extension: keep the base's action */
    LIME_DEFAULT_PREFER_EXTENSION, /**< base vs extension: keep the extension's */
    LIME_DEFAULT_PREFER_RANK     /**< higher rank wins; equal rank = error */
} LimePolicyDefault;

/** The host's policy.  All pointers are borrowed for the compose call
 *  (lime_policy_parse returns one that owns everything). */
typedef struct LimeComposePolicy {
    const LimePolicyConflict *conflicts;
    uint32_t nconflicts;
    const LimePolicyRank *ranks;
    uint32_t nranks;
    const LimePolicyToken *tokens;
    uint32_t ntokens;
    const LimePolicyReject *rejects;
    uint32_t nrejects;
    const LimePolicyKeyword *keywords;
    uint32_t nkeywords;
    const char *const *trusted; /**< extensions whose on_conflict answers apply */
    uint32_t ntrusted;
    int default_mode;           /**< LimePolicyDefault */
} LimeComposePolicy;

/**
 * Parse policy text.  Accepts a bare policy file or a grammar containing
 * `%policy { ... }` (only the block is read).  Returns a policy that owns
 * all its strings (free with lime_policy_free), or NULL with *error set
 * ("policy:LINE: ...").
 */
LimeComposePolicy *lime_policy_parse(const char *text, size_t len, char **error);
void lime_policy_free(LimeComposePolicy *p);

/** Rank of `owner` under `p` ("base" = 0; unlisted extensions = 0). */
int lime_policy_rank(const LimeComposePolicy *p, const char *owner);

/** True if `trust-extension-proposals owner.` is present. */
int lime_policy_trusts(const LimeComposePolicy *p, const char *owner);

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
    int prec_lookahead;  /**< -1 = the lookahead has no precedence */
    int prec_b;          /**< -1 = rule b has no precedence */
    char *resolution;    /**< the policy line that resolved it, or NULL = unresolved */
    char *tie;           /**< non-NULL: unresolved because two owners have equal rank */
} LimeComposeEntry;

typedef struct LimeComposeReport {
    LimeComposeEntry *entries; /**< sorted by key */
    uint32_t n;
    LimeComposeSide *dead;     /**< rules no state reduces because they lost a conflict */
    uint32_t ndead;
    uint32_t nunresolved;
    uint32_t nresolved;
    int base_nconflict; /**< conflicts of the base alone (lime's count) */
    int base_nexpect;   /**< the base's %expect, -1 if none */
    uint32_t nextensions; /**< extensions composed */
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
/** Map "<grammar text>:N:" positions in a compile error to "file:line" of
 *  the owning unit (base or extension).  malloc'd. */
char *lime_compose_locate_errors(const LimeComposeSession *s, const char *msg);
/** Number of unresolved extension-introduced conflicts; *report optional. */
uint32_t lime_compose_finish(LimeComposeSession *s, LimeComposeReport **report);
void lime_compose_session_free(LimeComposeSession *s);

#ifdef __cplusplus
}
#endif

#endif /* LIME_POLICY_H */
