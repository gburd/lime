/*
** test_json_pbt.c -- property-based test that all three JSON
** allocator modes produce structurally-equal trees.
**
** Target #5: "For the JSON example, all three allocator modes
** (MALLOC / MALLOC_NOFREE / ARENA) must produce structurally-equal
** JsonValue trees on the same input.  Property:
**     parse(input, MALLOC) == parse(input, ARENA)
** modulo pointer identity."
**
** Generators draw a JSON document of bounded depth and feed the
** same byte sequence through the parser three times -- once per
** allocator mode -- then walk the trees in lockstep asserting
** the same shape, the same value at every leaf, the same key
** at every object pair.  Pointer identity is explicitly NOT
** asserted (different allocators yield different addresses).
**
** Ported from the old gburd/hegel-c socket API to libhegel's
** in-process C ABI; see tests/hegel_pbt.h for the mapping.  The
** property is unchanged.
*/

#include "json.h"
#include "json_tokenize.h"
#include "json_grammar.h"
#include "parser.h"
#include "parse_context.h"
#include "snapshot.h"

#include "hegel_pbt.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* LeakSanitizer suppression -- LOAD-BEARING, do not delete.
**
** This test exercises JSON_ALLOC_MALLOC_NOFREE, an allocator mode
** whose defining behaviour is that it never frees: the property under
** test is that a leaking allocator still produces a structurally
** identical tree to the freeing one.  The leak is the mode, so LSan
** reporting it is a false positive on this binary specifically -- it
** flagged ~4 KB across ~130 objects allocated under json_alloc() and
** aborted the test with SIGABRT under -Db_sanitize=address.
**
** __lsan_is_turned_off() is the documented compile-time opt-out and
** is scoped to this executable, so the rest of the suite keeps full
** leak checking.  ASan's other checks (out-of-bounds, use-after-free,
** double-free) remain active here -- only leak reporting is disabled.
** If this test is ever changed to stop exercising NOFREE mode, delete
** this block so real leaks are caught again. */
#if defined(__SANITIZE_ADDRESS__) || \
    (defined(__has_feature) && __has_feature(address_sanitizer))
int __lsan_is_turned_off(void);
int __lsan_is_turned_off(void) { return 1; }
#endif

extern void *JsonAlloc(void *(*)(size_t));
extern void JsonFree(void *, void (*)(void *));
extern void Json(void *, int, void *, JsonValue **);
extern ParserSnapshot *JsonBuildSnapshot(void);

/* ------------------------------------------------------------------ */
/*  Generate a small JSON document                                     */
/* ------------------------------------------------------------------ */
/*
** Hand-built recursive generator.  libhegel's collection API could
** drive the element counts, but drawing bounded integers directly is
** the smaller change and keeps depth control explicit.  The output
** is a NUL-terminated buffer the JSON scanner can consume.
**
** Caller frees the returned buffer.
*/
typedef struct GenBuf {
    char *data;
    size_t len;
    size_t cap;
} GenBuf;

static void gb_init(GenBuf *b) {
    b->data = NULL;
    b->len = b->cap = 0;
}

static void gb_putc(GenBuf *b, char ch) {
    if (b->len + 2 > b->cap) {
        size_t cap = b->cap == 0 ? 64 : b->cap * 2;
        char *grown = realloc(b->data, cap);
        if (grown == NULL) return; /* OOM: truncate; caller assumes out */
        b->data = grown;
        b->cap = cap;
    }
    b->data[b->len++] = ch;
    b->data[b->len] = '\0';
}

static void gb_puts(GenBuf *b, const char *s) {
    while (*s)
        gb_putc(b, *s++);
}

static void gb_free(GenBuf *b) {
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}

/* Forward decl */
static void emit_value(hpbt_case *c, GenBuf *b, int depth);

static void emit_string(hpbt_case *c, GenBuf *b) {
    /* Bounded ASCII string; no escapes (keeps the comparison
    ** simple and avoids generator/lexer mismatches).  Hegel
    ** picks the length and the characters. */
    int64_t len = hpbt_int(c, 0, 12);
    int64_t i;
    gb_putc(b, '"');
    for (i = 0; i < len && !hpbt_done(c); i++) {
        gb_putc(b, (char)hpbt_int(c, 'a', 'z'));
    }
    gb_putc(b, '"');
}

static void emit_number(hpbt_case *c, GenBuf *b) {
    int64_t v = hpbt_int(c, -1000, 1000);
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", (long long)v);
    gb_puts(b, buf);
}

static void emit_array(hpbt_case *c, GenBuf *b, int depth) {
    int64_t n = hpbt_int(c, 0, 4);
    int64_t i;
    gb_putc(b, '[');
    for (i = 0; i < n && !hpbt_done(c); i++) {
        if (i > 0) gb_putc(b, ',');
        emit_value(c, b, depth - 1);
    }
    gb_putc(b, ']');
}

static void emit_object(hpbt_case *c, GenBuf *b, int depth) {
    int64_t n = hpbt_int(c, 0, 4);
    int64_t i;
    gb_putc(b, '{');
    for (i = 0; i < n && !hpbt_done(c); i++) {
        if (i > 0) gb_putc(b, ',');
        emit_string(c, b);
        gb_putc(b, ':');
        emit_value(c, b, depth - 1);
    }
    gb_putc(b, '}');
}

