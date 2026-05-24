/*
** Thread-safe Token Table Implementation
**
** Concurrent reads are protected by a pthread_rwlock (read lock),
** allowing multiple readers simultaneously. Writers acquire the
** write lock for mutual exclusion when adding/removing tokens.
** A version counter is maintained for external change detection.
*/
#include "token_table.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Case-insensitive FNV-1a hash (SQL keywords are case-insensitive)    */
/* ------------------------------------------------------------------ */
static uint32_t hash_string_ci(const char *str, size_t len) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        uint8_t c = (uint8_t)str[i];
        /* ASCII uppercase -> lowercase */
        if (c >= 'A' && c <= 'Z') c += 32;
        h ^= c;
        h *= 16777619u;
    }
    return h;
}

/* ------------------------------------------------------------------ */
/* Case-insensitive memcmp for ASCII                                   */
/* ------------------------------------------------------------------ */
static int memcmp_ci(const char *a, const char *b, size_t len) {
    for (size_t i = 0; i < len; i++) {
        uint8_t ca = (uint8_t)a[i];
        uint8_t cb = (uint8_t)b[i];
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return (int)ca - (int)cb;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Internal: rebuild hash table from the current tokens array          */
/* Caller must hold the write lock.                                    */
/* ------------------------------------------------------------------ */
static void rebuild_hash_table(TokenTable *table) {
    for (uint32_t i = 0; i < table->hash_capacity; i++) {
        table->hash_table[i] = INVALID_INDEX;
    }
    for (uint32_t i = 0; i < table->ntokens; i++) {
        table->tokens[i].next_in_chain = INVALID_INDEX;
    }
    for (uint32_t i = 0; i < table->ntokens; i++) {
        uint32_t bucket = hash_string_ci(table->tokens[i].lexeme, table->tokens[i].lexeme_len) %
                          table->hash_capacity;
        table->tokens[i].next_in_chain = table->hash_table[bucket];
        table->hash_table[bucket] = i;
    }
}

/* ------------------------------------------------------------------ */
/* Internal: grow the tokens array if full. Caller holds write lock.   */
/* Returns false on allocation failure.                                */
/* ------------------------------------------------------------------ */
static bool ensure_capacity(TokenTable *table) {
    if (table->ntokens < table->capacity) return true;

    uint32_t new_capacity = table->capacity * 2;
    if (new_capacity < 16) new_capacity = 16;

    TokenDefinition *new_tokens = realloc(table->tokens, new_capacity * sizeof(TokenDefinition));
    if (!new_tokens) return false;
    table->tokens = new_tokens;
    table->capacity = new_capacity;

    /* Grow hash table to maintain a load factor <= 0.5. */
    uint32_t new_hash_cap = new_capacity * 2;
    uint32_t *new_ht = realloc(table->hash_table, new_hash_cap * sizeof(uint32_t));
    if (!new_ht) return false;
    table->hash_table = new_ht;
    table->hash_capacity = new_hash_cap;

    rebuild_hash_table(table);
    return true;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

TokenTable *create_token_table(uint32_t initial_capacity) {
    if (initial_capacity == 0) initial_capacity = 64;

    TokenTable *table = calloc(1, sizeof(TokenTable));
    if (!table) return NULL;

    table->capacity = initial_capacity;
    table->ntokens = 0;

    table->tokens = calloc(initial_capacity, sizeof(TokenDefinition));
    if (!table->tokens) {
        free(table);
        return NULL;
    }

    table->hash_capacity = initial_capacity * 2;
    table->hash_table = malloc(table->hash_capacity * sizeof(uint32_t));
    if (!table->hash_table) {
        free(table->tokens);
        free(table);
        return NULL;
    }
    for (uint32_t i = 0; i < table->hash_capacity; i++) {
        table->hash_table[i] = INVALID_INDEX;
    }

    atomic_init(&table->version, 0);

    if (pthread_rwlock_init(&table->lock, NULL) != 0) {
        free(table->hash_table);
        free(table->tokens);
        free(table);
        return NULL;
    }

    return table;
}

void destroy_token_table(TokenTable *table) {
    if (!table) return;

    for (uint32_t i = 0; i < table->ntokens; i++) {
        free((void *)table->tokens[i].lexeme);
    }
    free(table->tokens);
    free(table->hash_table);
    pthread_rwlock_destroy(&table->lock);
    free(table);
}

int lookup_token(TokenTable *table, const char *str, size_t len) {
    if (!table) return -1;

    uint32_t hash = hash_string_ci(str, len);

    /*
    ** Acquire a read lock to prevent writers from reallocating or
    ** compacting the tokens/hash_table arrays while we traverse them.
    ** Multiple readers can hold the read lock concurrently.
    */
    pthread_rwlock_rdlock(&table->lock);

    uint32_t idx = table->hash_table[hash % table->hash_capacity];

    while (idx != INVALID_INDEX) {
        TokenDefinition *tok = &table->tokens[idx];

        if (tok->lexeme_len == len && memcmp_ci(tok->lexeme, str, len) == 0) {
            int code = tok->token_code;
            LIME_RWLOCK_RDUNLOCK(&table->lock);
            return code;
        }
        idx = tok->next_in_chain;
    }

    LIME_RWLOCK_RDUNLOCK(&table->lock);
    return -1;
}

bool add_token(TokenTable *table, const char *lexeme, int token_code, ExtensionID ext_id) {
    if (!table || !lexeme) return false;

    size_t len = strlen(lexeme);

    pthread_rwlock_wrlock(&table->lock);

    /* Check for duplicate. */
    uint32_t hash = hash_string_ci(lexeme, len);
    uint32_t bucket = hash % table->hash_capacity;
    uint32_t idx = table->hash_table[bucket];

    while (idx != INVALID_INDEX) {
        TokenDefinition *tok = &table->tokens[idx];
        if (tok->lexeme_len == len && memcmp_ci(tok->lexeme, lexeme, len) == 0) {
            LIME_RWLOCK_WRUNLOCK(&table->lock);
            return false; /* duplicate */
        }
        idx = tok->next_in_chain;
    }

    /* Ensure we have room. */
    if (!ensure_capacity(table)) {
        LIME_RWLOCK_WRUNLOCK(&table->lock);
        return false;
    }

    /* After ensure_capacity the hash table may have been rebuilt, so
    ** recompute the bucket. */
    bucket = hash % table->hash_capacity;

    /* Copy the lexeme string. */
    char *dup = malloc(len + 1);
    if (!dup) {
        LIME_RWLOCK_WRUNLOCK(&table->lock);
        return false;
    }
    memcpy(dup, lexeme, len + 1);

    /* Append to tokens array. */
    uint32_t new_idx = table->ntokens;
    TokenDefinition *new_tok = &table->tokens[new_idx];
    new_tok->lexeme = dup;
    new_tok->lexeme_len = len;
    new_tok->token_code = token_code;
    new_tok->extension_id = ext_id;

    /* Insert at head of hash chain. */
    new_tok->next_in_chain = table->hash_table[bucket];
    table->hash_table[bucket] = new_idx;

    table->ntokens++;

    /* Bump version AFTER changes are visible so readers will retry. */
    atomic_fetch_add_explicit(&table->version, 1, memory_order_release);

    LIME_RWLOCK_WRUNLOCK(&table->lock);
    return true;
}

bool remove_tokens_by_extension(TokenTable *table, ExtensionID ext_id) {
    pthread_rwlock_wrlock(&table->lock);

    /*
    ** Compact the tokens array in-place, removing entries that belong to
    ** the specified extension.  We copy surviving tokens to the front of
    ** the array and then rebuild the hash chains.
    */
    uint32_t dst = 0;
    for (uint32_t src = 0; src < table->ntokens; src++) {
        if (table->tokens[src].extension_id == ext_id) {
            free((void *)table->tokens[src].lexeme);
            continue;
        }
        if (dst != src) {
            table->tokens[dst] = table->tokens[src];
        }
        dst++;
    }

    bool changed = (dst != table->ntokens);
    table->ntokens = dst;

    if (changed) {
        rebuild_hash_table(table);
        atomic_fetch_add_explicit(&table->version, 1, memory_order_release);
    }

    LIME_RWLOCK_WRUNLOCK(&table->lock);
    return true;
}
