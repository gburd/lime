/*
** Natural Language SQL Extension with LLM Disambiguation
**
** Demonstrates a custom disambiguation strategy that queries an LLM
** to resolve ambiguous grammar constructs.  This extension:
**
**   1. Registers grammar modifications for natural-language-like SQL
**      constructs (e.g. "show me", "from that table", "last week").
**
**   2. Provides a custom DisambiguationStrategyVTable that queries
**      an LLM API when the parser encounters an ambiguous construct.
**
**   3. Falls back to standard SQL parsing when the LLM is unavailable.
**
** Example queries this extension can handle:
**
**   "show me all users"            -> SELECT * FROM users
**   "count orders from last week"  -> SELECT COUNT(*) FROM orders
**                                      WHERE created_at > NOW() - INTERVAL '7 days'
**   "find products under 50"       -> SELECT * FROM products WHERE price < 50
**
** Build:
**   cc -std=c11 -I../../include -I../../src nlsql_extension.c llm_client.c \
**      -L../../builddir/src -llime_parser -lcurl -lpthread -o nlsql_demo
**
** Or without libcurl (stub mode):
**   cc -std=c11 -DLLM_NO_CURL -I../../include -I../../src \
**      nlsql_extension.c llm_client.c \
**      -L../../builddir/src -llime_parser -lpthread -o nlsql_demo
*/

#define _GNU_SOURCE
#include "llm_client.h"

/* Use the internal extension header (src/, not include/)
** because the public include/extension.h is a minimal placeholder.
** The disambiguation header is in include/. */
#include "../../src/extension.h"
#include "disambiguation.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Portable local_strdup for strict C11 without _GNU_SOURCE */
static char *local_strdup(const char *s) {
    if (s == NULL) return NULL;
    size_t len = strlen(s) + 1;
    char *copy = malloc(len);
    if (copy) memcpy(copy, s, len);
    return copy;
}

/* ================================================================== */
/*  Natural language SQL grammar modifications                         */
/* ================================================================== */

/*
** New tokens for natural-language constructs.
*/
static GrammarModification nlsql_mods[] = {
    /* Natural language keywords */
    {
        .type = MOD_ADD_TOKEN,
        .description = "Natural language SHOW keyword",
        .u.add_token = {
            .name       = "NL_SHOW",
            .lexeme     = "show",
            .token_code = -1,
        },
    },
    {
        .type = MOD_ADD_TOKEN,
        .description = "Natural language FIND keyword",
        .u.add_token = {
            .name       = "NL_FIND",
            .lexeme     = "find",
            .token_code = -1,
        },
    },
    {
        .type = MOD_ADD_TOKEN,
        .description = "Natural language COUNT keyword",
        .u.add_token = {
            .name       = "NL_COUNT",
            .lexeme     = "count",
            .token_code = -1,
        },
    },
    {
        .type = MOD_ADD_TOKEN,
        .description = "Natural language ME keyword",
        .u.add_token = {
            .name       = "NL_ME",
            .lexeme     = "me",
            .token_code = -1,
        },
    },
    {
        .type = MOD_ADD_TOKEN,
        .description = "Natural language ALL keyword",
        .u.add_token = {
            .name       = "NL_ALL",
            .lexeme     = "all",
            .token_code = -1,
        },
    },
    {
        .type = MOD_ADD_TOKEN,
        .description = "Natural language UNDER keyword (for < comparisons)",
        .u.add_token = {
            .name       = "NL_UNDER",
            .lexeme     = "under",
            .token_code = -1,
        },
    },
    {
        .type = MOD_ADD_TOKEN,
        .description = "Natural language OVER keyword (for > comparisons)",
        .u.add_token = {
            .name       = "NL_OVER",
            .lexeme     = "over",
            .token_code = -1,
        },
    },
};

#define NLSQL_MOD_COUNT (sizeof(nlsql_mods) / sizeof(nlsql_mods[0]))

/* ================================================================== */
/*  Extension callbacks                                                */
/* ================================================================== */