static void emit_value(hpbt_case *c, GenBuf *b, int depth) {
    /* At max depth, restrict to scalars to avoid runaway
    ** recursion.  Hegel can shrink the drawn kinds, so failing
    ** test cases collapse to small documents. */
    int64_t kind = (depth <= 0) ? hpbt_int(c, 0, 4) /* no obj/arr */
                                : hpbt_int(c, 0, 6);
    switch (kind) {
    case 0:
        gb_puts(b, "null");
        break;
    case 1:
        gb_puts(b, "true");
        break;
    case 2:
        gb_puts(b, "false");
        break;
    case 3:
        emit_number(c, b);
        break;
    case 4:
        emit_string(c, b);
        break;
    case 5:
        emit_array(c, b, depth);
        break;
    case 6:
        emit_object(c, b, depth);
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/*  Parse one buffer with the given allocator mode                     */
/* ------------------------------------------------------------------ */

static JsonValue *parse_with_mode(ParserSnapshot *snap, const char *src, size_t len,
                                  JsonAllocMode mode, JsonArena *arena_out) {
    JsonScanner sc;
    void *parser;
    JsonValue *root = NULL;
    int tok;
    void *value;

    (void)snap;
    json_set_alloc_mode(mode);
    if (mode == JSON_ALLOC_ARENA) {
        json_arena_init(arena_out, 1 << 20);
        json_set_arena(arena_out);
    }

    json_scanner_init(&sc, src, len);
    parser = JsonAlloc(malloc);
    while ((tok = json_scan(&sc, &value)) > 0) {
        Json(parser, tok, value, &root);
    }
    Json(parser, 0, NULL, &root);
    JsonFree(parser, free);

    if (mode == JSON_ALLOC_ARENA) {
        json_set_arena(NULL);
    }
    return root;
}

/* ------------------------------------------------------------------ */
/*  Structural equality                                                */
/* ------------------------------------------------------------------ */
/*
** Walk two JsonValue trees in lockstep.  Pointer identity is NOT
** asserted (different allocators yield different addresses);
** type and content equality is.
*/
static int eq(const JsonValue *a, const JsonValue *b) {
    if (a == NULL || b == NULL) return a == b;
    if (a->type != b->type) return 0;
    switch (a->type) {
    case JSON_T_NULL:
        return 1;
    case JSON_T_BOOL:
        return a->b == b->b;
    case JSON_T_NUMBER:
        return a->n == b->n;
    case JSON_T_STRING:
        if (a->s == NULL && b->s == NULL) return 1;
        if (a->s == NULL || b->s == NULL) return 0;
        return strcmp(a->s, b->s) == 0;
    case JSON_T_ARRAY: {
        size_t i;
        if (a->a.count != b->a.count) return 0;
        for (i = 0; i < a->a.count; i++)
            if (!eq(a->a.items[i], b->a.items[i])) return 0;
        return 1;
    }
    case JSON_T_OBJECT: {
        size_t i;
        if (a->o.count != b->o.count) return 0;
        for (i = 0; i < a->o.count; i++)
            if (!eq(a->o.pairs[i], b->o.pairs[i])) return 0;
        return 1;
    }
    case JSON_T_PAIR:
        if ((a->p.key == NULL) != (b->p.key == NULL)) return 0;
        if (a->p.key && strcmp(a->p.key, b->p.key) != 0) return 0;
        return eq(a->p.val, b->p.val);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Property                                                           */
/* ------------------------------------------------------------------ */

static ParserSnapshot *g_snap;

static void prop_allocator_modes_agree(hpbt_case *c, void *u) {
    (void)u;
    GenBuf b;
    JsonValue *m, *l, *a;
    JsonArena arena;

    gb_init(&b);
    emit_value(c, &b, /*depth=*/3);
    if (b.data == NULL || hpbt_done(c)) {
        /* Nothing generated (or the choice budget ran out mid
        ** document): the partial buffer isn't a document worth
        ** comparing. */
        gb_free(&b);
        hpbt_assume(c, 0);
        return;
    }

    m = parse_with_mode(g_snap, b.data, b.len, JSON_ALLOC_MALLOC, NULL);
    l = parse_with_mode(g_snap, b.data, b.len, JSON_ALLOC_MALLOC_NOFREE, NULL);
    a = parse_with_mode(g_snap, b.data, b.len, JSON_ALLOC_ARENA, &arena);

    /* All three allocator modes must produce trees that compare
    ** equal under our structural equality. */
    if (!eq(m, l)) {
        HPBT_FAIL(c, "MALLOC vs MALLOC_NOFREE tree mismatch on input: %.200s", b.data);
    } else if (!eq(m, a)) {
        HPBT_FAIL(c, "MALLOC vs ARENA tree mismatch on input: %.200s", b.data);
    }

    /* Cleanup.  In MALLOC_NOFREE mode we deliberately leak l;
    ** memory is reclaimed by process exit anyway, and the leak
    ** is the test's whole point.  json_free is a no-op in
    ** ARENA / NOFREE modes; only the MALLOC tree gets freed. */
    json_set_alloc_mode(JSON_ALLOC_MALLOC);
    json_free(m);
    /* l: leaked deliberately */
    /* a: arena destroyed below */
    json_arena_destroy(&arena);

    gb_free(&b);
}

int main(void) {
    const char *ver = hpbt_version();
    int rc;

    if (ver == NULL) {
        fprintf(stderr, "libhegel failed to initialise -- skipping PBT\n");
        return 77; /* meson skip */
    }
    printf("libhegel %s (in-process engine)\n", ver);

    g_snap = JsonBuildSnapshot();
    if (g_snap == NULL) {
        fprintf(stderr, "JsonBuildSnapshot returned NULL\n");
        return 1;
    }

    rc = hpbt_run("prop_allocator_modes_agree", prop_allocator_modes_agree, NULL, 200);

    snapshot_release(g_snap);
    return rc;
}
