/*
** src/lexicon.h -- internal layout of LimeLexicon (include/lime_lexer.h).
**
** A lexicon is the lexer half of a ParserSnapshot: the keyword table,
** the operator trie and (B5) the extension-pattern DFA.  It is built
** once at compose time (src/lexicon_build.c, liblime_compiler), never
** written after it is attached to a snapshot, and freed with the
** snapshot.  Readers (src/kw_table.c, src/op_trie.c, liblime_parser)
** therefore need no lock: a reader holds a snapshot reference, and the
** snapshot holds the lexicon.
**
** Shared by the reader and the builder only; not installed.
*/
#ifndef LIME_LEXICON_H
#define LIME_LEXICON_H

#include "lime_lexer.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

/* One open-addressing slot.  len == 0 marks an empty slot (keywords
** are never empty).  `hash` is the full 32-bit hash so a probe rejects
** most non-matches without touching the string pool. */
typedef struct LimeKwSlot {
    uint32_t hash;
    uint32_t len;
    int32_t code;
    uint32_t off; /* offset of the (folded) text in LimeLexicon.pool */
} LimeKwSlot;

/* Item kinds. */
#define LIME_LEXITEM_KW 0u
#define LIME_LEXITEM_OP 1u

/* One source item, kept so a recompose can re-resolve token names
** against the new snapshot and check that base codes did not move. */
typedef struct LimeLexItemRec {
    uint32_t kind;      /* LIME_LEXITEM_*                          */
    uint32_t text_off;  /* into pool (folded when kind=KW and fold) */
    uint32_t text_len;
    uint32_t token_off; /* into pool: NUL-terminated token name    */
    int32_t code;
} LimeLexItemRec;

struct LimeLexicon {
    uint32_t flags; /* LIME_LEX_* */

    /* Keyword table: power-of-two slots, load factor <= 1/2. */
    uint32_t kw_mask; /* nslots - 1; 0 when there are no keywords */
    uint32_t kw_count;
    uint32_t kw_maxlen; /* longer input cannot be a keyword */
    LimeKwSlot *kw_slots;

    /* Operator trie, longest match.  Node 0 is the root.  Byte b moves
    ** node n to op_next[n * op_ncls + op_cmap[b]]; 0 there means "no
    ** child" (the root is never a child).  Class 0 is every byte that
    ** occurs in no operator, so its column is all zeros and ends the
    ** walk.  op_code[n] is the token code when the path to n spells an
    ** operator, else -1.  Typical tables: PostgreSQL's 21 multi-char
    ** operators + self chars = ~40 nodes x ~20 classes x 2 B = 1.6 KB. */
    uint8_t op_cmap[256];
    uint32_t op_ncls;
    uint32_t op_nnodes;
    uint32_t op_count;
    uint16_t *op_next;
    int32_t *op_code;

    /* Source items, sorted by (kind, text), for recompose. */
    uint32_t nitems;
    LimeLexItemRec *items;

    char *pool;
    size_t pool_len;
};

/* ASCII-only case fold, branch-free: 'A'..'Z' -> 'a'..'z'. */
static inline unsigned char lime_lex_fold(unsigned char c) {
    return (unsigned char)(c | ((unsigned)((unsigned)(c - 'A') < 26u) << 5));
}

/* FNV-1a over (optionally folded) bytes.  Builder and reader must
** agree, so it lives here. */
static inline uint32_t lime_lex_hash(const char *s, size_t n, int fold) {
    uint32_t h = 2166136261u;
    if (fold) {
        for (size_t i = 0; i < n; i++) {
            h ^= lime_lex_fold((unsigned char)s[i]);
            h *= 16777619u;
        }
    } else {
        for (size_t i = 0; i < n; i++) {
            h ^= (unsigned char)s[i];
            h *= 16777619u;
        }
    }
    return h;
}

/* Release a lexicon.  Inline (not a call into kw_table.c) so
** destroy_snapshot needs no new link dependency: every build that
** compiles snapshot.c by hand keeps linking. */
static inline void lime_lexicon_destroy(struct LimeLexicon *lx) {
    if (lx == NULL) return;
    free(lx->kw_slots);
    free(lx->op_next);
    free(lx->op_code);
    free(lx->items);
    free(lx->pool);
    free(lx);
}

#endif /* LIME_LEXICON_H */
