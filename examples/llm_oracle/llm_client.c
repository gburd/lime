/*
** LLM Client implementation.
**
** Uses libcurl to send HTTP requests to OpenAI or Anthropic APIs.
** Constructs JSON payloads manually (no JSON library dependency)
** and does minimal JSON parsing on the response to extract the
** content field.
**
** Compile with: -lcurl
*/
#define _GNU_SOURCE  /* for strdup on glibc */
#include "llm_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Attempt to use libcurl; provide stubs if unavailable               */
/* ------------------------------------------------------------------ */

#ifdef LLM_NO_CURL

/* Stub mode: no network, always returns an error */

static char *stub_dup_str(const char *s) {
    if (s == NULL) return NULL;
    size_t len = strlen(s) + 1;
    char *copy = malloc(len);
    if (copy) memcpy(copy, s, len);
    return copy;
}

struct LlmClient {
    LlmClientConfig config;
};

LlmClient *llm_client_create(const LlmClientConfig *config) {
    if (config == NULL || config->api_key == NULL) return NULL;
    LlmClient *c = calloc(1, sizeof(LlmClient));
    if (c) c->config = *config;
    return c;
}

LlmResponse llm_client_query(LlmClient *client,
                              const char *system_prompt,
                              const char *user_prompt) {
    (void)client; (void)system_prompt; (void)user_prompt;
    LlmResponse r = {0};
    r.success = false;
    r.error = stub_dup_str("LLM client compiled without libcurl (LLM_NO_CURL)");
    return r;
}

bool llm_client_available(const LlmClient *client) {
    (void)client;
    return false;
}

void llm_response_free(LlmResponse *resp) {
    if (resp == NULL) return;
    free(resp->content);
    free(resp->error);
    resp->content = NULL;
    resp->error = NULL;
}

void llm_client_destroy(LlmClient *client) {
    free(client);
}

#else /* libcurl available */

#include <curl/curl.h>

/* ------------------------------------------------------------------ */
/*  Internal types                                                      */
/* ------------------------------------------------------------------ */

struct LlmClient {
    LlmClientConfig config;
    char *api_key_copy;
    char *model_copy;
    char *base_url_copy;
    CURL *curl;
};

/* Dynamic string buffer for curl write callback */
typedef struct {
    char *data;
    size_t len;
    size_t capacity;
} Buffer;

/* ------------------------------------------------------------------ */
/*  Helpers                                                             */
/* ------------------------------------------------------------------ */

static char *dup_str(const char *s) {
    if (s == NULL) return NULL;
    size_t len = strlen(s) + 1;
    char *copy = malloc(len);
    if (copy) memcpy(copy, s, len);
    return copy;
}

static void buffer_init(Buffer *buf) {
    buf->data = malloc(1024);
    buf->len = 0;
    buf->capacity = buf->data ? 1024 : 0;
    if (buf->data) buf->data[0] = '\0';
}

static void buffer_free(Buffer *buf) {
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->capacity = 0;
}

static size_t write_callback(void *ptr, size_t size, size_t nmemb,
                             void *userdata) {
    Buffer *buf = (Buffer *)userdata;
    size_t total = size * nmemb;

    if (buf->len + total + 1 > buf->capacity) {
        size_t new_cap = (buf->len + total + 1) * 2;
        char *p = realloc(buf->data, new_cap);
        if (p == NULL) return 0;
        buf->data = p;
        buf->capacity = new_cap;
    }

    memcpy(buf->data + buf->len, ptr, total);
    buf->len += total;
    buf->data[buf->len] = '\0';
    return total;
}

/*
** Escape a string for inclusion in a JSON string value.
** Returns a malloc'd escaped string. Caller must free.
*/
static char *json_escape(const char *s) {
    if (s == NULL) return dup_str("");

    size_t len = strlen(s);
    /* Worst case: every char needs escaping (\uXXXX = 6 chars) */
    char *out = malloc(len * 6 + 1);
    if (out == NULL) return NULL;

    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
        case '"':  out[j++] = '\\'; out[j++] = '"';  break;
        case '\\': out[j++] = '\\'; out[j++] = '\\'; break;
        case '\n': out[j++] = '\\'; out[j++] = 'n';  break;
        case '\r': out[j++] = '\\'; out[j++] = 'r';  break;
        case '\t': out[j++] = '\\'; out[j++] = 't';  break;
        default:
            if (c < 0x20) {
                j += (size_t)snprintf(out + j, 7, "\\u%04x", c);
            } else {
                out[j++] = (char)c;
            }
            break;
        }
    }
    out[j] = '\0';
    return out;
}

