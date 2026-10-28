/*
** Fork-Resolve Disambiguation Strategy
**
** When multiple grammar extensions conflict, this strategy forks the
** parser state for each candidate interpretation and runs them in
** parallel (sequentially on the same thread, but logically independent).
** Each fork receives a copy of the parser stack and attempts to consume
** subsequent tokens using its assigned grammar.  The fork that:
**
**   1. Completes successfully (reaches an accept state)
**   2. Has the lowest priority value (lower = preferred)
**   3. Ties broken by fewest errors, then fewest tokens consumed
**
** is selected as the winner.  Forks that encounter errors or fail to
** make progress are pruned.
**
** This implements the DisambiguationStrategyVTable interface defined in
** disambiguation.h, and uses the ParseFork / ParseForkSet infrastructure
** from parser_fork.h.
**
** Tiebreaker rules:
**
**   TIEBREAK_PRIORITY       -- Lowest ParseFork.priority value wins.
**   TIEBREAK_LONGEST_MATCH  -- Fork that consumed the most tokens wins.
**   TIEBREAK_FIRST_COMPLETE -- First fork to reach FORK_COMPLETED wins
**                              (identified by lowest fork_id among winners).
**
** Lifecycle:
**   1. fork_resolve_init()    -- builds priority map from loaded extensions
**   2. fork_resolve_resolve() -- forks parser for each context in the
**      ConflictPoint, feeds a configurable number of lookahead tokens,
**      then picks the best surviving fork
**   3. fork_resolve_update()  -- optional feedback (currently no-op)
**   4. fork_resolve_destroy() -- frees all resources
**
** Thread safety: each ForkResolveContext is used by a single thread.
** The strategy is inherently single-threaded (forks execute sequentially).
*/
#include "disambiguation.h"
#include "extension.h"
#include "conflict.h"
#include "parser_fork.h"
#include "parse_context.h"
#include "snapshot.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

/* ================================================================== */
/*  Configuration constants                                            */
/* ================================================================== */

/*
** Maximum number of forks we'll create for a single conflict.
** This bounds memory usage when many extensions overlap.
*/
#define FORK_RESOLVE_MAX_FORKS 16

/*
** Default number of lookahead tokens to feed each fork before
** declaring a winner.  Higher values improve accuracy but increase
** the cost of conflict resolution.
*/
#define FORK_RESOLVE_DEFAULT_LOOKAHEAD 3

/*
** Maximum lookahead depth allowed.  Prevents runaway token feeding.
*/
#define FORK_RESOLVE_MAX_LOOKAHEAD 128

/* ================================================================== */
/*  Tiebreaker rules                                                   */
/* ================================================================== */

/*
** How to resolve ties when multiple forks complete successfully.
*/
typedef enum TiebreakRule {
    TIEBREAK_PRIORITY = 0,   /* Lowest priority value wins          */
    TIEBREAK_LONGEST_MATCH,  /* Most tokens consumed wins           */
    TIEBREAK_FIRST_COMPLETE, /* First fork to complete wins         */
} TiebreakRule;

/* ================================================================== */
/*  Per-extension metadata                                             */
/* ================================================================== */

typedef struct ForkExtEntry {
    uint32_t ext_id;
    int32_t priority;         /* Lower = preferred */
    const char *grammar_name; /* Borrowed from Extension (not owned) */
} ForkExtEntry;

/* ================================================================== */
/*  Strategy context                                                   */
/* ================================================================== */

typedef struct ForkResolveContext {
    ForkExtEntry *entries; /* Priority table for loaded extensions */
    uint32_t nentries;
    uint32_t capacity;

    TiebreakRule tiebreak;    /* How to break ties                   */
    uint32_t max_forks;       /* Upper bound on concurrent forks     */
    uint32_t lookahead_depth; /* Tokens to feed per fork             */

    /* Statistics */
    uint64_t total_resolutions;
    uint64_t total_forks_created;
    uint64_t total_forks_completed;
    uint64_t total_forks_failed;
    uint64_t total_forks_pruned;
} ForkResolveContext;

/* ================================================================== */
/*  Internal helpers                                                   */
/* ================================================================== */

static char *dup_str(const char *s) {
    if (s == NULL) return NULL;
    size_t len = strlen(s) + 1;
    char *copy = malloc(len);
    if (copy != NULL) memcpy(copy, s, len);
    return copy;
}

/*
** Look up the priority for a given extension ID.
** Returns INT32_MAX if the extension is unknown.
*/
static int32_t lookup_priority(const ForkResolveContext *fc, uint32_t ext_id) {
    if (fc == NULL) return INT32_MAX;
    for (uint32_t i = 0; i < fc->nentries; i++) {
        if (fc->entries[i].ext_id == ext_id) {
            return fc->entries[i].priority;
        }
    }
    return INT32_MAX;
}

/*
** Look up the grammar name for a given extension ID.
** Returns NULL if the extension is unknown.
*/
static const char *lookup_grammar_name(const ForkResolveContext *fc, uint32_t ext_id) {
    if (fc == NULL) return NULL;
    for (uint32_t i = 0; i < fc->nentries; i++) {
        if (fc->entries[i].ext_id == ext_id) {
            return fc->entries[i].grammar_name;
        }
    }
    return NULL;
}

