/*
** test_json_pbt.c -- property-based test that all three JSON
** allocator modes produce structurally-equal trees.
**
** Target #5 from .agent/skills/hegel/references/c/reference.md:
** "For the JSON example, all three allocator modes (MALLOC /
** MALLOC_NOFREE / ARENA) must produce structurally-equal
** JsonValue trees on the same input.  Property:
**     parse(input, MALLOC) == parse(input, ARENA)
** modulo pointer identity."
**
** Generators draw a JSON document of bounded depth and feed the
** same byte sequence through the parser three times -- once per
** allocator mode -- then walk both trees in lockstep asserting
** the same shape, the same value at every leaf, the same key
** at every object pair.  Pointer identity is explicitly NOT
** asserted (different allocators yield different addresses).
*/

#include "json.h"
#include "json_tokenize.h"
#include "json_grammar.h"
#include "parser.h"
#include "parse_context.h"
#include "snapshot.h"

#include <hegel/hegel.h>
#include <hegel/generators.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void *JsonAlloc(void *(*)(size_t));
extern void  JsonFree(void *, void (*)(void *));
extern void  Json(void *, int, void *, JsonValue **);
extern ParserSnapshot *JsonBuildSnapshot(void);

/* ------------------------------------------------------------------ */
/*  Generate a small JSON document                                     */
/* ------------------------------------------------------------------ */
/*
** Hand-built recursive generator.  Hegel-c does not yet have a
** flat_map combinator that recurses cleanly with depth control,
** so we draw at each step inside the test body and bound depth
** explicitly.  The output is a NUL-terminated buffer the JSON
** scanner can consume.
**
** Caller frees the returned buffer.
*/
typedef struct GenBuf {
    char *data;
    size_t len;
    size_t cap;
} GenBuf;

static void gb_init(GenBuf *b) { b->data = NULL; b->len = b->cap = 0; }

static void gb_putc(GenBuf *b, char c) {
    if (b->len + 2 > b->cap) {
        b->cap = b->cap == 0 ? 64 : b->cap * 2;
        b->data = realloc(b->data, b->cap);
    }
    b->data[b->len++] = c;
    b->data[b->len] = '\0';
}

static void gb_puts(GenBuf *b, const char *s) {
    while (*s) gb_putc(b, *s++);
}

static void gb_free(GenBuf *b) { free(b->data); b->data = NULL; b->len = b->cap = 0; }

/* Forward decl */
static void emit_value(hegel_test_case *tc, GenBuf *b, int depth);

static void emit_string(hegel_test_case *tc, GenBuf *b) {
    /* Bounded ASCII string; no escapes (keeps the comparison
    ** simple and avoids generator/lexer mismatches).  Hegel
    ** picks the length and the characters. */
    int64_t len = hegel_draw_int(tc, hegel_integers(0, 12));
    gb_putc(b, '"');
    for (int64_t i = 0; i < len; i++) {
        int64_t c = hegel_draw_int(tc, hegel_integers('a', 'z'));
        gb_putc(b, (char)c);
    }
    gb_putc(b, '"');
}

static void emit_number(hegel_test_case *tc, GenBuf *b) {
    int64_t v = hegel_draw_int(tc, hegel_integers(-1000, 1000));
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", (long long)v);
    gb_puts(b, buf);
}

static void emit_array(hegel_test_case *tc, GenBuf *b, int depth) {
    int64_t n = hegel_draw_int(tc, hegel_integers(0, 4));
    gb_putc(b, '[');
    for (int64_t i = 0; i < n; i++) {
        if (i > 0) gb_putc(b, ',');
        emit_value(tc, b, depth - 1);
    }
    gb_putc(b, ']');
}

static void emit_object(hegel_test_case *tc, GenBuf *b, int depth) {
    int64_t n = hegel_draw_int(tc, hegel_integers(0, 4));
    gb_putc(b, '{');
    for (int64_t i = 0; i < n; i++) {
        if (i > 0) gb_putc(b, ',');
        emit_string(tc, b);
        gb_putc(b, ':');
        emit_value(tc, b, depth - 1);
    }
    gb_putc(b, '}');
}

