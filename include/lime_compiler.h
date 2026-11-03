/**
 * @file lime_compiler.h
 * @brief In-process Lime grammar compilation.
 *
 * The only way Lime builds parse tables at runtime:
 * lime_compile_grammar_text / create_base_snapshot (src/snapshot_create.c)
 * and publish_modified_snapshot (src/compose.c) call it.  There is no
 * subprocess pipeline: Lime never runs `lime` or a C compiler and never
 * loads a shared object at runtime.
 *
 * Available since: v0.5.4 (ROADMAP item 1, phase 3).
 */

#ifndef LIME_COMPILER_H
#define LIME_COMPILER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ParserSnapshot;

/**
 * @brief Compile grammar text into a runtime ParserSnapshot, in-process.
 *
 * @param grammar_text  NUL-terminated grammar source.  Must outlive
 *                      this call only -- the function copies what
 *                      it needs.
 * @param len           Length of grammar_text in bytes (NOT including
 *                      NUL).
 * @param out_snapshot  On success, *out_snapshot points at a heap-
 *                      allocated ParserSnapshot.  Caller calls
 *                      snapshot_release() to free.
 * @param error         On failure, *error points at a heap-allocated
 *                      error message describing the parse error,
 *                      conflict, or LALR construction failure.
 *                      Caller free()s.  May be NULL on success.
 *
 * @retval 0 on success; *out_snapshot is non-NULL.
 * @retval non-zero on error; *out_snapshot is NULL, *error explains.
 *
 * Thread safety: each call must use a fresh LimeCompilerContext
 * internally.  At present (v0.5.4) the active-context pointer is
 * not __thread-local, so two concurrent calls in different threads
 * race.  Phase 5 will add thread-local storage when concurrent
 * compilation is required.  Sequential calls in the same thread
 * are fully isolated.
 */
int lime_compile_grammar_in_process(const char *grammar_text,
                                    size_t len,
                                    struct ParserSnapshot **out_snapshot,
                                    char **error);

/**
 * @brief Like lime_compile_grammar_in_process, but also reports the
 *        number of LALR conflicts the compose resolved.
 *
 * lemon resolves reduce/reduce conflicts (keep the earlier rule) and
 * shift/reduce conflicts (keep the shift) SILENTLY: they do not fail
 * the build.  So when a composed fragment contributes a rule that
 * shadows an already-loaded dialect's rule -- same RHS reducing to the
 * same LHS, or a shift/reduce overlap -- the snapshot builds and one
 * meaning is chosen by table-build order, not the author's intent.
 *
 * @param out_nconflict  On success, receives the count of conflicts
 *                       resolved during table construction.  0 = clean
 *                       compose.  >0 = at least one fragment rule
 *                       collided and lemon picked a winner
 *                       deterministically (keep-first / keep-shift);
 *                       the caller should treat this as "this grammar
 *                       conflicts with an already-loaded dialect" and
 *                       refuse the load or warn the author rather than
 *                       ship a snapshot that silently mis-parses.
 *                       May be NULL.
 *
 * Same return/ownership contract: 0 on success (*out_snapshot
 * non-NULL), non-zero on a hard error (parse error, empty grammar,
 * unreducible rule, LALR construction failure -- distinct from a
 * resolved conflict, which still returns 0).
 */
int lime_compile_grammar_in_process_ex(const char *grammar_text,
                                       size_t len,
                                       struct ParserSnapshot **out_snapshot,
                                       char **error,
                                       int *out_nconflict);

/* ------------------------------------------------------------------ */
/*  Conflict hook (compose-time only; see include/lime_policy.h)       */
/* ------------------------------------------------------------------ */

/** Kind of an LALR conflict precedence could not settle. */
typedef enum LimeCompileConflictKind {
    LIME_CK_SR = 1, /**< shift/reduce */
    LIME_CK_RR = 2, /**< reduce/reduce */
    LIME_CK_SS = 3  /**< shift/shift (pathological; never resolvable) */
} LimeCompileConflictKind;

/**
 * One unresolved conflict, as FindActions() sees it in one state.  All
 * strings are valid for the duration of the hook call only.
 *
 * Rule identities are the canonical "lhs ::= rhs1 rhs2" text that
 * snapshot->rule_ids[] carries: stable across composes, unlike state
 * numbers.  For SR, rule_a is a shift-side rule (a config with the dot
 * before the lookahead; display only, it is not part of the conflict's
 * identity) and rule_b the reduce rule.  For RR both are reduce rules,
 * rule_a the one Lemon would keep by default.  NULL for SS.
 */
typedef struct LimeConflictInfo {
    int kind;               /**< LimeCompileConflictKind */
    const char *lookahead;  /**< terminal name */
    const char *rule_a;
    const char *rule_b;
    int line_a, line_b;     /**< line of the rule in the compiled text */
    int dot_a;              /**< SR: position of the dot in rule_a */
    int prec_lookahead;     /**< precedence of the lookahead, -1 if none */
    int prec_b;             /**< precedence of rule_b (its precsym), -1 if none */
    int state;              /**< state number (cosmetic, NOT stable) */
    const char *example;    /**< a symbol path from state 0 to this state,
                                 "A B c . LOOKAHEAD" (may be NULL) */
} LimeConflictInfo;