/*
** Return the name of a tiebreak rule as a string.
*/
static const char *tiebreak_name(TiebreakRule rule) {
    switch (rule) {
    case TIEBREAK_PRIORITY:
        return "priority";
    case TIEBREAK_LONGEST_MATCH:
        return "longest-match";
    case TIEBREAK_FIRST_COMPLETE:
        return "first-complete";
    }
    return "unknown";
}

/* ================================================================== */
/*  Token feeding                                                      */
/* ================================================================== */

/*
** A fork can only be stepped for real if it has a snapshot whose
** action tables were populated from an actual LALR construction.
** These are exactly the fields parse_engine_step() guards on before
** it will drive a snapshot, so agreeing with it here keeps the two
** in sync: if parse_engine_step would refuse the snapshot, we treat
** the fork as static-mode instead of handing it a context that can
** only ever return -1.
**
** Returns the steppable snapshot, or NULL for a static-mode fork
** (no snapshot at all, or a metadata-only snapshot such as the ones
** built by the lightweight path in fork_resolve_resolve()).
*/
static ParserSnapshot *fork_steppable_snapshot(const ParseFork *fork) {
    if (fork == NULL) return NULL;
    ParserSnapshot *snap = fork->snapshot;
    if (snap == NULL) return NULL;
    if (snap->yy_action == NULL || snap->yy_default == NULL || snap->yy_rule_info_nrhs == NULL) {
        return NULL;
    }
    return snap;
}

/* ================================================================== */
/*  Seeding the stepping context from the cloned parser stack          */
/* ================================================================== */

/*
** Translate the cloned (grammar-specific) yyParser stack into the
** runtime engine's state stack and install it on `ctx`, so stepping
** resumes from the point the parser was forked at instead of from
** LALR state 0.
**
** How the layout is known: clone_parser_state_seedable() retains the
** two facts that make this readable -- offsetof(yyStackEntry, stateno)
** and sizeof(yyStackEntry::stateno).  Both are compile-time constants
** where yyStackEntry is visible (see PARSER_FORK_LAYOUT_PARAMS_SEEDABLE)
** and neither is derivable here: the generator picks YYACTIONTYPE as
** the narrowest unsigned type covering the grammar's largest action
** value (minimum_size_type() in lime.c yields unsigned char / unsigned
** short int / unsigned int), and per-entry padding makes
** sizeof(yyStackEntry) an ambiguous signal.  `stateno` is documented
** as the first member of yyStackEntry, but the offset is recorded
** rather than assumed.
**
** SHIFTREDUCE: per limpar.c, after the shift half of a SHIFTREDUCE
** action `stateno` holds reduce_action + (YY_MIN_REDUCE -
** YY_MIN_SHIFTREDUCE), not a plain state.  That is bit-for-bit the
** same encoding parse_engine_step() pushes for the same situation, and
** the engine's reduce branch already unpacks states >= yy_min_reduce,
** so such an entry is carried across verbatim and behaves identically
** to one the engine produced itself.  parse_engine_seed_stack()
** re-validates each value against the fork's snapshot and rejects
** anything that is neither a plain state nor a decodable pending
** reduce, so an untranslatable stack fails cleanly here rather than
** installing a wrong one.
**
** Returns true if the context was seeded, false if the fork carries no
** usable layout / stack (caller then leaves the context to start at
** state 0 as before).
*/
static bool seed_step_ctx_from_clone(ParseFork *fork, ParseContext *ctx) {
    const ClonedParserState *cs = &fork->cloned_state;

    /* stateno_size == 0 means the fork was created through the
    ** non-seedable clone_parser_state() / fork_parser(), which do not
    ** take the entry layout.  Nothing to translate from. */
    if (cs->stateno_size == 0) return false;
    if (cs->stack_data == NULL || cs->stack_depth == 0 || cs->stack_entry_size == 0) return false;

    /* Bounds: every byte we are about to read must lie inside the
    ** cloned stack buffer. */
    size_t span = (size_t)cs->stack_depth * cs->stack_entry_size;
    if (span > cs->stack_size) return false;
    if (cs->stateno_offset + cs->stateno_size > cs->stack_entry_size) return false;

    uint16_t *states = calloc(cs->stack_depth, sizeof(uint16_t));
    if (states == NULL) return false;

    bool ok = true;
    const char *entry = (const char *)cs->stack_data + cs->stateno_offset;

    for (uint32_t i = 0; i < cs->stack_depth; i++, entry += cs->stack_entry_size) {
        uint32_t raw = 0;
        switch (cs->stateno_size) {
        case 1: {
            uint8_t v;
            memcpy(&v, entry, sizeof(v));
            raw = v;
            break;
        }
        case 2: {
            uint16_t v;
            memcpy(&v, entry, sizeof(v));
            raw = v;
            break;
        }
        default: { /* 4 -- the only other width clone_* accepts */
            uint32_t v;
            memcpy(&v, entry, sizeof(v));
            raw = v;
            break;
        }
        }

        /* The engine's stack entries are uint16_t.  A 32-bit
        ** YYACTIONTYPE grammar could in principle hold a state the
        ** engine cannot represent; refuse rather than truncate. */
        if (raw > UINT16_MAX) {
            ok = false;
            break;
        }
        states[i] = (uint16_t)raw;
    }

    /* Symbol codes are deliberately not carried: the engine records
    ** them for debuggability only and never reads them in dispatch,
    ** and recovering the grammar's `major` field would need a second
    ** width (YYCODETYPE) for no observable gain. */
    if (ok) {
        ok = (parse_engine_seed_stack(ctx, states, NULL, cs->stack_depth) == 0);
    }

    free(states);
    return ok;
}

