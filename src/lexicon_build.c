/*
** src/lexicon_build.c -- compose-time builders for the snapshot lexicon
** (include/lime_lexer.h): lime_snapshot_set_lexicon, lime_lex_compose.
**
** liblime_compiler.  Runs when a snapshot is built, never per token.
** The tables it produces are read lock-free by src/kw_table.c
** (liblime_parser) and are never written after the snapshot is shared.
*/
#include "extension.h"
#include "lexicon.h"
#include "snapshot.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int lime_snapshot_token_code(const ParserSnapshot *snap, const char *name);

/* A pending item: spelling + kind + the token it must produce. */
typedef struct {
    unsigned kind;
    const char *text; /* not owned */
    size_t len;
    const char *token; /* not owned */
    int code;
    char *key; /* owned: the comparison key (folded for keywords when fold) */
} Pending;

typedef struct {
    Pending *v;
    size_t n, cap;
} PendVec;

static char *fmt(const char *f, ...) {
    va_list ap;
    va_start(ap, f);
    int n = vsnprintf(NULL, 0, f, ap);
    va_end(ap);
    if (n < 0) return NULL;
    char *s = malloc((size_t)n + 1);
    if (s == NULL) return NULL;
    va_start(ap, f);
    vsnprintf(s, (size_t)n + 1, f, ap);
    va_end(ap);
    return s;
}

static char *oom(void) {
    return fmt("lexicon: out of memory");
}

static void pv_free(PendVec *pv) {
    for (size_t i = 0; i < pv->n; i++)
        free(pv->v[i].key);
    free(pv->v);
}

/* Resolve `token` against `snap` and append.  Returns NULL or an error. */
static char *pv_add(PendVec *pv, const ParserSnapshot *snap, unsigned kind, const char *text,
                    size_t len, const char *token, unsigned flags) {
    if (len == 0) return fmt("lexicon: empty %s for token %s",
                             kind == LIME_LEXITEM_KW ? "keyword" : "operator", token);
    if (token == NULL) return fmt("lexicon: '%.*s' names no token", (int)len, text);
    int code = lime_snapshot_token_code(snap, token);
    /* Only real terminals: 0 < index < nterminal (code = index +
    ** first_token; index 0 is "$", end of input). */
    if (code <= (int)snap->yy_first_token ||
        (uint32_t)(code - snap->yy_first_token) >= snap->nterminal)
        return fmt("lexicon: '%.*s' -> %s, which is not a terminal of the snapshot", (int)len,
                   text, token);
    if (pv->n == pv->cap) {
        size_t nc = pv->cap ? pv->cap * 2 : 64;
        Pending *nv = realloc(pv->v, nc * sizeof(*nv));
        if (nv == NULL) return oom();
        pv->v = nv;
        pv->cap = nc;
    }
    char *key = malloc(len + 1);
    if (key == NULL) return oom();
    int fold = kind == LIME_LEXITEM_KW && (flags & LIME_LEX_FOLD_CASE);
    for (size_t i = 0; i < len; i++)
        key[i] = fold ? (char)lime_lex_fold((unsigned char)text[i]) : text[i];
    key[len] = '\0';
    pv->v[pv->n++] = (Pending){ kind, text, len, token, code, key };
    return NULL;
}

/* Total order on (kind, key, token): the build is a function of the item
** SET, so the result does not depend on the order mods arrive in. */
static int pend_cmp(const void *a, const void *b) {
    const Pending *x = a, *y = b;
    if (x->kind != y->kind) return x->kind < y->kind ? -1 : 1;
    size_t m = x->len < y->len ? x->len : y->len;
    int c = memcmp(x->key, y->key, m);
    if (c != 0) return c;
    if (x->len != y->len) return x->len < y->len ? -1 : 1;
    return strcmp(x->token, y->token);
}

static uint32_t pool_put(char *pool, size_t *at, const char *s, size_t n) {
    uint32_t off = (uint32_t)*at;
    memcpy(pool + *at, s, n);
    pool[*at + n] = '\0';
    *at += n + 1;
    return off;
}

static int build_trie(LimeLexicon *lx);

