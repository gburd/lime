#ifndef LIME_AST_H
#define LIME_AST_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Arena allocator for AST nodes.
 *
 * Allocates from large contiguous blocks, freeing everything at
 * once.  Ideal for parse trees that are built once and freed
 * together as a unit.
 */
typedef struct LimeArena {
    char *base;              /**< Base pointer of the current block */
    size_t used;             /**< Bytes used in the current block */
    size_t capacity;         /**< Total bytes in the current block */
    struct LimeArena *next;  /**< Next block in the linked list of blocks */
} LimeArena;

/* Create an arena with the given initial block size. Returns NULL on failure. */
LimeArena *lime_arena_create(size_t initial_size);

/* Allocate size bytes from the arena with pointer alignment.
** Returns NULL only if malloc fails for a new block. Never fails
** for sizes smaller than the block size. */
void *lime_arena_alloc(LimeArena *arena, size_t size);

/* Allocate and zero-fill size bytes from the arena. */
void *lime_arena_calloc(LimeArena *arena, size_t size);

/* Duplicate a string into the arena. */
char *lime_arena_strdup(LimeArena *arena, const char *s);

/* Destroy the arena and all its blocks. Passing NULL is safe. */
void lime_arena_destroy(LimeArena *arena);

/* Return total bytes allocated across all blocks. */
size_t lime_arena_total_allocated(const LimeArena *arena);

/* Return total bytes used across all blocks. */
size_t lime_arena_total_used(const LimeArena *arena);

#ifdef __cplusplus
}
#endif

#endif /* LIME_AST_H */