/*
** Feed a single token to a fork.
**
** Two modes, chosen per fork by fork_steppable_snapshot():
**
** 1. Real mode -- the fork carries a snapshot with populated action
**    tables (i.e. it came from fork_parser() with a snapshot produced
**    by a real LALR construction).  The token is driven through the
**    runtime LALR(1) stepper in src/parse_engine.c against *this
**    fork's* snapshot, using a per-fork ParseContext (fork->step_ctx)
**    created lazily on first use.  That context owns the fork's own
**    LALR state stack, so each fork advances independently through its
**    own grammar.  When the fork's cloned state retains the generated
**    stack-entry layout (i.e. it came from fork_parser_seedable()),
**    that stack is seeded from the cloned yyParser so stepping resumes
**    at the conflict point -- which is the question fork-resolve is
**    asking: "which interpretation continues correctly from HERE".
**    Without the layout the context starts at state 0, the older
**    behaviour, which instead asks whether the lookahead is a valid
**    standalone parse.  A token the fork's grammar does not accept
**    makes parse_token() return -1 and this function return false, so
**    the caller marks the fork failed -- which is what makes
**    fork-resolve an empirical test of "which grammar actually parses
**    this input" rather than a priority comparison.  Reaching accept
**    marks the fork FORK_COMPLETED (see is_statement_end()).
**
**    Token codes are the externally-visible ones; parse_token()
**    applies the snapshot's %first_token offset itself.
**
** 2. Static mode -- the fork has no snapshot, or a metadata-only
**    snapshot with NULL action tables.  This is the
**    grammar-analysis-time path: fork_resolve_resolve() builds
**    lightweight forks from ConflictPoint metadata alone, with no live
**    parser and no tables to step against.  There is nothing to test
**    the token against, so it is counted and accepted.  Being
**    permissive here is deliberate: every candidate survives and the
**    tiebreaker decides on priority metadata, which is the best a
**    table-less fork can do.  Failing instead would reject every
**    candidate and make the strategy unable to resolve anything.
**
** Returns true if the fork is still alive after consuming the token,
** false if it failed.
*/
static bool feed_token_to_fork(ParseFork *fork, int token_code) {
    if (fork == NULL) return false;
    if (fork->status == FORK_FAILED || fork->status == FORK_ABANDONED) {
        return false;
    }

    ParserSnapshot *snap = fork_steppable_snapshot(fork);

    if (snap == NULL) {
        /* Static mode: no tables, so the token is accepted. */
        fork->status = FORK_RUNNING;
        fork->tokens_consumed++;
        (void)token_code;
        return true;
    }

    /* Real mode.  Seed this fork's stepping context from the cloned
    ** parser's stack so it resumes at the conflict point; forks whose
    ** cloned state carries no retained entry layout (created via the
    ** non-seedable fork_parser()) fall back to starting at state 0.
    **
    ** Residual limitations of the seeded resume:
    **   - Only state numbers cross over.  Semantic values (YYMINORTYPE
    **     `minor`) and, when %locations is active, per-entry
    **     YYLOCATIONTYPE are left in the clone: the runtime engine is
    **     value-free by design (see the header comment in
    **     src/parse_engine.c), so a fork answers "does this parse"
    **     and never rebuilds the semantic result.
    **   - Per-entry symbol codes are dropped (the engine keeps them
    **     only for debuggability and never dispatches on them).
    **   - State numbers are only meaningful against a snapshot that
    **     shares the forked parser's state numbering.  A fork whose
    **     grammar was rebuilt with added rules can renumber states, in
    **     which case a seeded state is a valid-but-wrong index; the
    **     range check in parse_engine_seed_stack() catches gross
    **     mismatches only.  Today's callers fork same-numbering
    **     snapshots; a fork against a renumbered grammar needs a state
    **     mapping carried alongside the modification. */
    if (fork->step_ctx == NULL) {
        fork->step_ctx = parse_begin(snap);
        if (fork->step_ctx == NULL) return false;
        (void)seed_step_ctx_from_clone(fork, (ParseContext *)fork->step_ctx);
    }

    fork->status = FORK_RUNNING;
    fork->tokens_consumed++;

    /* Real-mode stepping against this fork's own snapshot.  The
    ** vtable entry point fork_resolve_resolve() still builds
    ** table-less forks from ConflictPoint metadata (static mode,
    ** above); callers that hold real snapshots and want full-
    ** statement disambiguation can also use lime_mg_resolve() in
    ** src/multi_grammar.c, built on lime_simulate_parse(). */
    int rc = parse_token((ParseContext *)fork->step_ctx, token_code, NULL, LIME_LOC_UNKNOWN);

    if (rc < 0) {
        /* Genuine syntax error for this fork's grammar. */
        fork->error_count++;
        return false;
    }

    if (rc == 1) {
        /* Accept: record it so is_statement_end() can report it
        ** without mutating the fork. */
        fork->status = FORK_COMPLETED;
    }

    return true;
}

