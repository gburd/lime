/*
** src/kw_table.c -- per-token readers for the snapshot lexicon
** (include/lime_lexer.h): keyword lookup, accessors, free.
**
** liblime_parser.  Nothing here allocates or locks: the lexicon is
** immutable once attached to a snapshot, and the caller holds the
** snapshot.  The builder is src/lexicon_build.c (liblime_compiler).
*/
#include "lexicon.h"
#include "snapshot.h"

#include <stdlib.h>
#include <string.h>

const LimeLexicon *lime_snapshot_lexicon(const ParserSnapshot *snap) {
    return snap ? snap->lexicon : NULL;
}

static int kw_eq(const char *kw, const char *s, size_t n, int fold) {
    if (!fold) return memcmp(kw, s, n) == 0;
    for (size_t i = 0; i < n; i++)
        if ((unsigned char)kw[i] != lime_lex_fold((unsigned char)s[i])) return 0;
    return 1;
}

int lime_kw_lookup(const LimeLexicon *lx, const char *s, size_t n) {
    /* kw_maxlen is 0 when there are no keywords, so this also covers
    ** the empty table (and rejects long identifiers without hashing). */
    if (lx == NULL || n == 0 || n > lx->kw_maxlen) return -1;
    int fold = (lx->flags & LIME_LEX_FOLD_CASE) != 0;
    uint32_t h = lime_lex_hash(s, n, fold);
    for (uint32_t i = h & lx->kw_mask;; i = (i + 1) & lx->kw_mask) {
        const LimeKwSlot *sl = &lx->kw_slots[i];
        if (sl->len == 0) return -1; /* load factor <= 1/2: always terminates */
        if (sl->hash == h && sl->len == n && kw_eq(lx->pool + sl->off, s, n, fold))
            return sl->code;
    }
}

uint32_t lime_lexicon_keyword_count(const LimeLexicon *lx) {
    return lx ? lx->kw_count : 0;
}

void lime_lexicon_free(LimeLexicon *lx) {
    lime_lexicon_destroy(lx);
}
