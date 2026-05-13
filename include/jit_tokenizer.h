/*
** JIT-compiled keyword tokenizer.
**
** Generates optimized machine code for keyword classification using LLVM
** OrcJIT. Instead of hash table lookups, the JIT builds a trie/switch-based
** classifier that maps input strings directly to token codes through a
** series of character comparisons compiled to native branch sequences.
**
** When JIT is unavailable (LIME_NO_JIT defined), the classifier returns -1
** for all inputs, signaling the caller to fall back to the hash-based
** TokenTable lookup.
*/
#ifndef JIT_TOKENIZER_H
#define JIT_TOKENIZER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TokenTable TokenTable;

/* ------------------------------------------------------------------ */
/*  Statistics                                                          */
/* ------------------------------------------------------------------ */

/**
 * @brief JIT compilation statistics for the tokenizer/keyword trie.
 */
typedef struct JITTokenizerStats {
    uint32_t keywords_compiled;  /**< Number of keywords in the compiled trie */
    uint64_t compile_time_ns;    /**< Wall-clock nanoseconds to compile */
    uint64_t code_size_bytes;    /**< Approximate generated code size in bytes */
} JITTokenizerStats;

/* ------------------------------------------------------------------ */
/*  Opaque handle                                                      */
/* ------------------------------------------------------------------ */

typedef struct JITTokenizer JITTokenizer;

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

/*
** Create a JIT-compiled tokenizer from a TokenTable.
**
** Reads all keywords from the table and compiles a trie-based classifier
** that maps (input, length) pairs to token codes. The resulting
** JITTokenizer is independent of the original table and does not hold
** a reference to it.
**
** Returns NULL if:
**   - JIT is not available (LIME_NO_JIT)
**   - table is NULL or empty
**   - LLVM compilation fails
*/
JITTokenizer *jit_tokenizer_create(const TokenTable *table);

/*
** Destroy a JIT tokenizer and free all associated resources.
** Passing NULL is safe and does nothing.
*/
void jit_tokenizer_destroy(JITTokenizer *tok);

/*
** Classify a keyword using the JIT-compiled trie.
**
** Returns the token code if the input matches a compiled keyword,
** or -1 if no match is found (caller should fall back to hash lookup).
**
** The comparison is case-insensitive for ASCII letters (matching the
** TokenTable's behavior for SQL keywords).
**
** Parameters:
**   tok   - JIT tokenizer (must not be NULL)
**   input - Pointer to the keyword string (not necessarily NUL-terminated)
**   len   - Length of the input string in bytes
*/
int jit_tokenizer_classify_keyword(const JITTokenizer *tok,
                                   const char *input, size_t len);

/*
** Get compilation statistics for the tokenizer.
*/
JITTokenizerStats jit_tokenizer_get_stats(const JITTokenizer *tok);

/*
** Check whether the JIT tokenizer is available at runtime.
** Returns true if LLVM support was compiled in and initialization
** succeeds, false otherwise.
*/
bool jit_tokenizer_is_available(void);

#ifdef __cplusplus
}
#endif

#endif /* JIT_TOKENIZER_H */