/* ================================================================== */
/*  Statement end detection                                            */
/* ================================================================== */

/*
** Check whether a fork has reached a statement boundary / accept state.
**
** Real mode (fork has a steppable snapshot): the fork is complete once
** its LALR stepping has reached accept.  feed_token_to_fork() records
** that by setting FORK_COMPLETED when parse_token() returns 1, which is
** the same condition parse_engine_step() uses to set its `accepted`
** flag.  This predicate is const, so the state is read rather than
** recomputed.
**
** Static mode (no steppable snapshot): there is no parser state to
** inspect, so the historical heuristic stands -- any running fork that
** consumed at least one token without error counts as complete, which
** lets the tiebreaker rank candidates on priority metadata.
**
** Returns true if the fork should be marked FORK_COMPLETED.
*/
static bool is_statement_end(const ParseFork *fork) {
    if (fork == NULL) return false;
    if (fork->status == FORK_FAILED || fork->status == FORK_ABANDONED) {
        return false;
    }

    if (fork_steppable_snapshot(fork) != NULL) {
        return fork->status == FORK_COMPLETED;
    }

    return (fork->status == FORK_RUNNING && fork->tokens_consumed > 0);
}

/* ================================================================== */
/*  Tiebreaker                                                         */
/* ================================================================== */

/*
** Collect pointers to all forks with FORK_COMPLETED status.
**
** Parameters:
**   set   - The fork set to scan.
**   out   - Output array (caller-allocated, at least set->count slots).
**   nout  - Output: number of completed forks found.
*/
static void collect_completed_forks(const ParseForkSet *set, ParseFork **out, uint32_t *nout) {
    *nout = 0;
    if (set == NULL) return;

    for (uint32_t i = 0; i < set->count; i++) {
        if (set->forks[i]->status == FORK_COMPLETED) {
            out[(*nout)++] = set->forks[i];
        }
    }
}

/*
** Apply the tiebreaker rule to select one winner from multiple
** completed forks.
**
** Parameters:
**   forks  - Array of completed ParseFork pointers.
**   nforks - Number of entries.
**   rule   - The tiebreak rule to apply.
**
** Returns the index of the winning fork, or -1 if nforks == 0.
*/
static int apply_tiebreaker(ParseFork **forks, uint32_t nforks, TiebreakRule rule) {
    if (nforks == 0) return -1;
    if (nforks == 1) return 0;

    int best = 0;

    switch (rule) {
    case TIEBREAK_PRIORITY:
        /* Lowest priority value wins */
        for (uint32_t i = 1; i < nforks; i++) {
            if (forks[i]->priority < forks[best]->priority) {
                best = (int)i;
            } else if (forks[i]->priority == forks[best]->priority) {
                /* Sub-tiebreak: fewer errors, then fewer tokens consumed */
                if (forks[i]->error_count < forks[best]->error_count) {
                    best = (int)i;
                } else if (forks[i]->error_count == forks[best]->error_count &&
                           forks[i]->tokens_consumed < forks[best]->tokens_consumed) {
                    best = (int)i;
                }
            }
        }
        break;

    case TIEBREAK_LONGEST_MATCH:
        /* Most tokens consumed wins */
        for (uint32_t i = 1; i < nforks; i++) {
            if (forks[i]->tokens_consumed > forks[best]->tokens_consumed) {
                best = (int)i;
            } else if (forks[i]->tokens_consumed == forks[best]->tokens_consumed) {
                /* Sub-tiebreak: lower priority value (higher priority) */
                if (forks[i]->priority < forks[best]->priority) {
                    best = (int)i;
                }
            }
        }
        break;

    case TIEBREAK_FIRST_COMPLETE:
        /* Lowest fork_id among completed forks wins (earliest created) */
        for (uint32_t i = 1; i < nforks; i++) {
            if (forks[i]->fork_id < forks[best]->fork_id) {
                best = (int)i;
            }
        }
        break;
    }

    return best;
}

/* ================================================================== */
/*  VTable: init                                                       */
/* ================================================================== */

static void *fork_resolve_init(const Extension *const *extensions, uint32_t nextensions) {
    ForkResolveContext *fc = calloc(1, sizeof(ForkResolveContext));
    if (fc == NULL) return NULL;

    fc->tiebreak = TIEBREAK_PRIORITY;
    fc->max_forks = FORK_RESOLVE_MAX_FORKS;
    fc->lookahead_depth = FORK_RESOLVE_DEFAULT_LOOKAHEAD;

    if (nextensions == 0) {
        /* Valid empty context */
        return fc;
    }

    fc->entries = calloc(nextensions, sizeof(ForkExtEntry));
    if (fc->entries == NULL) {
        free(fc);
        return NULL;
    }
    fc->capacity = nextensions;

    for (uint32_t i = 0; i < nextensions; i++) {
        const Extension *ext = extensions[i];
        if (ext == NULL) continue;

        ForkExtEntry *entry = &fc->entries[fc->nentries];
        entry->ext_id = ext->id;

        /* Extract priority: use the extension ID as a proxy for
        ** registration order (lower ID = higher priority).
        ** A more sophisticated version could read priority from
        ** extension metadata. */
        entry->priority = (int32_t)ext->id;
        entry->grammar_name = ext->name;

        fc->nentries++;
    }

    return fc;
}