static bool nlsql_get_modifications(
    void *user_data,
    const struct ParserSnapshot *base_snapshot,
    GrammarModification **mods_out,
    uint32_t *nmods_out)
{
    (void)user_data;
    (void)base_snapshot;
    *mods_out = nlsql_mods;
    *nmods_out = NLSQL_MOD_COUNT;
    return true;
}

static ConflictResolution nlsql_on_conflict(
    void *user_data,
    const ConflictInfo *info)
{
    (void)user_data;
    (void)info;
    /* When natural language keywords conflict with standard SQL,
    ** keep the standard SQL interpretation. The LLM disambiguator
    ** will handle the natural language case at parse time. */
    return CONFLICT_KEEP_EXISTING;
}

static void nlsql_on_unload(void *user_data) {
    (void)user_data;
}

const ExtensionInfo nlsql_extension_info = {
    .name               = "nlsql",
    .version            = "0.1.0",
    .get_modifications  = nlsql_get_modifications,
    .on_conflict        = nlsql_on_conflict,
    .on_unload          = nlsql_on_unload,
    .user_data          = NULL,
};

/* ================================================================== */
/*  LLM Disambiguation Strategy                                        */
/* ================================================================== */

/*
** System prompt instructing the LLM how to resolve grammar conflicts.
*/
static const char *LLM_SYSTEM_PROMPT =
    "You are a SQL parser disambiguation oracle. When given a description "
    "of an ambiguous grammar construct, you must decide which interpretation "
    "is correct.\n\n"
    "You will receive:\n"
    "- A description of the conflict\n"
    "- The available interpretations (numbered)\n"
    "- Context about the input being parsed\n\n"
    "Respond with ONLY a single line in this exact format:\n"
    "CHOICE: <number>\n\n"
    "Where <number> is the 1-based index of the winning interpretation. "
    "Do not include any other text.";

/*
** Strategy context: holds the LLM client.
*/
typedef struct LlmStrategyContext {
    LlmClient *client;
    int queries_made;
    int queries_failed;
} LlmStrategyContext;

static void *llm_strategy_init(const Extension *const *extensions,
                                uint32_t nextensions)
{
    (void)extensions;
    (void)nextensions;

    LlmStrategyContext *ctx = calloc(1, sizeof(LlmStrategyContext));
    if (ctx == NULL) return NULL;

    /* Try to create an LLM client from environment */
    const char *api_key = getenv("LIME_LLM_API_KEY");
    const char *provider_str = getenv("LIME_LLM_PROVIDER");

    LlmProvider provider = LLM_PROVIDER_OPENAI;
    if (provider_str != NULL && strcmp(provider_str, "anthropic") == 0) {
        provider = LLM_PROVIDER_ANTHROPIC;
    }

    if (api_key != NULL && api_key[0] != '\0') {
        LlmClientConfig config = {
            .provider    = provider,
            .api_key     = api_key,
            .model       = getenv("LIME_LLM_MODEL"),
            .base_url    = getenv("LIME_LLM_BASE_URL"),
            .timeout_ms  = 10000,
            .temperature = 0.0f,
            .max_tokens  = 64,
        };
        ctx->client = llm_client_create(&config);
    }

    /* It is valid to have no client -- we just return "unresolved" */
    return ctx;
}

