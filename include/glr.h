/**
 * @file glr.h
 * @brief Generalized LR (GLR) parser support for Lime.
 *
 * GLR parsing extends the underlying LALR(1) machinery so a runtime
 * shift/reduce or reduce/reduce conflict produces a *fork* in the parse
 * stack rather than an error.  Heads that converge on the same parser
 * state are merged.  When two reductions produce the same nonterminal
 * at the same position the user-provided disambiguation callback is
 * consulted.
 *
 * GLR is built on top of a Graph-Structured Stack (GSS) -- each node
 * is a parser state with reference-counted predecessor links, so two
 * parallel parses can share a common prefix without copying the stack.
 *
 * The GLR engine consumes the same @ref ParserSnapshot tables that the
 * LALR runtime drives.  No code generation changes are needed: any
 * grammar that compiles to a snapshot can be parsed in either mode.
 *
 * @par When to use GLR
 * Use GLR for grammars that are inherently ambiguous (natural-language
 * parsers, dialect-rich SQL with overloaded operators, scope-sensitive
 * syntactic categories) or when you want to avoid hand-encoding
 * lookahead in semantic actions.  For unambiguous grammars LALR is
 * faster -- see docs/GLR.md for measured numbers.
 *
 * @see docs/GLR.md, tests/test_glr_no_conflict.c, tests/test_glr_ambiguous.c
 */
#ifndef LIME_GLR_H
#define LIME_GLR_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct LimeArena LimeArena;
typedef struct ParserSnapshot ParserSnapshot;

/**
 * @brief Graph-Structured Stack (GSS) node for GLR parsing.
 *
 * Each node represents a parser state in the GLR graph.  Multiple
 * predecessor links allow the graph structure that distinguishes
 * GLR from standard LR parsing.
 */
typedef struct GSSNode {
    uint32_t state; /**< Parser state number */
    /** Semantic value (one of int / pointer / double, by convention). */
    union {
        int ival;
        void *pval;
        double dval;
    } value;
    struct GSSNode **predecessors; /**< Array of predecessor nodes */
    uint32_t npred;                /**< Number of predecessors */
    uint32_t pred_capacity;        /**< Allocated predecessor slots */
    uint32_t refcount;             /**< Reference count */
#ifdef YYLOCATIONTYPE
    /** Location tracking; only present when %locations is active. */
    struct {
        uint32_t first_line, first_column, last_line, last_column;
    } location;
#endif
} GSSNode;

/**
 * @brief User-provided disambiguation callback.
 *
 * Called when two reductions produce the same non-terminal at the
 * same position (i.e., two GSS heads merge at the same state).
 * Returns 1 to prefer rule1, 2 to prefer rule2, 0 for ambiguity error.
 */
typedef int (*GLRDisambiguateFn)(uint32_t rule1_index, uint32_t rule2_index, void *user_data);

/**
 * @brief GLR parser context, wrapping an underlying LALR(1) parser.
 *
 * Manages the set of active parse stack heads (the "frontier") and
 * performs forking on conflicts and merging on convergence.
 */
typedef struct GLRParser {
    GSSNode **active_heads;         /**< Array of active stack top nodes */
    uint32_t nheads;                /**< Number of active heads */
    uint32_t max_heads;             /**< Capacity of @ref active_heads */
    LimeArena *arena;               /**< Arena for GSSNode allocation */
    GLRDisambiguateFn disambiguate; /**< User disambiguation callback */
    void *disambiguate_data;        /**< User data for callback */
    uint32_t total_forks;           /**< Statistics: total forks */
    uint32_t total_merges;          /**< Statistics: total merges */
    bool has_ambiguity;             /**< True if unresolved ambiguity detected */
} GLRParser;

/**
 * @brief Create a GLR parser.
 *
 * @param initial_state The LALR start state (typically 0).
 * @param arena_size    Initial arena block size (0 for the default 64 KiB).
 * @return New GLR parser, or NULL on allocation failure.
 */
GLRParser *glr_parser_create(uint32_t initial_state, size_t arena_size);

/**
 * @brief Destroy a GLR parser and free all resources.
 *
 * Passing NULL is safe.
 */
void glr_parser_destroy(GLRParser *parser);

