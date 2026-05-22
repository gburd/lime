/*
** Execution Policy Engine implementation.
**
** Controls which grammars' semantic actions execute after disambiguation.
** Supports four policies: first-only, all, chained, and conditional.
**
** The engine is decoupled from parser internals through the ParserExecuteFn
** callback -- callers provide the function that actually runs semantic
** actions on a parser instance.
*/
#include "../include/execution_policy.h"
#include "../include/disambiguation.h" /* StrategyResult, LimeContext */

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

static char *dup_str(const char *s) {
    if (s == NULL) return NULL;
    size_t len = strlen(s) + 1;
    char *copy = malloc(len);
    if (copy != NULL) memcpy(copy, s, len);
    return copy;
}

static ExecutionResult make_error_result(uint32_t ext_id, const char *msg) {
    ExecutionResult r;
    r.extension_id = ext_id;
    r.result = NULL;
    r.error = dup_str(msg);
    return r;
}

static ExecutionResult make_success_result(uint32_t ext_id, void *output) {
    ExecutionResult r;
    r.extension_id = ext_id;
    r.result = output;
    r.error = NULL;
    return r;
}

/* ------------------------------------------------------------------ */
/*  execute_parser: run a single parser through the callback           */
/* ------------------------------------------------------------------ */

static ExecutionResult execute_parser(ParserExecuteFn execute_fn, LimeParserHandle *parser,
                                      GrammarExtensionMetadata *ext, void *chain_input) {
    if (execute_fn == NULL) {
        return make_error_result(ext->extension_id, "no execute callback provided");
    }
    if (parser == NULL) {
        return make_error_result(ext->extension_id, "NULL parser handle");
    }

    void *output = NULL;
    char *error = NULL;
    bool ok = execute_fn(parser, ext, chain_input, &output, &error);

    if (ok) {
        return make_success_result(ext->extension_id, output);
    }

    ExecutionResult r;
    r.extension_id = ext->extension_id;
    r.result = NULL;
    r.error = error; /* Ownership transferred from execute_fn */
    return r;
}

/* ------------------------------------------------------------------ */
/*  Policy: EXEC_FIRST_ONLY                                            */
/* ------------------------------------------------------------------ */

/*
** Execute only the first (highest-priority) winner.
** The disambiguation layer sorts winners by priority, so index 0
** is the highest-priority winner.
*/
static ExecutionResult *exec_first_only(ParserExecuteFn execute_fn,
                                        const StrategyResult *disambiguation,
                                        LimeParserHandle **parsers,
                                        GrammarExtensionMetadata **extensions, int *nresults_out) {
    *nresults_out = 0;

    if (disambiguation->nwinners == 0) {
        return NULL;
    }

    ExecutionResult *results = malloc(sizeof(ExecutionResult));
    if (results == NULL) return NULL;

    results[0] = execute_parser(execute_fn, parsers[0], extensions[0], NULL);
    *nresults_out = 1;
    return results;
}

/* ------------------------------------------------------------------ */
/*  Policy: EXEC_ALL                                                   */
/* ------------------------------------------------------------------ */

/*
** Execute all winners independently.  Each parser runs with NULL input
** (no chaining).  If stop_on_error is set, execution halts at the
** first failure.
*/
static ExecutionResult *exec_all(ParserExecuteFn execute_fn, const StrategyResult *disambiguation,
                                 LimeParserHandle **parsers, GrammarExtensionMetadata **extensions,
                                 bool stop_on_error, int max_executions, int *nresults_out) {
    int nwinners = disambiguation->nwinners;
    *nresults_out = 0;

    if (nwinners == 0) {
        return NULL;
    }

    /* Apply execution limit */
    int limit = nwinners;
    if (max_executions > 0 && max_executions < limit) {
        limit = max_executions;
    }

    ExecutionResult *results = calloc((size_t)limit, sizeof(ExecutionResult));
    if (results == NULL) return NULL;

    int count = 0;
    for (int i = 0; i < limit; i++) {
        results[count] = execute_parser(execute_fn, parsers[i], extensions[i], NULL);
        count++;

        if (stop_on_error && results[count - 1].error != NULL) {
            break;
        }
    }

    *nresults_out = count;
    return results;
}

/* ------------------------------------------------------------------ */
/*  Policy: EXEC_CHAIN                                                 */
/* ------------------------------------------------------------------ */

/*
** Execute winners in sequence, feeding each parser's output as input
** to the next.  The first parser receives NULL input.
**
** On error, if stop_on_error is set, the chain halts and the results
** collected so far are returned.
*/
static ExecutionResult *exec_chain(ParserExecuteFn execute_fn, const StrategyResult *disambiguation,
                                   LimeParserHandle **parsers,
                                   GrammarExtensionMetadata **extensions, bool stop_on_error,
                                   int max_executions, int *nresults_out) {
    int nwinners = disambiguation->nwinners;
    *nresults_out = 0;

    if (nwinners == 0) {
        return NULL;
    }

    /* Apply execution limit */
    int limit = nwinners;
    if (max_executions > 0 && max_executions < limit) {
        limit = max_executions;
    }

    ExecutionResult *results = calloc((size_t)limit, sizeof(ExecutionResult));
    if (results == NULL) return NULL;

    void *chain_input = NULL;
    int count = 0;

    for (int i = 0; i < limit; i++) {
        results[count] = execute_parser(execute_fn, parsers[i], extensions[i], chain_input);
        count++;

        if (results[count - 1].error != NULL) {
            /* Chain broken by error */
            if (stop_on_error) {
                break;
            }
            /* On error without stop, pass NULL to next parser */
            chain_input = NULL;
        } else {
            /* Feed this parser's output to the next one */
            chain_input = results[count - 1].result;
        }
    }

    *nresults_out = count;
    return results;
}

