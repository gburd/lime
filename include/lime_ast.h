#ifndef LIME_AST_H
#define LIME_AST_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
** Arena allocator for AST nodes.
** Allocates from large contiguous blocks, freeing everything at once.
** This is ideal for parse trees that are built once and freed together.
*/
typedef struct LimeArena {
    char *base;
    size_t used;
    size_t capacity;
    struct LimeArena *next;  /* linked list of arena blocks */
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