/** What the hook decides for one conflict occurrence. */
typedef enum LimeConflictChoice {
    LIME_CHOOSE_NONE = 0, /**< leave it: Lemon's default, still counted */
    LIME_CHOOSE_SHIFT,    /**< SR: keep the shift (drop the reduce) */
    LIME_CHOOSE_REDUCE,   /**< SR: keep the reduce (drop the shift) */
    LIME_CHOOSE_KEEP_A,   /**< RR: keep rule_a */
    LIME_CHOOSE_KEEP_B    /**< RR: keep rule_b */
} LimeConflictChoice;

typedef LimeConflictChoice (*LimeConflictHookFn)(void *user, const LimeConflictInfo *c);

/** Associativity for a precedence override. */
enum { LIME_ASSOC_INHERIT = 0, LIME_ASSOC_LEFT, LIME_ASSOC_RIGHT, LIME_ASSOC_NONASSOC };

/**
 * Give terminal `token` a precedence before rule precedences are computed,
 * exactly as a %left/%right/%nonassoc line would: either the level of
 * terminal `like`, or the raw level `level` (levels count the grammar's
 * precedence lines in order, 1 = first).  `assoc` INHERIT takes `like`'s.
 */
typedef struct LimePrecOverride {
    const char *token;
    const char *like;  /**< NULL = use `level` */
    int level;
    int assoc;         /**< LIME_ASSOC_* */
} LimePrecOverride;

typedef struct LimeCompileHook {
    LimeConflictHookFn on_conflict; /**< called once per unresolved conflict */
    void *user;
    const LimePrecOverride *prec;   /**< applied before FindRulePrecedences */
    uint32_t nprec;
    /** Called for every terminal that has a precedence, before `prec` is
     *  applied (lets a caller learn the base's precedence set). */
    void (*on_token_prec)(void *user, const char *token, int level);
    /** A rule no state reduces (Lemon: "This rule can not be reduced").
     *  Return non-zero to accept it as a dead rule (it lost a conflict the
     *  hook decided) instead of a hard error.  NULL = always an error. */
    int (*on_dead_rule)(void *user, const char *rule, int line);
} LimeCompileHook;

/** Outcome counters of one in-process compile. */
typedef struct LimeCompileStats {
    int nconflict;  /**< conflicts left unresolved (after the hook) */
    int nresolved;  /**< conflicts the hook resolved */
    int nexpect;    /**< the grammar's %expect, or -1 */
    int nstate;
    int nrule;
    int nterminal;
} LimeCompileStats;

/**
 * @brief The in-process pipeline with a conflict hook.
 *
 * Runs ParseText + LALR(1) construction.  Every conflict precedence
 * cannot settle is offered to @p hook (may be NULL); its choice is
 * applied as an ordinary SH/RD_RESOLVED action, so nothing of the hook
 * survives into the tables.  With @p out_snapshot NULL the pipeline
 * stops after FindActions (conflict analysis only; no tables packed).
 * Available in liblime_compiler and in the `lime` CLI build.
 *
 * @retval 0 on success (conflicts, resolved or not, are not errors:
 *         read @p stats); non-zero on a hard error, *error explains.
 */
int lime_compile_grammar_in_process_ex2(const char *grammar_text,
                                        size_t len,
                                        const LimeCompileHook *hook,
                                        struct ParserSnapshot **out_snapshot,
                                        LimeCompileStats *stats,
                                        char **error);

/* ------------------------------------------------------------------ */
/*  Lint (the in-process equivalent of `lime -L`)                      */
/* ------------------------------------------------------------------ */
/*
** Lint grammar text without building a snapshot.  *out_diags receives a
** malloc'd buffer with the human-format diagnostics `lime -L` prints
** (caller frees).  Returns 0 clean, >0 when errors or warnings were
** emitted, -1 when no compiler context could be set up.
**
** lime_lint_grammar_in_process runs FindActions too, so conflict
** warnings appear (~2 s on PostgreSQL's grammar);
** lime_lint_grammar_fast_in_process stops after parsing (~150 ms) and
** misses the conflict-dependent warnings.  lime-lsp uses both.
*/
int lime_lint_grammar_in_process(const char *grammar_text, size_t len,
                                 char **out_diags);
int lime_lint_grammar_fast_in_process(const char *grammar_text, size_t len,
                                      char **out_diags);

/*
** Format grammar text in process: the bytes `lime -F` would write to
** <file>.formatted, in a malloc'd *out_text (caller frees).  Returns 0 on
** success, 1 when the text does not parse (*out_text NULL; *out_diags,
** if non-NULL, gets the errors), -1 when no context could be set up.
*/
int lime_format_grammar_in_process(const char *grammar_text, size_t len,
                                   char **out_text, char **out_diags);

#ifdef __cplusplus
}
#endif

#endif /* LIME_COMPILER_H */