/* ================================================================== */
/*  VTable: resolve                                                    */
/* ================================================================== */

/*
** Attempt to resolve a conflict by forking for each interpretation.
**
** For each LimeContext in the conflict point, we:
**   1. Create a ParseFork (lightweight or full clone depending on
**      whether a live parser is available).
**   2. Feed up to lookahead_depth tokens (simulated in static mode).
**   3. Prune failed forks.
**   4. Collect completed forks.
**   5. Apply the tiebreaker rule to select the winner.
**
** Supports two modes:
**   - Static mode (parse_ctx == NULL): Uses ConflictPoint metadata
**     to simulate forking.  Creates lightweight forks with priority
**     and token-count metadata for the tiebreaker.
**   - Runtime mode (parse_ctx != NULL): Creates real parser forks
**     via fork_parser() and feeds tokens through the runtime LALR
**     stepper against each fork's own snapshot (see
**     feed_token_to_fork()), so forks whose grammar rejects the input
**     genuinely fail.
*/
static bool fork_resolve_resolve(void *strategy_context, const ConflictPoint *conflict,
                                 struct ParseContext *parse_ctx, int lookahead,
                                 StrategyResult *result) {
    (void)parse_ctx;

    ForkResolveContext *fc = (ForkResolveContext *)strategy_context;
    if (fc == NULL || conflict == NULL || result == NULL) return false;

    strategy_result_init(result);
    fc->total_resolutions++;

    /* No contexts to fork on */
    if (conflict->ncontexts <= 0 || conflict->contexts == NULL) {
        return false;
    }

    /* Single context -- no ambiguity */
    if (conflict->ncontexts == 1) {
        result->winning_contexts = malloc(sizeof(LimeContext));
        if (result->winning_contexts == NULL) return false;

        result->winning_contexts[0] = conflict->contexts[0];
        result->nwinners = 1;
        result->confidence = 1.0f;
        result->explanation = dup_str("fork-resolve: single context, no fork needed");
        return true;
    }

    /*
    ** Create a fork set to evaluate each context.
    */
    uint32_t nforks = (uint32_t)conflict->ncontexts;
    if (nforks > fc->max_forks) {
        nforks = fc->max_forks;
    }

    ParseForkSet *fset = parse_fork_set_create(fc->max_forks);
    if (fset == NULL) {
        return false;
    }

    /*
    ** Create a fork for each candidate context.
    **
    ** Without a live parser to clone, we create lightweight forks
    ** using calloc (no parser state cloning).  Each fork carries the
    ** priority and extension metadata from the LimeContext so the
    ** tiebreaker can operate.
    */
    for (uint32_t i = 0; i < nforks; i++) {
        const LimeContext *ctx = &conflict->contexts[i];

        /* Determine fork priority: prefer LimeContext.priority if
        ** non-zero, otherwise fall back to the cached extension priority.
        **
        ** ParseFork.priority uses lower-is-better convention.  We scale
        ** the main priority by 10000 and add ext_id as a minor offset
        ** so that lower ext_id (registered first) wins ties. */
        int32_t prio;
        if (ctx->priority != 0) {
            prio = (int32_t)ctx->priority * 10000 + (int32_t)ctx->ext_id;
        } else {
            prio = lookup_priority(fc, ctx->ext_id);
        }

        ParseFork *fork = calloc(1, sizeof(ParseFork));
        if (fork == NULL) continue;

        fork->fork_id = parser_fork_next_id();
        fork->priority = (int)prio;
        fork->status = FORK_PENDING;
        fork->tokens_consumed = 0;
        fork->error_count = 0;
        fork->semantic_result = NULL;
        fork->free_result = NULL;
        fork->snapshot = NULL;

        fc->total_forks_created++;

        /* Feed the lookahead token if available */
        if (lookahead >= 0) {
            if (!feed_token_to_fork(fork, lookahead)) {
                parse_fork_fail(fork);
                fc->total_forks_failed++;
            }
        } else {
            /* No lookahead: assume all forks survive one step */
            fork->status = FORK_RUNNING;
            fork->tokens_consumed = 1;
        }

        /* Check for statement end */
        if (is_statement_end(fork)) {
            parse_fork_complete(fork, NULL, NULL);
            fc->total_forks_completed++;
        }

        if (!parse_fork_set_add(fset, fork)) {
            /* Set at capacity -- free this fork */
            free(fork);
        }
    }

    /* Prune failed forks */
    uint32_t pruned = parse_fork_set_prune(fset);
    fc->total_forks_pruned += pruned;

    /* Collect completed forks */
    ParseFork **completed = NULL;
    uint32_t ncompleted = 0;

    if (fset->count > 0) {
        completed = calloc(fset->count, sizeof(ParseFork *));
        if (completed != NULL) {
            collect_completed_forks(fset, completed, &ncompleted);
        }
    }

    if (ncompleted == 0) {
        /* All forks failed -- conflict is unresolvable */
        free(completed);
        parse_fork_set_destroy(fset);

        result->confidence = 0.0f;
        result->explanation = dup_str("fork-resolve: all forks failed, conflict unresolvable");
        return false;
    }

    /* Apply tiebreaker to select the winner */
    int winner_idx = apply_tiebreaker(completed, ncompleted, fc->tiebreak);
    if (winner_idx < 0) {
        free(completed);
        parse_fork_set_destroy(fset);
        return false;
    }

    ParseFork *winner = completed[winner_idx];

    /*
    ** Map the winning fork back to a LimeContext.  Match by the
    ** scaled priority (which encodes both the context priority and
    ** ext_id).  Since priorities are unique per fork, this is a
    ** reliable lookup.
    */
    int winning_context_idx = -1;
    for (int i = 0; i < conflict->ncontexts; i++) {
        const LimeContext *ctx = &conflict->contexts[i];
        int32_t ctx_prio;
        if (ctx->priority != 0) {
            ctx_prio = (int32_t)ctx->priority * 10000 + (int32_t)ctx->ext_id;
        } else {
            ctx_prio = lookup_priority(fc, ctx->ext_id);
        }

        if ((int)ctx_prio == winner->priority) {
            winning_context_idx = i;
            break;
        }
    }

    /* Fallback: if priority matching fails, use the fork's position */
    if (winning_context_idx < 0 && winner_idx < conflict->ncontexts) {
        winning_context_idx = winner_idx;
    }

    if (winning_context_idx < 0) {
        free(completed);
        parse_fork_set_destroy(fset);
        return false;
    }

    /* Build result */
    result->winning_contexts = malloc(sizeof(LimeContext));
    if (result->winning_contexts == NULL) {
        free(completed);
        parse_fork_set_destroy(fset);
        return false;
    }

    result->winning_contexts[0] = conflict->contexts[winning_context_idx];
    result->nwinners = 1;

    /* Confidence scales with specificity of the winner:
    **   1 of N completed -> high confidence (only one survived)
    **   all N completed  -> lower confidence (tiebreaker needed) */
    if (ncompleted == 1) {
        result->confidence = 1.0f;
    } else {
        result->confidence = 0.5f + 0.5f / (float)ncompleted;
    }

    /* Build explanation */
    const LimeContext *wctx = &conflict->contexts[winning_context_idx];
    const char *winner_name = lookup_grammar_name(fc, wctx->ext_id);
    if (winner_name == NULL) {
        winner_name = wctx->grammar_name;
    }

    char buf[512];
    snprintf(buf, sizeof(buf),
             "fork-resolve: %u/%u forks completed, winner is ext %u "
             "(%s, prio %d) via %s tiebreak",
             ncompleted, (uint32_t)conflict->ncontexts, (unsigned)wctx->ext_id,
             winner_name ? winner_name : "unknown", wctx->priority, tiebreak_name(fc->tiebreak));
    result->explanation = dup_str(buf);

    free(completed);
    parse_fork_set_destroy(fset);

    return true;
}