static void emit_value(hegel_test_case *tc, GenBuf *b, int depth) {
    /* At max depth, restrict to scalars to avoid runaway
    ** recursion.  Hegel can shrink the depth budget, so failing
    ** test cases collapse to small documents. */
    int64_t kind;
    if (depth <= 0) {
        kind = hegel_draw_int(tc, hegel_integers(0, 4)); /* no obj/arr */
    } else {
        kind = hegel_draw_int(tc, hegel_integers(0, 6));
    }
    switch (kind) {
        case 0: gb_puts(b, "null");  break;
        case 1: gb_puts(b, "true");  break;
        case 2: gb_puts(b, "false"); break;
        case 3: emit_number(tc, b);  break;
        case 4: emit_string(tc, b);  break;
        case 5: emit_array (tc, b, depth); break;
        case 6: emit_object(tc, b, depth); break;
    }
}

/* ------------------------------------------------------------------ */
/*  Parse one buffer with the given allocator mode                     */
/* ------------------------------------------------------------------ */

static JsonValue *parse_with_mode(ParserSnapshot *snap, const char *src, size_t len,
                                  JsonAllocMode mode, JsonArena *arena_out) {
    json_set_alloc_mode(mode);
    if (mode == JSON_ALLOC_ARENA) {
        json_arena_init(arena_out, 1 << 20);
        json_set_arena(arena_out);
    }

    JsonScanner sc;
    json_scanner_init(&sc, src, len);
    void *parser = JsonAlloc(malloc);
    JsonValue *root = NULL;
    int tok;
    void *value;
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
        case JSON_T_NULL:   return 1;
        case JSON_T_BOOL:   return a->b == b->b;
        case JSON_T_NUMBER: return a->n == b->n;
        case JSON_T_STRING:
            if (a->s == NULL && b->s == NULL) return 1;
            if (a->s == NULL || b->s == NULL) return 0;
            return strcmp(a->s, b->s) == 0;
        case JSON_T_ARRAY:
            if (a->a.count != b->a.count) return 0;
            for (size_t i = 0; i < a->a.count; i++)
                if (!eq(a->a.items[i], b->a.items[i])) return 0;
            return 1;
        case JSON_T_OBJECT:
            if (a->o.count != b->o.count) return 0;
            for (size_t i = 0; i < a->o.count; i++)
                if (!eq(a->o.pairs[i], b->o.pairs[i])) return 0;
            return 1;
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

static void prop_allocator_modes_agree(hegel_test_case *tc, void *u) {
    (void)u;

    GenBuf b;
    gb_init(&b);
    emit_value(tc, &b, /*depth=*/3);
    if (b.data == NULL) { hegel_assume(0); return; }

    JsonValue *m = parse_with_mode(g_snap, b.data, b.len, JSON_ALLOC_MALLOC, NULL);
    JsonValue *l = parse_with_mode(g_snap, b.data, b.len, JSON_ALLOC_MALLOC_NOFREE, NULL);
    JsonArena arena;
    JsonValue *a = parse_with_mode(g_snap, b.data, b.len, JSON_ALLOC_ARENA, &arena);

    /* All three allocator modes must produce trees that compare
    ** equal under our structural equality. */
    if (!eq(m, l) || !eq(m, a)) {
        char note[256];
        snprintf(note, sizeof(note), "tree mismatch on input: %.200s", b.data);
        hegel_note(note);
        assert(0 && "allocator mode equivalence violated");
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
    hegel_session *s = hegel_session_new();
    if (s == NULL) {
        fprintf(stderr, "hegel: server unavailable -- skipping PBT\n");
        return 77;
    }

    g_snap = JsonBuildSnapshot();
    if (g_snap == NULL) {
        fprintf(stderr, "JsonBuildSnapshot returned NULL\n");
        hegel_session_free(s);
        return 1;
    }

    hegel_settings settings = HEGEL_DEFAULT_SETTINGS;
    settings.max_examples = 200;

    hegel_results r = hegel_run_test(s, prop_allocator_modes_agree, NULL, &settings);
    printf("prop_allocator_modes_agree: %s (%u valid / %u interesting)\n",
           r.passed ? "PASS" : "FAIL",
           r.valid_test_cases, r.interesting_test_cases);

    int rc = r.passed ? 0 : 1;
    hegel_results_free(&r);
    snapshot_release(g_snap);
    hegel_session_free(s);
    return rc;
}