/**
 * @brief Feed a token to the GLR parser.
 *
 * For each active stack head, the snapshot's action table is
 * consulted.  When both a shift and a reduce are valid (S/R conflict)
 * the head is forked: one copy shifts, the other reduces.  After
 * processing all heads, heads that arrived at the same state are
 * merged into one.
 *
 * The snapshot supplies every constant that the original 19-parameter
 * version of this function took -- the action arrays, the dispatch
 * range constants (yy_min_shiftreduce, yy_min_reduce, ...), and the
 * per-rule LHS/nrhs metadata.  Pass the same snapshot pointer that
 * was used to drive the LALR engine.
 *
 * @param parser  Active GLR parser.
 * @param snap    Snapshot whose action tables to consult.
 * @param token   Terminal symbol code.
 *
 * @retval  0 Success.
 * @retval -1 Unresolvable ambiguity (no disambiguation callback set
 *            and two heads converged).
 * @retval -2 All heads died (syntax error on every active path).
 */
int glr_parser_feed(GLRParser *parser, const ParserSnapshot *snap, uint16_t token);

/**
 * @brief Set the disambiguation callback.
 *
 * @param parser     Active GLR parser.
 * @param fn         Callback function (NULL to clear).
 * @param user_data  Opaque pointer passed back into @p fn.
 */
void glr_parser_set_disambiguate(GLRParser *parser, GLRDisambiguateFn fn, void *user_data);

/**
 * @brief Return the number of active parse heads.
 *
 * @retval 1 Unambiguous (single live head).
 * @retval >1 Ambiguous prefix; multiple heads still live.
 * @retval 0 All heads died.
 */
uint32_t glr_parser_head_count(const GLRParser *parser);

/**
 * @brief Check if the parse completed successfully.
 *
 * @param parser        Active GLR parser.
 * @param accept_action Snapshot accept-action constant.
 * @return true iff exactly one live head sits in the accept state.
 */
bool glr_parser_accepted(const GLRParser *parser, uint16_t accept_action);

/* GSSNode management (exposed for testing and embedding integrations). */
GSSNode *gss_node_create(LimeArena *arena, uint32_t state);
void gss_node_add_predecessor(GSSNode *node, GSSNode *pred);
GSSNode *gss_node_acquire(GSSNode *node);
void gss_node_release(GSSNode *node);

/* ----------------------------------------------------------------------
 * Public GLR API (parse_glr.c).
 *
 * These are the user-facing entry points that lift the GLR engine
 * above into the same conceptual world as the LALR push parser.
 * The LALR fast path (parse_token()) is intentionally untouched;
 * users who never call lime_parse_glr() pay zero cost for GLR.
 *
 * Lifecycle:
 *   ParseContext *ctx = parse_begin(snap);
 *   if (lime_parse_glr(ctx, my_disambig, my_data) != 0) ...
 *   while (have_more_tokens) {
 *       int rc = lime_parse_glr_feed(ctx, token);
 *       if (rc < 0) break;
 *   }
 *   bool ok = lime_parse_glr_accepted(ctx);
 *   lime_parse_glr_end(ctx);
 *   parse_end(ctx);
 * ---------------------------------------------------------------------- */

/* Forward declaration so the prototypes below don't pull in parser.h. */
struct ParseContext;

/**
 * @brief Enter GLR mode for an existing ParseContext.
 *
 * @param ctx        Active ParseContext (from parse_begin).
 * @param disambig   Disambiguation callback (NULL = report ambiguity).
 * @param user_data  Opaque pointer passed to disambig callback.
 * @return 0 on success, non-zero on failure.
 */
int lime_parse_glr(struct ParseContext *ctx,
                   GLRDisambiguateFn disambig,
                   void *user_data);

/**
 * @brief Feed one token into the GLR engine.
 *
 * @retval  0 Token consumed successfully.
 * @retval -1 Unresolvable ambiguity.
 * @retval -2 All parse heads died.
 */
int lime_parse_glr_feed(struct ParseContext *ctx, uint16_t token);

/**
 * @brief Check whether the GLR parse accepted the input.
 */
bool lime_parse_glr_accepted(struct ParseContext *ctx);

/**
 * @brief Number of active parse heads.
 */
uint32_t lime_parse_glr_head_count(struct ParseContext *ctx);

/**
 * @brief Tear down GLR-mode state on the ParseContext.
 */
void lime_parse_glr_end(struct ParseContext *ctx);

#ifdef __cplusplus
}
#endif

#endif /* LIME_GLR_H */
