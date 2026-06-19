/**
 * @file multi_grammar.h
 * @brief Runtime disambiguation for overlapping loaded grammars (Tiers 1-3).
 *
 * Three layered mechanisms for resolving collisions when several
 * grammar dialects are loaded into one composed parser.  Each tier is
 * explicit about its guarantee level -- this is deliberately NOT a
 * promise that any union of dialects "always works", which LR theory
 * forbids when two dialects share a production with different meaning.
 *
 *   Tier 1 -- full-statement fork-resolve (lime_mg_resolve):
 *       On a collision admissible in >=2 candidate (snapshot, token)
 *       pairs, simulate each candidate over the real upcoming token
 *       stream (lime_simulate_parse) and prefer the one that reaches
 *       accept / gets furthest with fewest errors.  CORRECT when the
 *       candidates diverge within the statement (the common case for
 *       "90% overlap" dialects: LIMIT vs ROWNUM, hint syntax, etc.).
 *
 *   Tier 2 -- Bayesian tie-break (LimeBayesStore):
 *       When Tier 1 ties (candidates trial identically -- e.g. truly
 *       identical productions), pick the historically-likelier dialect
 *       for this (state, token) and learn from confirmed parses.
 *       HEURISTIC: it does not make an ambiguous parse correct, it
 *       makes it resolve consistently and improve with feedback.
 *
 *   Tier 3 -- dialect mode selection (LimeDialectRegistry):
 *       For genuinely mutually-ambiguous full dialects that no
 *       parse-time method can separate, select one dialect per session
 *       or per statement (a named composed snapshot, or a leading
 *       @dialect sigil).  EXACT, but this is mode selection, not
 *       disambiguation.
 *
 * @see docs/MULTI_GRAMMAR.md
 * @see parse_context.h for lime_simulate_parse and the admissibility oracle.
 */
#ifndef MULTI_GRAMMAR_H
#define MULTI_GRAMMAR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "parse_context.h" /* ParserSnapshot, LimeForkTrial, lime_simulate_parse */

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================== */
/*  Tier 1: full-statement fork-resolve                                */
/* ================================================================== */

/* Forward-declared here; the full typedef is in the Tier 2 section. */
struct LimeBayesStore;

/**
 * @brief One candidate grammar for a collision.
 *
 * A candidate is the (composed) snapshot to try plus the priority to
 * use for the final tie-break (lower = preferred) and an ext_id used by
 * the Tier-2 Bayesian tie-break.
 */
typedef struct LimeForkCandidate {
    const ParserSnapshot *snapshot; /**< Grammar to simulate. */
    int32_t priority;               /**< Lower = preferred (final tie-break). */
    uint32_t ext_id;                /**< Extension id (Tier-2 Bayesian key). */
} LimeForkCandidate;

/**
 * @brief Per-candidate ranking detail (optional output of lime_mg_resolve).
 */
typedef struct LimeForkRank {
    LimeForkTrial trial; /**< How far this candidate got. */
    bool tied_winner;    /**< True if it tied for first on Tier-1 metrics. */
} LimeForkRank;

/**
 * @brief Resolve a collision by simulating each candidate over the
 *        upcoming token stream and ranking the outcomes (Tier 1).
 *
 * Each candidate is run through lime_simulate_parse against the SAME
 * @p lookahead buffer (the real upcoming tokens, external codes).  The
 * winner is chosen by, in order:
 *   1. reached_accept (true preferred),
 *   2. fewest errors,
 *   3. most tokens consumed (got furthest),
 *   4. lowest priority value,
 *   5. Tier-2 Bayesian posterior (if @p bayes != NULL),
 *   6. lowest ext_id (deterministic final tie-break).
 *
 * @param cands      Candidate grammars (>= 1).
 * @param ncands     Number of candidates.
 * @param lookahead  Upcoming external token codes to simulate against.
 * @param nlook      Number of lookahead tokens.
 * @param bayes      Optional Tier-2 store consulted only on a Tier-1
 *                   tie (NULL to skip).
 * @param bayes_state,bayes_token  Key for the Bayesian tie-break.
 * @param out_ranks  Optional array of @p ncands entries filled with
 *                   per-candidate detail (NULL to skip).
 * @return Index of the winning candidate, or -1 if all failed / bad args.
 */