/* Sort, reject one spelling claimed by two tokens, drop exact
** duplicates, and build the immutable lexicon. */
static char *build(PendVec *pv, unsigned flags, LimeLexicon **out) {
    *out = NULL;
    if (pv->n > 1) qsort(pv->v, pv->n, sizeof(Pending), pend_cmp);
    size_t n = 0, pool_len = 0, nkw = 0, kw_maxlen = 0;
    /* Compacts pv->v[0..n) in place; pv->n stays, so pv_free still sees
    ** every slot (moved and freed keys are NULLed). */
    for (size_t i = 0; i < pv->n; i++) {
        Pending *p = &pv->v[i];
        if (n > 0) {
            Pending *q = &pv->v[n - 1];
            if (q->kind == p->kind && q->len == p->len && memcmp(q->key, p->key, p->len) == 0) {
                if (strcmp(q->token, p->token) == 0) {
                    free(p->key); /* same spelling, same token: one entry */
                    p->key = NULL;
                    continue;
                }
                /* sorted, so q->token < p->token: deterministic message */
                return fmt("lexicon: %s '%s' is claimed by two tokens, %s and %s",
                           p->kind == LIME_LEXITEM_KW ? "keyword" : "operator", p->key,
                           q->token, p->token);
            }
        }
        if (n != i) {
            pv->v[n] = *p; /* compact; the key moves with it */
            p->key = NULL;
        }
        n++;
        pool_len += p->len + 1 + strlen(p->token) + 1;
        if (p->kind == LIME_LEXITEM_KW) {
            nkw++;
            if (p->len > kw_maxlen) kw_maxlen = p->len;
        }
    }
    if (pool_len >= UINT32_MAX || n >= UINT32_MAX / 2) return fmt("lexicon: too large");

    LimeLexicon *lx = calloc(1, sizeof(*lx));
    if (lx == NULL) return oom();
    lx->flags = flags;
    lx->pool = malloc(pool_len ? pool_len : 1);
    lx->items = calloc(n ? n : 1, sizeof(*lx->items));
    uint32_t nslots = 0;
    if (nkw > 0) {
        nslots = 2;
        while (nslots < 2 * nkw) nslots <<= 1; /* load factor <= 1/2 */
        lx->kw_slots = calloc(nslots, sizeof(*lx->kw_slots));
    }
    if (lx->pool == NULL || lx->items == NULL || (nkw > 0 && lx->kw_slots == NULL)) {
        lime_lexicon_destroy(lx);
        return oom();
    }
    size_t at = 0;
    for (size_t i = 0; i < n; i++) {
        const Pending *p = &pv->v[i];
        LimeLexItemRec *r = &lx->items[i];
        r->kind = p->kind;
        r->text_off = pool_put(lx->pool, &at, p->key, p->len);
        r->text_len = (uint32_t)p->len;
        r->token_off = pool_put(lx->pool, &at, p->token, strlen(p->token));
        r->code = p->code;
        if (p->kind != LIME_LEXITEM_KW) continue;
        uint32_t h = lime_lex_hash(p->key, p->len, 0); /* key is already folded */
        uint32_t s = h & (nslots - 1);
        while (lx->kw_slots[s].len != 0) s = (s + 1) & (nslots - 1);
        lx->kw_slots[s] = (LimeKwSlot){ h, (uint32_t)p->len, p->code, r->text_off };
    }
    lx->nitems = (uint32_t)n;
    lx->pool_len = at;
    lx->kw_mask = nslots ? nslots - 1 : 0;
    lx->kw_count = (uint32_t)nkw;
    lx->kw_maxlen = (uint32_t)kw_maxlen;
    if (build_trie(lx) != 0) {
        lime_lexicon_destroy(lx);
        return fmt("lexicon: out of memory or too many operator bytes");
    }
    *out = lx;
    return NULL;
}

/* Build the operator trie from the (sorted, deduplicated) OP items.
** Returns 0, or -1 on OOM / too many nodes for uint16_t. */
static int build_trie(LimeLexicon *lx) {
    size_t nops = 0, maxnodes = 1;
    for (uint32_t i = 0; i < lx->nitems; i++)
        if (lx->items[i].kind == LIME_LEXITEM_OP) {
            nops++;
            maxnodes += lx->items[i].text_len;
        }
    lx->op_count = (uint32_t)nops;
    if (nops == 0) return 0;
    if (maxnodes > UINT16_MAX) return -1;
    /* byte classes: 0 = in no operator; others numbered by byte value
    ** so the layout is a function of the operator set */
    uint32_t ncls = 1;
    for (uint32_t i = 0; i < lx->nitems; i++) {
        const LimeLexItemRec *r = &lx->items[i];
        if (r->kind != LIME_LEXITEM_OP) continue;
        for (uint32_t k = 0; k < r->text_len; k++) lx->op_cmap[(unsigned char)lx->pool[r->text_off + k]] = 1;
    }
    for (int b = 0; b < 256; b++)
        if (lx->op_cmap[b]) lx->op_cmap[b] = (uint8_t)ncls++;
    lx->op_ncls = ncls;
    lx->op_next = calloc(maxnodes * ncls, sizeof(uint16_t));
    lx->op_code = malloc(maxnodes * sizeof(int32_t));
    if (lx->op_next == NULL || lx->op_code == NULL) return -1;
    for (size_t k = 0; k < maxnodes; k++) lx->op_code[k] = -1;
    uint32_t nn = 1;
    for (uint32_t i = 0; i < lx->nitems; i++) {
        const LimeLexItemRec *r = &lx->items[i];
        if (r->kind != LIME_LEXITEM_OP) continue;
        uint32_t node = 0;
        for (uint32_t k = 0; k < r->text_len; k++) {
            uint16_t *slot = &lx->op_next[node * ncls + lx->op_cmap[(unsigned char)lx->pool[r->text_off + k]]];
            if (*slot == 0) *slot = (uint16_t)nn++;
            node = *slot;
        }
        lx->op_code[node] = r->code;
    }
    lx->op_nnodes = nn;
    return 0;
}