static bool llm_strategy_resolve(void *strategy_context,
                                  const ConflictPoint *conflict,
                                  struct ParseContext *parse_ctx,
                                  int lookahead,
                                  StrategyResult *result)
{
    (void)parse_ctx;
    (void)lookahead;

    LlmStrategyContext *ctx = (LlmStrategyContext *)strategy_context;
    if (ctx == NULL || conflict == NULL || result == NULL) return false;

    strategy_result_init(result);

    /* If no LLM client, we cannot resolve */
    if (ctx->client == NULL || !llm_client_available(ctx->client)) {
        return false;
    }

    if (conflict->ncontexts <= 1 || conflict->contexts == NULL) {
        return false; /* Nothing to disambiguate */
    }

    /* Build the user prompt describing the conflict */
    char prompt[2048];
    int pos = 0;
    pos += snprintf(prompt + pos, sizeof(prompt) - (size_t)pos,
                    "Grammar conflict at token %u, state %d.\n",
                    (unsigned)conflict->token, conflict->state);

    if (conflict->description != NULL) {
        pos += snprintf(prompt + pos, sizeof(prompt) - (size_t)pos,
                        "Description: %s\n\n", conflict->description);
    }

    pos += snprintf(prompt + pos, sizeof(prompt) - (size_t)pos,
                    "Available interpretations:\n");

    for (int i = 0; i < conflict->ncontexts && pos < (int)sizeof(prompt) - 128; i++) {
        const LimeContext *lc = &conflict->contexts[i];
        pos += snprintf(prompt + pos, sizeof(prompt) - (size_t)pos,
                        "%d. Extension %u (%s), token %u, priority %d\n",
                        i + 1,
                        (unsigned)lc->ext_id,
                        lc->grammar_name ? lc->grammar_name : "unknown",
                        (unsigned)lc->token,
                        lc->priority);
    }

    pos += snprintf(prompt + pos, sizeof(prompt) - (size_t)pos,
                    "\nWhich interpretation is correct?");

    /* Query the LLM */
    ctx->queries_made++;
    LlmResponse resp = llm_client_query(ctx->client, LLM_SYSTEM_PROMPT,
                                         prompt);

    if (!resp.success || resp.content == NULL) {
        ctx->queries_failed++;
        char err_buf[256];
        snprintf(err_buf, sizeof(err_buf), "LLM query failed: %s",
                 resp.error ? resp.error : "unknown error");
        result->explanation = local_strdup(err_buf);
        llm_response_free(&resp);
        return false;
    }

    /* Parse the response: look for "CHOICE: N" */
    int choice = 0;
    const char *choice_str = strstr(resp.content, "CHOICE:");
    if (choice_str == NULL) choice_str = strstr(resp.content, "choice:");
    if (choice_str != NULL) {
        choice = atoi(choice_str + 7); /* skip "CHOICE:" */
    }

    if (choice < 1 || choice > conflict->ncontexts) {
        /* LLM gave an invalid response -- try to parse just a number */
        choice = atoi(resp.content);
    }

    if (choice >= 1 && choice <= conflict->ncontexts) {
        result->winning_contexts = malloc(sizeof(LimeContext));
        if (result->winning_contexts == NULL) {
            llm_response_free(&resp);
            return false;
        }
        result->winning_contexts[0] = conflict->contexts[choice - 1];
        result->nwinners = 1;
        result->confidence = 0.8f; /* LLM is confident but not certain */

        char buf[512];
        snprintf(buf, sizeof(buf),
                 "LLM chose interpretation %d (ext %u, '%s')",
                 choice,
                 (unsigned)conflict->contexts[choice - 1].ext_id,
                 conflict->contexts[choice - 1].grammar_name
                     ? conflict->contexts[choice - 1].grammar_name
                     : "unknown");
        result->explanation = local_strdup(buf);
        llm_response_free(&resp);
        return true;
    }

    /* Could not parse LLM response */
    char buf[512];
    snprintf(buf, sizeof(buf),
             "LLM response unparseable: '%.200s'",
             resp.content);
    result->explanation = local_strdup(buf);
    llm_response_free(&resp);
    ctx->queries_failed++;
    return false;
}

static void llm_strategy_update(void *strategy_context,
                                 struct ExtensionRegistry *registry,
                                 bool success)
{
    (void)strategy_context;
    (void)registry;
    (void)success;
    /* Could track success rate and adjust temperature */
}

static void llm_strategy_destroy(void *strategy_context) {
    LlmStrategyContext *ctx = (LlmStrategyContext *)strategy_context;
    if (ctx == NULL) return;

    if (ctx->queries_made > 0) {
        fprintf(stderr, "LLM strategy stats: %d queries, %d failed\n",
                ctx->queries_made, ctx->queries_failed);
    }

    if (ctx->client) llm_client_destroy(ctx->client);
    free(ctx);
}

static const DisambiguationStrategyVTable llm_strategy_vtable = {
    .init    = llm_strategy_init,
    .resolve = llm_strategy_resolve,
    .update  = llm_strategy_update,
    .destroy = llm_strategy_destroy,
};

