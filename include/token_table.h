/*
** Thread-safe Token Table
**
** Provides concurrent token lookup via reader-writer locking.
** Multiple readers can look up tokens simultaneously; writers
** acquire exclusive access for modifications.  A version counter
** is maintained for external change detection.
*/
#ifndef TOKEN_TABLE_H
#define TOKEN_TABLE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <pthread.h>

#define INVALID_INDEX 0xFFFFFFFF

typedef uint32_t ExtensionID;

/**
 * @brief A single token definition in the table.
 */
typedef struct TokenDefinition {
    const char *lexeme;       /**< Token string (e.g., "SELECT") */
    size_t lexeme_len;        /**< Length of @ref lexeme */
    int token_code;           /**< Token ID (e.g., TK_SELECT) */
    ExtensionID extension_id; /**< Which extension added it (0 = base) */
    uint32_t next_in_chain;   /**< Hash collision chain link */
} TokenDefinition;

/**
 * @brief Thread-safe token lookup table.
 *
 * Multiple readers can look up tokens simultaneously; writers
 * acquire exclusive access for modifications.  A version counter
 * is maintained for external change detection.
 */
typedef struct TokenTable {
    TokenDefinition *tokens; /**< Dense array of tokens */
    uint32_t ntokens;        /**< Number of tokens in @ref tokens */
    uint32_t capacity;       /**< Allocated slots in @ref tokens */

    uint32_t *hash_table;   /**< Hash bucket table mapping to indices */
    uint32_t hash_capacity; /**< Length of @ref hash_table */

    atomic_uint_fast32_t version; /**< RCU-style version counter */
    pthread_rwlock_t lock;        /**< Reader/writer lock guarding the table */
} TokenTable;

/*
** Create a new token table with the given initial capacity.
** Returns NULL on allocation failure.
*/
TokenTable *create_token_table(uint32_t initial_capacity);

/*
** Destroy a token table and free all associated memory.
*/
void destroy_token_table(TokenTable *table);

/*
** Look up a token by string. Acquires read lock internally, so
** multiple threads can look up tokens concurrently.
** Returns the token_code if found, or -1 if not found.
*/
int lookup_token(TokenTable *table, const char *str, size_t len);

/*
** Add a token to the table. Acquires write lock internally.
** Returns true on success, false on failure (allocation error or duplicate).
*/
bool add_token(TokenTable *table, const char *lexeme, int token_code, ExtensionID ext_id);

/*
** Remove all tokens belonging to a given extension.
** Acquires write lock internally. Rebuilds hash chains.
** Returns true on success, false on failure.
*/
bool remove_tokens_by_extension(TokenTable *table, ExtensionID ext_id);

#endif /* TOKEN_TABLE_H */