/*
** Extract a string value from a JSON object for a given key.
** Very simple parser -- looks for "key": "value" patterns.
** Returns a malloc'd string or NULL.
*/
static char *json_extract_string(const char *json, const char *key) {
    if (json == NULL || key == NULL) return NULL;

    /* Build the search pattern: "key" */
    size_t klen = strlen(key);
    char *pattern = malloc(klen + 4);
    if (pattern == NULL) return NULL;
    snprintf(pattern, klen + 4, "\"%s\"", key);

    const char *pos = strstr(json, pattern);
    free(pattern);
    if (pos == NULL) return NULL;

    /* Skip past "key" and whitespace, expecting : */
    pos += klen + 2;
    while (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r') pos++;
    if (*pos != ':') return NULL;
    pos++;
    while (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r') pos++;

    if (*pos != '"') return NULL;
    pos++; /* skip opening quote */

    /* Extract until unescaped closing quote */
    Buffer buf;
    buffer_init(&buf);

    while (*pos != '\0') {
        if (*pos == '\\' && pos[1] != '\0') {
            /* Simplified: just copy the escaped char literally */
            switch (pos[1]) {
            case 'n': buf.data[buf.len++] = '\n'; break;
            case 't': buf.data[buf.len++] = '\t'; break;
            case 'r': buf.data[buf.len++] = '\r'; break;
            case '"': buf.data[buf.len++] = '"'; break;
            case '\\': buf.data[buf.len++] = '\\'; break;
            default: buf.data[buf.len++] = pos[1]; break;
            }
            pos += 2;
        } else if (*pos == '"') {
            break;
        } else {
            buf.data[buf.len++] = *pos;
            pos++;
        }

        if (buf.len + 2 >= buf.capacity) {
            size_t new_cap = buf.capacity * 2;
            char *p = realloc(buf.data, new_cap);
            if (p == NULL) { buffer_free(&buf); return NULL; }
            buf.data = p;
            buf.capacity = new_cap;
        }
    }

    buf.data[buf.len] = '\0';
    return buf.data;
}

/* ------------------------------------------------------------------ */
/*  Default URLs and models                                             */
/* ------------------------------------------------------------------ */

static const char *default_url(LlmProvider provider) {
    switch (provider) {
    case LLM_PROVIDER_OPENAI:
        return "https://api.openai.com/v1/chat/completions";
    case LLM_PROVIDER_ANTHROPIC:
        return "https://api.anthropic.com/v1/messages";
    }
    return "";
}

static const char *default_model(LlmProvider provider) {
    switch (provider) {
    case LLM_PROVIDER_OPENAI:    return "gpt-4o-mini";
    case LLM_PROVIDER_ANTHROPIC: return "claude-sonnet-4-20250514";
    }
    return "";
}

/* ------------------------------------------------------------------ */
/*  Build JSON request body                                             */
/* ------------------------------------------------------------------ */

static char *build_openai_body(const LlmClient *c,
                               const char *system_prompt,
                               const char *user_prompt) {
    char *sys_esc = json_escape(system_prompt);
    char *usr_esc = json_escape(user_prompt);
    if (!sys_esc || !usr_esc) { free(sys_esc); free(usr_esc); return NULL; }

    size_t model_len = strlen(c->model_copy);
    size_t total = model_len + strlen(sys_esc) + strlen(usr_esc) + 512;
    char *body = malloc(total);
    if (body == NULL) { free(sys_esc); free(usr_esc); return NULL; }

    snprintf(body, total,
        "{"
        "\"model\":\"%s\","
        "\"messages\":["
            "{\"role\":\"system\",\"content\":\"%s\"},"
            "{\"role\":\"user\",\"content\":\"%s\"}"
        "],"
        "\"temperature\":%.2f,"
        "\"max_tokens\":%d"
        "}",
        c->model_copy, sys_esc, usr_esc,
        (double)c->config.temperature,
        c->config.max_tokens > 0 ? c->config.max_tokens : 256);

    free(sys_esc);
    free(usr_esc);
    return body;
}

static char *build_anthropic_body(const LlmClient *c,
                                  const char *system_prompt,
                                  const char *user_prompt) {
    char *sys_esc = json_escape(system_prompt);
    char *usr_esc = json_escape(user_prompt);
    if (!sys_esc || !usr_esc) { free(sys_esc); free(usr_esc); return NULL; }

    size_t model_len = strlen(c->model_copy);
    size_t total = model_len + strlen(sys_esc) + strlen(usr_esc) + 512;
    char *body = malloc(total);
    if (body == NULL) { free(sys_esc); free(usr_esc); return NULL; }

    snprintf(body, total,
        "{"
        "\"model\":\"%s\","
        "\"system\":\"%s\","
        "\"messages\":["
            "{\"role\":\"user\",\"content\":\"%s\"}"
        "],"
        "\"temperature\":%.2f,"
        "\"max_tokens\":%d"
        "}",
        c->model_copy, sys_esc, usr_esc,
        (double)c->config.temperature,
        c->config.max_tokens > 0 ? c->config.max_tokens : 256);

    free(sys_esc);
    free(usr_esc);
    return body;
}

/* ------------------------------------------------------------------ */
/*  Parse response                                                      */
/* ------------------------------------------------------------------ */

static char *extract_content(LlmProvider provider, const char *json) {
    if (json == NULL) return NULL;

    if (provider == LLM_PROVIDER_OPENAI) {
        /* OpenAI: choices[0].message.content */
        char *content = json_extract_string(json, "content");
        return content;
    } else {
        /* Anthropic: content[0].text */
        char *text = json_extract_string(json, "text");
        return text;
    }
}

/* ------------------------------------------------------------------ */
/*  Public API                                                          */
/* ------------------------------------------------------------------ */

LlmClient *llm_client_create(const LlmClientConfig *config) {
    if (config == NULL || config->api_key == NULL) return NULL;
    if (config->api_key[0] == '\0') return NULL;

    LlmClient *c = calloc(1, sizeof(LlmClient));
    if (c == NULL) return NULL;

    c->config = *config;
    c->api_key_copy = dup_str(config->api_key);
    c->model_copy = dup_str(config->model ? config->model
                            : default_model(config->provider));
    c->base_url_copy = dup_str(config->base_url ? config->base_url
                               : default_url(config->provider));

    if (!c->api_key_copy || !c->model_copy || !c->base_url_copy) {
        llm_client_destroy(c);
        return NULL;
    }

    if (c->config.temperature <= 0.0f) c->config.temperature = 0.1f;
    if (c->config.max_tokens <= 0) c->config.max_tokens = 256;
    if (c->config.timeout_ms <= 0) c->config.timeout_ms = 10000;

    c->curl = curl_easy_init();
    if (c->curl == NULL) {
        llm_client_destroy(c);
        return NULL;
    }

    return c;
}

LlmResponse llm_client_query(LlmClient *client,
                              const char *system_prompt,
                              const char *user_prompt) {
    LlmResponse resp = {0};

    if (client == NULL || client->curl == NULL) {
        resp.error = dup_str("client not initialized");
        return resp;
    }
    if (system_prompt == NULL) system_prompt = "";
    if (user_prompt == NULL || user_prompt[0] == '\0') {
        resp.error = dup_str("empty prompt");
        return resp;
    }

    /* Build request body */
    char *body;
    if (client->config.provider == LLM_PROVIDER_OPENAI) {
        body = build_openai_body(client, system_prompt, user_prompt);
    } else {
        body = build_anthropic_body(client, system_prompt, user_prompt);
    }
    if (body == NULL) {
        resp.error = dup_str("failed to build request body");
        return resp;
    }

    /* Set up curl */
    CURL *curl = client->curl;
    curl_easy_reset(curl);

    curl_easy_setopt(curl, CURLOPT_URL, client->base_url_copy);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS,
                     (long)client->config.timeout_ms);

    /* Headers */
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    char auth_header[512];
    if (client->config.provider == LLM_PROVIDER_OPENAI) {
        snprintf(auth_header, sizeof(auth_header),
                 "Authorization: Bearer %s", client->api_key_copy);
        headers = curl_slist_append(headers, auth_header);
    } else {
        snprintf(auth_header, sizeof(auth_header),
                 "x-api-key: %s", client->api_key_copy);
        headers = curl_slist_append(headers, auth_header);
        headers = curl_slist_append(headers,
                                    "anthropic-version: 2023-06-01");
    }
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    /* Response buffer */
    Buffer response_buf;
    buffer_init(&response_buf);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_buf);

    /* Execute */
    CURLcode res = curl_easy_perform(curl);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    resp.status_code = (int)http_code;

    curl_slist_free_all(headers);
    free(body);

    if (res != CURLE_OK) {
        resp.success = false;
        resp.error = dup_str(curl_easy_strerror(res));
        buffer_free(&response_buf);
        return resp;
    }

    if (http_code < 200 || http_code >= 300) {
        resp.success = false;
        size_t err_len = response_buf.len + 64;
        resp.error = malloc(err_len);
        if (resp.error) {
            snprintf(resp.error, err_len,
                     "HTTP %d: %.500s", (int)http_code,
                     response_buf.data ? response_buf.data : "");
        }
        buffer_free(&response_buf);
        return resp;
    }

    /* Extract content from response JSON */
    char *content = extract_content(client->config.provider,
                                    response_buf.data);
    buffer_free(&response_buf);

    if (content == NULL) {
        resp.success = false;
        resp.error = dup_str("failed to parse response JSON");
        return resp;
    }

    resp.success = true;
    resp.content = content;
    return resp;
}

bool llm_client_available(const LlmClient *client) {
    return client != NULL && client->curl != NULL &&
           client->api_key_copy != NULL && client->api_key_copy[0] != '\0';
}

void llm_response_free(LlmResponse *resp) {
    if (resp == NULL) return;
    free(resp->content);
    free(resp->error);
    resp->content = NULL;
    resp->error = NULL;
}

void llm_client_destroy(LlmClient *client) {
    if (client == NULL) return;
    if (client->curl) curl_easy_cleanup(client->curl);
    free(client->api_key_copy);
    free(client->model_copy);
    free(client->base_url_copy);
    free(client);
}

#endif /* LLM_NO_CURL */
