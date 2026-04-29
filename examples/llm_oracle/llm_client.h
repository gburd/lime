/*
** LLM Client - HTTP client for LLM API providers
**
** Provides a simple abstraction over libcurl to query LLM APIs
** (OpenAI, Anthropic) for disambiguation guidance.  The client
** sends a structured prompt describing a grammar conflict and
** parses the JSON response to extract a recommended resolution.
**
** This is an example/demonstration utility, not production-grade.
** Error handling is minimal and there is no retry logic.
*/
#ifndef LLM_CLIENT_H
#define LLM_CLIENT_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================== */
/*  Provider selection                                                  */
/* ================================================================== */

typedef enum LlmProvider {
    LLM_PROVIDER_OPENAI    = 0,
    LLM_PROVIDER_ANTHROPIC = 1,
} LlmProvider;

/* ================================================================== */
/*  Configuration                                                       */
/* ================================================================== */

typedef struct LlmClientConfig {
    LlmProvider provider;
    const char *api_key;            /* API key (required)                  */
    const char *model;              /* Model name (NULL for default)       */
    const char *base_url;           /* Override API URL (NULL for default) */
    int timeout_ms;                 /* Request timeout (0 for default 10s) */
    float temperature;              /* Sampling temperature (0.0 - 2.0)   */
    int max_tokens;                 /* Max response tokens (0 for 256)    */
} LlmClientConfig;

/* ================================================================== */
/*  Client handle                                                       */
/* ================================================================== */

typedef struct LlmClient LlmClient;

/* ================================================================== */
/*  Response                                                            */
/* ================================================================== */

typedef struct LlmResponse {
    bool success;                   /* True if API call succeeded          */
    char *content;                  /* Response text (malloc'd)            */
    char *error;                    /* Error message if !success (malloc'd)*/
    int status_code;                /* HTTP status code                    */
    int tokens_used;                /* Tokens consumed (from usage field)  */
} LlmResponse;

/* ================================================================== */
/*  API                                                                 */
/* ================================================================== */

/*
** Create an LLM client.  Returns NULL if libcurl is unavailable
** or the config is invalid.
*/
LlmClient *llm_client_create(const LlmClientConfig *config);

/*
** Send a prompt to the LLM and get a response.
** The system_prompt provides context; user_prompt is the query.
** Caller must call llm_response_free() on the result.
*/
LlmResponse llm_client_query(LlmClient *client,
                              const char *system_prompt,
                              const char *user_prompt);

/*
** Check if the LLM client is functional (API key set, curl available).
*/
bool llm_client_available(const LlmClient *client);

/*
** Free resources owned by an LlmResponse.
*/
void llm_response_free(LlmResponse *resp);

/*
** Destroy an LLM client and free all resources.
*/
void llm_client_destroy(LlmClient *client);

#ifdef __cplusplus
}
#endif

#endif /* LLM_CLIENT_H */