/* ================================================================== */
/*  VTable: update                                                     */
/* ================================================================== */

/*
** Feedback callback.  The fork-resolve strategy could use this to
** learn which grammars succeed more often and adjust priorities
** dynamically, but for now it's a no-op.
*/
static void fork_resolve_update(void *strategy_context, struct ExtensionRegistry *registry,
                                bool success) {
    (void)strategy_context;
    (void)registry;
    (void)success;
    /* Future: track success rates per extension and bias priority */
}

/* ================================================================== */
/*  VTable: destroy                                                    */
/* ================================================================== */

static void fork_resolve_destroy(void *strategy_context) {
    ForkResolveContext *fc = (ForkResolveContext *)strategy_context;
    if (fc == NULL) return;

    free(fc->entries);
    free(fc);
}

/* ================================================================== */
/*  Exported vtable                                                    */
/* ================================================================== */

const DisambiguationStrategyVTable strategy_fork_resolve_vtable = {
    .init = fork_resolve_init,
    .resolve = fork_resolve_resolve,
    .update = fork_resolve_update,
    .destroy = fork_resolve_destroy,
};

/* ================================================================== */
/*  Live fork resolution (for use when parser state is available)      */
/* ================================================================== */

/*
** Create forks for all contexts in a conflict point, using a live
** parser state.
**
** This is the entry point for the "real" fork-resolve: it clones the
** parser state for each grammar and creates a fork set ready for
** token feeding.
**
** Parameters:
**   conflict         - The conflict to resolve
**   parser           - Live parser state (yyParser*, as void*)
**   parser_size      - sizeof(yyParser)
**   stack_entry_size - sizeof(yyStackEntry)
**   inline_stack_offset - offsetof(yyParser, yystk0)
**   inline_stack_count  - YYSTACKDEPTH
**   stack_field_offset  - offsetof(yyParser, yystack)
**   tos_field_offset    - offsetof(yyParser, yytos)
**   stack_end_offset    - offsetof(yyParser, yystackEnd)
**   snapshots        - Array of snapshots, one per context (parallel to
**                      conflict->contexts[])
**   max_forks        - Maximum forks (0 = unlimited)
**
** Returns a fork set with one fork per context, or NULL on failure.
** Caller must destroy the set with parse_fork_set_destroy().
**
** The _seedable() variant additionally takes
** offsetof(yyStackEntry, stateno) and sizeof(...->stateno), which lets
** each fork's stepping context resume from the forked parser's stack
** rather than restarting at LALR state 0.  See
** seed_step_ctx_from_clone() for why those two values must be supplied
** rather than derived.
*/
ParseForkSet *fork_resolve_create_forks_seedable(
    const ConflictPoint *conflict, const void *parser, size_t parser_size, size_t stack_entry_size,
    size_t inline_stack_offset, uint32_t inline_stack_count, size_t stack_field_offset,
    size_t tos_field_offset, size_t stack_end_offset, size_t stateno_offset, size_t stateno_size,
    struct ParserSnapshot **snapshots, uint32_t max_forks) {
    if (conflict == NULL || parser == NULL || snapshots == NULL) {
        return NULL;
    }
    if (conflict->ncontexts <= 0 || conflict->contexts == NULL) {
        return NULL;
    }

    uint32_t nforks = (uint32_t)conflict->ncontexts;
    if (max_forks > 0 && nforks > max_forks) {
        nforks = max_forks;
    }

    ParseForkSet *fset = parse_fork_set_create(nforks);
    if (fset == NULL) return NULL;

    for (uint32_t i = 0; i < nforks; i++) {
        const LimeContext *ctx = &conflict->contexts[i];
        int priority = ctx->priority;

        ParseFork *fork = fork_parser_seedable(
            parser, parser_size, stack_entry_size, inline_stack_offset, inline_stack_count,
            stack_field_offset, tos_field_offset, stack_end_offset, stateno_offset, stateno_size,
            snapshots[i], priority);

        if (fork == NULL) {
            /* Skip contexts we can't fork for (allocation failure) */
            continue;
        }

        if (!parse_fork_set_add(fset, fork)) {
            free_parse_fork(fork);
        }
    }

    /* If no forks were created, clean up */
    if (fset->count == 0) {
        parse_fork_set_destroy(fset);
        return NULL;
    }

    return fset;
}