/* ------------------------------------------------------------------ */
/*  Policy: EXEC_CONDITIONAL                                           */
/* ------------------------------------------------------------------ */

/*
** Execute only those winners whose extension's should_execute callback
** returns true.  Extensions without a should_execute callback are
** treated as "always execute".
*/
static ExecutionResult *exec_conditional(ParserExecuteFn execute_fn,
                                         const StrategyResult *disambiguation,
                                         LimeParserHandle **parsers,
                                         GrammarExtensionMetadata **extensions, bool stop_on_error,
                                         int max_executions, int *nresults_out) {
    int nwinners = disambiguation->nwinners;
    *nresults_out = 0;

    if (nwinners == 0) {
        return NULL;
    }

    /* Worst case: all winners pass the condition */
    ExecutionResult *results = calloc((size_t)nwinners, sizeof(ExecutionResult));
    if (results == NULL) return NULL;

    int count = 0;

    for (int i = 0; i < nwinners; i++) {
        /* Apply execution limit */
        if (max_executions > 0 && count >= max_executions) {
            break;
        }

        /* Check conditional callback */
        GrammarExtensionMetadata *ext = extensions[i];
        if (ext->should_execute != NULL) {
            if (!ext->should_execute(ext, disambiguation)) {
                continue; /* Extension opted out */
            }
        }

        results[count] = execute_parser(execute_fn, parsers[i], ext, NULL);
        count++;

        if (stop_on_error && results[count - 1].error != NULL) {
            break;
        }
    }

    *nresults_out = count;

    /* Shrink allocation if we skipped some winners */
    if (count < nwinners && count > 0) {
        ExecutionResult *shrunk = realloc(results, (size_t)count * sizeof(ExecutionResult));
        if (shrunk != NULL) {
            results = shrunk;
        }
        /* If realloc fails, the oversized buffer is fine */
    } else if (count == 0) {
        free(results);
        results = NULL;
    }

    return results;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

void execution_policy_config_init(ExecutionPolicyConfig *config) {
    if (config == NULL) return;
    config->policy = EXEC_FIRST_ONLY;
    config->execute_fn = NULL;
    config->stop_on_error = true;
    config->max_executions = 0;
}

ExecutionResult *execute_semantic_actions(const ExecutionPolicyConfig *config,
                                          const StrategyResult *disambiguation,
                                          LimeParserHandle **parsers,
                                          GrammarExtensionMetadata **extensions,
                                          int *nresults_out) {
    if (nresults_out == NULL) return NULL;
    *nresults_out = 0;

    if (config == NULL || disambiguation == NULL || parsers == NULL || extensions == NULL) {
        return NULL;
    }

    if (disambiguation->nwinners <= 0) {
        return NULL;
    }

    if (config->execute_fn == NULL) {
        /* No execute callback -- return a single error result */
        ExecutionResult *r = malloc(sizeof(ExecutionResult));
        if (r == NULL) return NULL;
        r[0] = make_error_result(disambiguation->winning_contexts[0].ext_id,
                                 "no execute callback configured");
        *nresults_out = 1;
        return r;
    }

    switch (config->policy) {
    case EXEC_FIRST_ONLY:
        return exec_first_only(config->execute_fn, disambiguation, parsers, extensions,
                               nresults_out);

    case EXEC_ALL:
        return exec_all(config->execute_fn, disambiguation, parsers, extensions,
                        config->stop_on_error, config->max_executions, nresults_out);

    case EXEC_CHAIN:
        return exec_chain(config->execute_fn, disambiguation, parsers, extensions,
                          config->stop_on_error, config->max_executions, nresults_out);

    case EXEC_CONDITIONAL:
        return exec_conditional(config->execute_fn, disambiguation, parsers, extensions,
                                config->stop_on_error, config->max_executions, nresults_out);

    default:
        /* Unknown policy -- fall back to FIRST_ONLY */
        return exec_first_only(config->execute_fn, disambiguation, parsers, extensions,
                               nresults_out);
    }
}

void execution_results_free(ExecutionResult *results, int nresults) {
    if (results == NULL) return;
    for (int i = 0; i < nresults; i++) {
        free(results[i].error);
        /* Note: results[i].result is NOT freed -- caller owns it */
    }
    free(results);
}

const char *execution_policy_name(LimeExecMode policy) {
    switch (policy) {
    case EXEC_FIRST_ONLY:
        return "first_only";
    case EXEC_ALL:
        return "all";
    case EXEC_CHAIN:
        return "chain";
    case EXEC_CONDITIONAL:
        return "conditional";
    default:
        return "unknown";
    }
}

ExecutionResult *execute_first_only(ParserExecuteFn execute_fn,
                                    const StrategyResult *disambiguation,
                                    LimeParserHandle **parsers,
                                    GrammarExtensionMetadata **extensions, int *nresults_out) {
    ExecutionPolicyConfig config;
    execution_policy_config_init(&config);
    config.execute_fn = execute_fn;
    /* policy is already EXEC_FIRST_ONLY from init */

    return execute_semantic_actions(&config, disambiguation, parsers, extensions, nresults_out);
}