/* ================================================================== */
/*  Demo main                                                           */
/* ================================================================== */

/*
** Demonstrates:
**   1. Creating an extension registry
**   2. Registering the NL-SQL extension
**   3. Creating a disambiguation context with LLM strategy
**   4. Simulating a conflict resolution
*/
int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    printf("=== LLM Oracle Disambiguation Demo ===\n\n");

    /* Step 1: Create extension registry */
    ExtensionRegistry *reg = create_extension_registry();
    if (reg == NULL) {
        fprintf(stderr, "Failed to create extension registry\n");
        return 1;
    }

    /* Step 2: Register the NL-SQL extension */
    ExtensionID nlsql_id;
    if (!register_extension(reg, &nlsql_extension_info, &nlsql_id)) {
        fprintf(stderr, "Failed to register nlsql extension\n");
        destroy_extension_registry(reg);
        return 1;
    }
    printf("Registered extension: nlsql (id=%u)\n", nlsql_id);

    /* Step 3: Load the extension */
    char *error = NULL;
    if (!load_extension(reg, nlsql_id, NULL, &error)) {
        fprintf(stderr, "Failed to load extension: %s\n",
                error ? error : "unknown");
        free(error);
        destroy_extension_registry(reg);
        return 1;
    }
    printf("Loaded extension: nlsql\n\n");

    /* Step 4: Create disambiguation context with LLM strategy */
    DisambiguationContext *dis = disambiguation_create_custom(
        &llm_strategy_vtable, reg);

    if (dis == NULL) {
        fprintf(stderr, "Failed to create disambiguation context\n");
        destroy_extension_registry(reg);
        return 1;
    }
    printf("Created LLM disambiguation context\n");
    printf("  LLM available: %s\n",
           getenv("LIME_LLM_API_KEY") ? "yes (key set)" : "no (set LIME_LLM_API_KEY)");
    printf("\n");

    /* Step 5: Simulate a conflict */
    printf("--- Simulating conflict resolution ---\n\n");

    /* Build a sample ConflictPoint with two interpretations */
    LimeContext contexts[2] = {
        {
            .ext_id = 0,          /* Base grammar */
            .token  = 42,
            .state  = 100,
            .priority = 10,
            .grammar_name = "standard_sql",
        },
        {
            .ext_id = nlsql_id,
            .token  = 42,
            .state  = 100,
            .priority = 5,
            .grammar_name = "nlsql",
        },
    };

    ConflictPoint cp;
    conflict_point_init(&cp, 42, 100, CONFLICT_LEVEL_RULE);
    cp.contexts = contexts;
    cp.ncontexts = 2;
    cp.description = local_strdup("Token 'show': standard SQL SELECT vs NL-SQL show query");

    printf("Conflict: %s\n", cp.description);
    printf("  Interpretation 1: standard_sql (priority %d)\n",
           contexts[0].priority);
    printf("  Interpretation 2: nlsql (priority %d)\n",
           contexts[1].priority);
    printf("\n");

    /* Resolve */
    StrategyResult result = disambiguation_resolve(dis, &cp, NULL);

    if (result.nwinners > 0) {
        printf("Resolution: extension %u wins\n",
               (unsigned)result.winning_contexts[0].ext_id);
        printf("  Confidence: %.2f\n", (double)result.confidence);
        if (result.explanation) {
            printf("  Explanation: %s\n", result.explanation);
        }
    } else {
        printf("Resolution: unresolved (LLM unavailable or failed)\n");
        if (result.explanation) {
            printf("  Note: %s\n", result.explanation);
        }
        printf("\n  Tip: Set LIME_LLM_API_KEY environment variable to enable LLM.\n");
        printf("  Supported providers:\n");
        printf("    LIME_LLM_PROVIDER=openai (default)\n");
        printf("    LIME_LLM_PROVIDER=anthropic\n");
    }

    /* Cleanup */
    strategy_result_cleanup(&result);
    free(cp.description);
    /* Note: cp.contexts points to stack array, not freed */
    cp.contexts = NULL;

    disambiguation_destroy(dis);
    destroy_extension_registry(reg);

    printf("\nDone.\n");
    return 0;
}