/*
** Layout-free variant: forks created here carry no stack-entry layout,
** so their stepping contexts start at LALR state 0 (pre-existing
** behaviour).  Callers that can supply offsetof/sizeof for
** yyStackEntry::stateno should use fork_resolve_create_forks_seedable()
** instead, which makes each fork resume from the captured stack.
*/
ParseForkSet *fork_resolve_create_forks(const ConflictPoint *conflict, const void *parser,
                                        size_t parser_size, size_t stack_entry_size,
                                        size_t inline_stack_offset, uint32_t inline_stack_count,
                                        size_t stack_field_offset, size_t tos_field_offset,
                                        size_t stack_end_offset, struct ParserSnapshot **snapshots,
                                        uint32_t max_forks) {
    return fork_resolve_create_forks_seedable(
        conflict, parser, parser_size, stack_entry_size, inline_stack_offset, inline_stack_count,
        stack_field_offset, tos_field_offset, stack_end_offset, 0, 0, snapshots, max_forks);
}

/*
** Feed a token stream to all active forks in a fork set, pruning
** failures along the way.
**
** Parameters:
**   fset    - The fork set with active forks.
**   tokens  - Array of token codes to feed.
**   ntokens - Number of tokens in the array.
**
** Returns the number of forks that are still active (RUNNING or
** COMPLETED) after all tokens have been consumed.
*/
uint32_t fork_resolve_feed_tokens(ParseForkSet *fset, const int *tokens, uint32_t ntokens) {
    if (fset == NULL || tokens == NULL || ntokens == 0) return 0;

    for (uint32_t t = 0; t < ntokens; t++) {
        uint32_t active = parse_fork_set_active_count(fset);
        if (active == 0) break;

        for (uint32_t i = 0; i < fset->count; i++) {
            ParseFork *f = fset->forks[i];
            if (f->status != FORK_RUNNING && f->status != FORK_PENDING) {
                continue;
            }

            if (!feed_token_to_fork(f, tokens[t])) {
                parse_fork_fail(f);
                continue;
            }

            /* Check for statement end after each token */
            if (is_statement_end(f)) {
                parse_fork_complete(f, NULL, NULL);
            }
        }

        /* Prune failed forks to free memory eagerly */
        parse_fork_set_prune(fset);

        /* Early exit if only one fork remains and it completed */
        if (fset->count == 1 && fset->forks[0]->status == FORK_COMPLETED) {
            break;
        }
    }

    /* Mark surviving running/pending forks as completed */
    for (uint32_t i = 0; i < fset->count; i++) {
        ParseFork *f = fset->forks[i];
        if (f->status == FORK_RUNNING || f->status == FORK_PENDING) {
            parse_fork_complete(f, NULL, NULL);
        }
    }

    return fset->count;
}

/*
** Evaluate a fork set after tokens have been fed to each fork.
**
** Selects the best completed fork using the specified tiebreaker rule.
**
** Returns the best fork's index in the fork set, or -1 if no fork
** completed successfully.
**
** The fork set is not destroyed by this function.
*/
int fork_resolve_evaluate(const ParseForkSet *fset, TiebreakRule tiebreak) {
    if (fset == NULL || fset->count == 0) return -1;

    /* Collect completed forks */
    ParseFork **completed = calloc(fset->count, sizeof(ParseFork *));
    if (completed == NULL) return -1;

    uint32_t ncompleted = 0;
    collect_completed_forks(fset, completed, &ncompleted);

    if (ncompleted == 0) {
        free(completed);
        return -1;
    }

    /* Apply tiebreaker */
    int winner_in_completed = apply_tiebreaker(completed, ncompleted, tiebreak);
    if (winner_in_completed < 0) {
        free(completed);
        return -1;
    }

    ParseFork *winner = completed[winner_in_completed];

    /* Map back to index in the fork set */
    int result = -1;
    for (uint32_t i = 0; i < fset->count; i++) {
        if (fset->forks[i] == winner) {
            result = (int)i;
            break;
        }
    }

    free(completed);
    return result;
}