static void attach(ParserSnapshot *snap, LimeLexicon *lx) {
    lime_lexicon_destroy(snap->lexicon);
    snap->lexicon = lx;
}

char *lime_snapshot_set_lexicon(ParserSnapshot *snap, const LimeLexItem *keywords,
                                size_t nkeywords, const LimeLexItem *operators,
                                size_t noperators, unsigned flags) {
    if (snap == NULL) return fmt("lime_snapshot_set_lexicon: snapshot is NULL");
    if ((nkeywords && keywords == NULL) || (noperators && operators == NULL))
        return fmt("lime_snapshot_set_lexicon: NULL item array");
    PendVec pv = { 0 };
    char *err = NULL;
    for (size_t i = 0; i < nkeywords && err == NULL; i++) {
        const char *t = keywords[i].text ? keywords[i].text : "";
        err = pv_add(&pv, snap, LIME_LEXITEM_KW, t, strlen(t), keywords[i].token, flags);
    }
    for (size_t i = 0; i < noperators && err == NULL; i++) {
        const char *t = operators[i].text ? operators[i].text : "";
        err = pv_add(&pv, snap, LIME_LEXITEM_OP, t, strlen(t), operators[i].token, flags);
    }
    LimeLexicon *lx = NULL;
    if (err == NULL) err = build(&pv, flags, &lx);
    pv_free(&pv);
    if (err == NULL) attach(snap, lx);
    return err;
}

/* A lexeme made only of identifier bytes is a keyword; anything else
** (punctuation) is an operator. */
static unsigned lexeme_kind(const char *s) {
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (!(c == '_' || c >= 0x80 || (c >= '0' && c <= '9') || (unsigned)(c | 0x20) - 'a' < 26u))
            return LIME_LEXITEM_OP;
    }
    return LIME_LEXITEM_KW;
}

char *lime_lex_compose(ParserSnapshot *composed, const ParserSnapshot *base,
                       const GrammarModification *mods, uint32_t nmods) {
    if (composed == NULL) return fmt("lime_lex_compose: composed snapshot is NULL");
    if (nmods && mods == NULL) return fmt("lime_lex_compose: mods is NULL");
    const LimeLexicon *bl = base ? base->lexicon : NULL;
    unsigned flags = bl ? bl->flags : 0;
    PendVec pv = { 0 };
    char *err = NULL;

    /* Base items: re-resolve each token by name in the composed
    ** snapshot.  A moved code means the composed grammar renumbered a
    ** base terminal, which would silently change what every base
    ** keyword lexes to (DESIGN invariant 4). */
    for (uint32_t i = 0; bl && i < bl->nitems && err == NULL; i++) {
        const LimeLexItemRec *r = &bl->items[i];
        const char *text = bl->pool + r->text_off, *tok = bl->pool + r->token_off;
        err = pv_add(&pv, composed, r->kind, text, r->text_len, tok, flags);
        if (err == NULL && pv.v[pv.n - 1].code != r->code)
            err = fmt("lexicon: base token %s ('%s') changed code %d -> %d in the composed "
                      "snapshot; base terminal codes must be stable",
                      tok, text, r->code, pv.v[pv.n - 1].code);
    }
    for (uint32_t i = 0; i < nmods && err == NULL; i++) {
        const GrammarModification *m = &mods[i];
        if (m->type != MOD_ADD_TOKEN || m->u.add_token.name == NULL) continue;
        const char *lex = m->u.add_token.lexeme;
        if (lex == NULL || lex[0] == '\0') continue; /* token with no spelling */
        err = pv_add(&pv, composed, lexeme_kind(lex), lex, strlen(lex), m->u.add_token.name,
                     flags);
    }
    LimeLexicon *lx = NULL;
    if (err == NULL && pv.n > 0) err = build(&pv, flags, &lx);
    pv_free(&pv);
    if (err == NULL) attach(composed, lx);
    return err;
}