int lime_mg_resolve(const LimeForkCandidate *cands, uint32_t ncands,
                    const int *lookahead, uint32_t nlook,
                    struct LimeBayesStore *bayes, uint16_t bayes_state,
                    uint16_t bayes_token, LimeForkRank *out_ranks);

/* ================================================================== */
/*  Tier 2: Bayesian tie-break with persistence                        */
/* ================================================================== */

/** @brief Opaque Beta-Bernoulli store keyed by (state, token, ext_id). */
typedef struct LimeBayesStore LimeBayesStore;

/** @brief Create an empty store (entries allocated lazily). */
LimeBayesStore *lime_bayes_create(void);

/** @brief Destroy a store. */
void lime_bayes_destroy(LimeBayesStore *s);

/**
 * @brief Record that @p ext_id won at (state, token) and the parse
 *        @p success-ceeded -- updates that arm's Beta posterior.
 */
void lime_bayes_observe(LimeBayesStore *s, uint16_t state, uint16_t token,
                        uint32_t ext_id, bool success);

/**
 * @brief Rank @p ext_ids at (state, token) by posterior mean.
 *
 * @return Index into @p ext_ids of the highest-posterior arm, or -1 on
 *         bad args.  @p out_confidence (optional) receives the winning
 *         posterior mean (0.5 with no evidence).
 */
int lime_bayes_rank(LimeBayesStore *s, uint16_t state, uint16_t token,
                    const uint32_t *ext_ids, uint32_t n, float *out_confidence);

/**
 * @brief Serialize the store to a flat blob for host-side persistence.
 *
 * @return Bytes written; pass @p buf == NULL to query the size needed.
 *         Returns 0 on failure (buffer too small).
 */
size_t lime_bayes_serialize(const LimeBayesStore *s, void *buf, size_t buflen);

/**
 * @brief Rebuild a store from a blob produced by lime_bayes_serialize.
 * @return New store, or NULL on bad/corrupt input.
 */
LimeBayesStore *lime_bayes_deserialize(const void *buf, size_t buflen);

/* ================================================================== */
/*  Tier 3: dialect mode selection                                     */
/* ================================================================== */

/** @brief Opaque name -> composed-snapshot registry. */
typedef struct LimeDialectRegistry LimeDialectRegistry;

/** @brief Create an empty dialect registry. */
LimeDialectRegistry *lime_dialect_registry_create(void);

/**
 * @brief Destroy the registry.  Releases the snapshot reference held
 *        for each registered dialect.
 */
void lime_dialect_registry_destroy(LimeDialectRegistry *reg);

/**
 * @brief Register a composed snapshot under a dialect name.
 *
 * Acquires a reference to @p snap (released on destroy / re-register).
 * Re-registering a name replaces the previous snapshot.
 *
 * @return true on success, false on bad args / allocation failure.
 */
bool lime_dialect_register(LimeDialectRegistry *reg, const char *name, ParserSnapshot *snap);

/**
 * @brief Look up the composed snapshot for a dialect name.
 *
 * @return Borrowed snapshot pointer (NOT acquired; valid while
 *         registered), or NULL if the name is unknown.  Pass to
 *         parse_begin (which acquires its own reference).
 */
ParserSnapshot *lime_dialect_select(const LimeDialectRegistry *reg, const char *name);

/**
 * @brief Parse a leading @dialect sigil at the start of @p input.
 *
 * Recognizes an optional `@name` prefix (ASCII letters/digits/_ after
 * '@'), looks the name up in @p reg, and reports the snapshot plus the
 * byte offset where the real statement begins (past the sigil and any
 * following whitespace).  The host owns the actual scanning of the rest
 * of the input; this is just the per-statement dialect selector.
 *
 * @param reg          Dialect registry.
 * @param input        Statement text (NUL-terminated).
 * @param[out] out_snap  Selected snapshot, or NULL if no recognized
 *                       sigil (caller falls back to a default dialect).
 * @param[out] out_offset Byte offset of the statement body in @p input.
 * @return true if a recognized @dialect sigil was consumed; false if
 *         there was no sigil (out_offset = 0) or the name was unknown.
 */
bool lime_dialect_parse_sigil(const LimeDialectRegistry *reg, const char *input,
                              ParserSnapshot **out_snap, size_t *out_offset);

#ifdef __cplusplus
}
#endif

#endif /* MULTI_GRAMMAR_H */
