#include "lime_ast.h"

#include <stdlib.h>
#include <string.h>

#define LIME_ARENA_DEFAULT_SIZE 4096
#define LIME_ARENA_MIN_BLOCK    256
#define LIME_ARENA_ALIGNMENT    (sizeof(void *) < 8 ? 8 : sizeof(void *))

static size_t align_up(size_t n, size_t align) {
    return (n + align - 1) & ~(align - 1);
}

LimeArena *lime_arena_create(size_t initial_size) {
    if (initial_size == 0) initial_size = LIME_ARENA_DEFAULT_SIZE;
    if (initial_size < LIME_ARENA_MIN_BLOCK) initial_size = LIME_ARENA_MIN_BLOCK;

    LimeArena *arena = (LimeArena *)malloc(sizeof(LimeArena));
    if (!arena) return NULL;

    arena->base = (char *)malloc(initial_size);
    if (!arena->base) {
        free(arena);
        return NULL;
    }
    arena->used = 0;
    arena->capacity = initial_size;
    arena->next = NULL;
    return arena;
}

void *lime_arena_alloc(LimeArena *arena, size_t size) {
    if (!arena || size == 0) return NULL;

    size_t aligned = align_up(size, LIME_ARENA_ALIGNMENT);
    size_t avail = arena->capacity - arena->used;

    if (aligned <= avail) {
        void *ptr = arena->base + arena->used;
        arena->used += aligned;
        return ptr;
    }

    /* Current block is full -- allocate a new block and prepend it. */
    size_t block_size = arena->capacity * 2;
    if (block_size < aligned) block_size = aligned;
    if (block_size < LIME_ARENA_MIN_BLOCK) block_size = LIME_ARENA_MIN_BLOCK;

    LimeArena *new_block = (LimeArena *)malloc(sizeof(LimeArena));
    if (!new_block) return NULL;

    new_block->base = (char *)malloc(block_size);
    if (!new_block->base) {
        free(new_block);
        return NULL;
    }

    /* Move current arena contents into the new node, then make arena
    ** the head of the list pointing at the new large block. */
    new_block->used = arena->used;
    new_block->capacity = arena->capacity;
    new_block->next = arena->next;

    /* Swap bases: the old base goes to new_block, arena gets the fresh block. */
    char *old_base = arena->base;
    arena->base = new_block->base;
    new_block->base = old_base;
    arena->used = aligned;
    arena->capacity = block_size;
    arena->next = new_block;

    return arena->base;
}

void *lime_arena_calloc(LimeArena *arena, size_t size) {
    void *ptr = lime_arena_alloc(arena, size);
    if (ptr) memset(ptr, 0, size);
    return ptr;
}

char *lime_arena_strdup(LimeArena *arena, const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *dup = (char *)lime_arena_alloc(arena, len);
    if (dup) memcpy(dup, s, len);
    return dup;
}

void lime_arena_destroy(LimeArena *arena) {
    if (!arena) return;
    /* Walk the chain starting at arena->next (the secondary blocks). */
    LimeArena *cur = arena->next;
    while (cur) {
        LimeArena *tmp = cur->next;
        free(cur->base);
        free(cur);
        cur = tmp;
    }
    /* Free the head block's storage and the arena struct itself. */
    free(arena->base);
    free(arena);
}

size_t lime_arena_total_allocated(const LimeArena *arena) {
    size_t total = 0;
    const LimeArena *cur = arena;
    while (cur) {
        total += cur->capacity;
        cur = cur->next;
    }
    return total;
}

size_t lime_arena_total_used(const LimeArena *arena) {
    size_t total = 0;
    const LimeArena *cur = arena;
    while (cur) {
        total += cur->used;
        cur = cur->next;
    }
    return total;
}