/*
** Run the complete fork-resolve pipeline on a live token stream.
**
** This combines fork creation, token feeding, and evaluation into a
** single call.  It produces a StrategyResult compatible with the
** disambiguation framework.
**
** Parameters:
**   conflict         - The conflict point with candidate contexts.
**   parser           - Live parser state (opaque yyParser*).
**   parser_size      - sizeof(yyParser).
**   stack_entry_size - sizeof(yyStackEntry).
**   inline_stack_offset - offsetof(yyParser, yystk0).
**   inline_stack_count  - YYSTACKDEPTH.
**   stack_field_offset  - offsetof(yyParser, yystack).
**   tos_field_offset    - offsetof(yyParser, yytos).
**   stack_end_offset    - offsetof(yyParser, yystackEnd).
**   snapshots        - Array of ParserSnapshot pointers, one per context.
**   tokens           - Array of token codes to feed.
**   ntokens          - Number of tokens in the stream.
**   tiebreak         - Which tiebreaker rule to use.
**   max_forks        - Maximum number of forks (0 = unlimited).
**   result           - Output: the resolution result.
**
** Returns true if a winner was found, false if all forks failed.
** The caller must call strategy_result_cleanup() on the result.
*/
bool fork_resolve_runtime(const ConflictPoint *conflict, const void *parser, size_t parser_size,
                          size_t stack_entry_size, size_t inline_stack_offset,
                          uint32_t inline_stack_count, size_t stack_field_offset,
                          size_t tos_field_offset, size_t stack_end_offset,
                          struct ParserSnapshot **snapshots, const int *tokens, uint32_t ntokens,
                          TiebreakRule tiebreak, uint32_t max_forks, StrategyResult *result) {
    if (conflict == NULL || parser == NULL || result == NULL || snapshots == NULL) {
        return false;
    }

    strategy_result_init(result);

    /* Create forks */
    ParseForkSet *fset = fork_resolve_create_forks(
        conflict, parser, parser_size, stack_entry_size, inline_stack_offset, inline_stack_count,
        stack_field_offset, tos_field_offset, stack_end_offset, snapshots, max_forks);

    if (fset == NULL) {
        result->explanation = dup_str("fork-resolve (runtime): failed to create any forks");
        return false;
    }

    /* Feed tokens */
    if (tokens != NULL && ntokens > 0) {
        fork_resolve_feed_tokens(fset, tokens, ntokens);
    } else {
        /* No tokens: mark all forks as completed immediately */
        for (uint32_t i = 0; i < fset->count; i++) {
            parse_fork_complete(fset->forks[i], NULL, NULL);
        }
    }

    /* Evaluate */
    int winner_idx = fork_resolve_evaluate(fset, tiebreak);
    if (winner_idx < 0) {
        parse_fork_set_destroy(fset);
        result->explanation = dup_str("fork-resolve (runtime): all forks failed");
        return false;
    }

    /* Map fork index back to context index.  Forks were created in
    ** the same order as conflict->contexts[], but some may have been
    ** pruned.  Use fork priority to find the matching context. */
    ParseFork *winner = fset->forks[winner_idx];
    int ctx_idx = -1;
    for (int i = 0; i < conflict->ncontexts; i++) {
        if (conflict->contexts[i].priority == winner->priority) {
            ctx_idx = i;
            break;
        }
    }

    if (ctx_idx < 0 && winner_idx < conflict->ncontexts) {
        ctx_idx = winner_idx; /* Fallback */
    }

    if (ctx_idx < 0) {
        parse_fork_set_destroy(fset);
        return false;
    }

    /* Build result */
    result->winning_contexts = malloc(sizeof(LimeContext));
    if (result->winning_contexts == NULL) {
        parse_fork_set_destroy(fset);
        return false;
    }

    result->winning_contexts[0] = conflict->contexts[ctx_idx];
    result->nwinners = 1;

    /* Compute confidence */
    ParseFork **completed = calloc(fset->count, sizeof(ParseFork *));
    uint32_t ncompleted = 0;
    if (completed != NULL) {
        collect_completed_forks(fset, completed, &ncompleted);
        free(completed);
    }

    result->confidence = (ncompleted <= 1) ? 1.0f : 0.5f + 0.5f / (float)ncompleted;

    /* Build explanation */
    const LimeContext *wctx = &conflict->contexts[ctx_idx];
    char buf[512];
    snprintf(buf, sizeof(buf),
             "fork-resolve (runtime): %u forks survived %u tokens, "
             "winner ext %u (grammar '%s', consumed %u tokens) via %s",
             ncompleted, ntokens, (unsigned)wctx->ext_id,
             wctx->grammar_name ? wctx->grammar_name : "unknown", (unsigned)winner->tokens_consumed,
             tiebreak_name(tiebreak));
    result->explanation = dup_str(buf);

    parse_fork_set_destroy(fset);
    return true;
}
