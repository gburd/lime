/*
** src/lex/lex_emit.c -- emit C source from a compiled lexer.
**
** Layout choices:
**   - Each state's DFA emits as `<prefix>_<STATE>_trans[N][256]`,
**     `<prefix>_<STATE>_accept[N]` (rule id or -1), and
**     `<prefix>_<STATE>_n_states`.
**   - One Foo_match() function dispatches state -> DFA tables
**     via a switch.  Hot loop is inlined per state for cache
**     locality (small total code; if a grammar has 50+ states
**     this becomes wasteful and we'd refactor to a single
**     parameterised loop).
**   - Rule-id constants and string array cover *every* rule in
**     the spec, including EOF rules (which don't appear in the
**     DFA but are needed for diagnostics and future LexFeedEOF
**     dispatch).
*/

#include "lex_emit.h"
#include "lex_ast.h"
#include "lex_compile.h"
#include "lex_dfa.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* v0.8.10: --lex-vectorize default ON.  Set by lime.c after the
** option parser runs.  When non-zero, the C emit ships the
** multiversion-at-tokenize SIMD architecture (mirror of the Rust
** --rustlex-simd emit): per-state fast-path scan helpers in AVX2 /
** NEON / scalar variants and a multiversion <prefix>_match() that
** runtime-dispatches via __builtin_cpu_supports.  When zero, the
** legacy single-function scalar emit is used.
**
** Defined here (rather than in lex_main.c) so test programs that
** link against liblime_lex_compiler.a and only reference
** lime_lex_emit_c can resolve the symbol without dragging in
** emit_rust_lex.c.o and its further unresolved externs. */
int g_lime_lex_vectorize_flag = 1;


/* ============================================================
** Identifier helpers
** ============================================================ */

static const char *eff_prefix(const char *name_prefix) {
    if (!name_prefix || !*name_prefix) return "Lex";
    return name_prefix;
}

/* Allocate an UPPER_CASE copy of s (e.g. "INITIAL" -> "INITIAL",
** "boot" -> "BOOT").  Caller frees. */
static char *upper_dup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char *out = malloc(n + 1);
    if (!out) return NULL;
    for (size_t i = 0; i < n; i++) {
        out[i] = (char)toupper((unsigned char)s[i]);
    }
    out[n] = '\0';
    return out;
}

/* "Foo" -> "FOO" (caller frees). */
static char *upper_prefix(const char *name_prefix) {
    return upper_dup(eff_prefix(name_prefix));
}

/* Replace any non-identifier characters with '_' to make a
** safe C identifier suffix (e.g. "kw-if" -> "kw_if"). */
static void sanitise_ident(char *s) {
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (!(isalnum(c) || c == '_')) *s = '_';
    }
}

/* ============================================================
** %literal_buffer helpers (M3.7)
** ============================================================ */

/* Effective C element type for a buffer (default "char"). */
static const char *eff_buf_type(const LimeLexLiteralBuffer *lb) {
    if (lb && lb->element_type && *lb->element_type) return lb->element_type;
    return "char";
}

/* Effective initial capacity (default 64; 0 / negative -> 64). */
static int eff_buf_initial(const LimeLexLiteralBuffer *lb) {
    if (lb && lb->initial_capacity > 0) return lb->initial_capacity;
    return 64;
}

/* Parse the grow_policy string ("*N" or "+N"; default "*2"
** when missing or malformed) and emit one C statement that
** advances `new_cap` for one iteration of the grow loop.  The
** caller wraps it in a no-progress / overflow guard. */
static void emit_buf_grow_step(const LimeLexLiteralBuffer *lb, FILE *out) {
    char op = '*';
    long n = 2;
    const char *s = lb ? lb->grow_policy : NULL;
    if (s && (*s == '*' || *s == '+')) {
        char *end = NULL;
        long parsed = strtol(s + 1, &end, 10);
        if (end != s + 1 && parsed > 0) {
            op = *s;
            n = parsed;
        }
    }
    if (op == '*') {
        fprintf(out, "            new_cap = new_cap * %ld;\n", n);
    } else {
        fprintf(out, "            new_cap = new_cap + %ld;\n", n);
    }
}

/* True when at least one %literal_buffer was declared. */
static int has_literal_buffers(const LimeLexSpec *spec) {
    return spec && spec->literal_buffers != NULL;
}

/* Emit `#error` guards for any buffer that is missing a
** required allocator function.  The directive declares intent
** to use the LEX_BUF_* macros, which is impossible without
** matched alloc/realloc/free entries.  We surface this at
** generated-code-compile time rather than silently emit
** unresolved references.  Returns 0 on success (always). */
static void emit_buf_alloc_guards(const LimeLexSpec *spec, FILE *out) {
    if (!has_literal_buffers(spec)) return;
    for (const LimeLexLiteralBuffer *lb = spec->literal_buffers; lb; lb = lb->next) {
        if (!lb->alloc_fn) {
            fprintf(out, "#error \"%%literal_buffer %s: missing 'alloc' function\"\n", lb->name);
        }
        if (!lb->realloc_fn) {
            fprintf(out, "#error \"%%literal_buffer %s: missing 'realloc' function\"\n", lb->name);
        }
        if (!lb->free_fn) {
            fprintf(out, "#error \"%%literal_buffer %s: missing 'free' function\"\n", lb->name);
        }
    }
}

/* Emit per-buffer struct fields (buf/len/cap) inside FooLexer. */
static void emit_buf_struct_fields(const LimeLexSpec *spec, FILE *out) {
    if (!has_literal_buffers(spec)) return;
    fprintf(out, "    /* M3.7: %%literal_buffer storage, one block per buffer. */\n");
    for (const LimeLexLiteralBuffer *lb = spec->literal_buffers; lb; lb = lb->next) {
        fprintf(out,
                "    %s    *%s_buf;\n"
                "    size_t  %s_len;\n"
                "    size_t  %s_cap;\n",
                eff_buf_type(lb), lb->name, lb->name, lb->name);
    }
}

/* Emit LexAlloc init lines (zero buf/len/cap for each buffer). */
static void emit_buf_alloc_init(const LimeLexSpec *spec, FILE *out) {
    if (!has_literal_buffers(spec)) return;
    for (const LimeLexLiteralBuffer *lb = spec->literal_buffers; lb; lb = lb->next) {
        fprintf(out,
                "    yyl->%s_buf = 0;\n"
                "    yyl->%s_len = 0;\n"
                "    yyl->%s_cap = 0;\n",
                lb->name, lb->name, lb->name);
    }
}

/* Emit per-buffer cleanup inside LexFree.  Each buffer is
** freed via the user-declared `<free>` function (NOT freeProc)
** because allocation went through `<alloc>`/`<realloc>`. */
static void emit_buf_free_walk(const LimeLexSpec *spec, FILE *out) {
    if (!has_literal_buffers(spec)) return;
    for (const LimeLexLiteralBuffer *lb = spec->literal_buffers; lb; lb = lb->next) {
        if (!lb->free_fn) continue; /* #error guard above */
        fprintf(out,
                "        if (yyl->%s_buf) {\n"
                "            %s(yyl->%s_buf);\n"
                "            yyl->%s_buf = 0;\n"
                "            yyl->%s_len = 0;\n"
                "            yyl->%s_cap = 0;\n"
                "        }\n",
                lb->name, lb->free_fn, lb->name, lb->name, lb->name, lb->name);
    }
}

/* Emit the per-buffer static helpers: grow + take.  These
** keep the LEX_BUF_* macros small so action bodies don't bloat
** the FeedBytes function. */
static void emit_buf_helpers(const LimeLexSpec *spec, const char *prefix, FILE *out) {
    if (!has_literal_buffers(spec)) return;
    fprintf(out, "/* ===== M3.7: per-buffer grow / take helpers ===== */\n");
    for (const LimeLexLiteralBuffer *lb = spec->literal_buffers; lb; lb = lb->next) {
        const char *type = eff_buf_type(lb);
        const char *alloc = lb->alloc_fn ? lb->alloc_fn : "/*missing-alloc*/";
        const char *realloc_fn = lb->realloc_fn ? lb->realloc_fn : "/*missing-realloc*/";
        int initial = eff_buf_initial(lb);

        /* grow(): ensure capacity >= need; returns 0 ok / -1 fail. */
        fprintf(out,
                "static int %s_buf_%s_grow(%sLexer *lex, size_t need) {\n"
                "    if (lex->%s_cap >= need) return 0;\n"
                "    size_t new_cap = lex->%s_cap;\n"
                "    if (new_cap == 0) new_cap = %d;\n"
                "    while (new_cap < need) {\n"
                "        size_t prev = new_cap;\n",
                prefix, lb->name, prefix, lb->name, lb->name, initial);
        emit_buf_grow_step(lb, out);
        fprintf(out,
                "        if (new_cap <= prev) { new_cap = need; break; }\n"
                "    }\n"
                "    void *p = lex->%s_buf\n"
                "        ? %s(lex->%s_buf, new_cap * sizeof(%s))\n"
                "        : %s(new_cap * sizeof(%s));\n"
                "    if (!p) return -1;\n"
                "    lex->%s_buf = (%s*)p;\n"
                "    lex->%s_cap = new_cap;\n"
                "    return 0;\n"
                "}\n",
                lb->name, realloc_fn, lb->name, type, alloc, type, lb->name, type, lb->name);

        /* take(): grow for trailing NUL, NUL-terminate, transfer
        ** ownership to caller (returned pointer), reset state. */
        fprintf(out,
                "static %s *%s_buf_%s_take(%sLexer *lex) {\n"
                "    if (%s_buf_%s_grow(lex, lex->%s_len + 1) != 0) {\n"
                "        lex->err_msg = \"literal buffer alloc failed\";\n"
                "        return 0;\n"
                "    }\n"
                "    lex->%s_buf[lex->%s_len] = 0;\n"
                "    %s *p = lex->%s_buf;\n"
                "    lex->%s_buf = 0;\n"
                "    lex->%s_len = 0;\n"
                "    lex->%s_cap = 0;\n"
                "    return p;\n"
                "}\n\n",
                type, prefix, lb->name, prefix, prefix, lb->name, lb->name, lb->name, lb->name,
                type, lb->name, lb->name, lb->name, lb->name);
    }
}

/* ============================================================
** Action-body emission helpers (M3.4)
** ============================================================ */

typedef struct {
    const char **actions;
    int *is_eof;
    int n;
    int cap;
    int oom;
} ActionVec;

static int action_visit(const LimeLexRule *r, void *user) {
    ActionVec *v = (ActionVec *)user;
    if (v->n == v->cap) {
        int nc = v->cap ? v->cap * 2 : 16;
        const char **na = realloc(v->actions, (size_t)nc * sizeof(*na));
        int *ne = realloc(v->is_eof, (size_t)nc * sizeof(*ne));
        if (!na || !ne) {
            free(na ? na : v->actions);
            free(ne ? ne : v->is_eof);
            v->actions = NULL;
            v->is_eof = NULL;
            v->oom = 1;
            return -1;
        }
        v->actions = na;
        v->is_eof = ne;
        v->cap = nc;
    }
    v->actions[v->n] = r->action ? r->action : "";
    v->is_eof[v->n] = r->is_eof;
    v->n++;
    return 0;
}

/* Walk the spec's rules in compile order (M4.1: subject to
** %lexer_include filtering) and populate parallel action /
** is_eof arrays.  *actions_out[i] points into the spec; do NOT
** free the strings -- only the arrays.  Returns 0 on success,
** -1 on alloc failure or count mismatch. */
static int collect_rule_actions(const LimeLexSpec *spec, int expected_n, const char ***actions_out,
                                int **is_eof_out) {
    *actions_out = NULL;
    *is_eof_out = NULL;
    if (!spec) return -1;
    ActionVec v = { 0 };
    if (lime_lex_walk_rules(spec, action_visit, &v) != 0) {
        free(v.actions);
        free(v.is_eof);
        return -1;
    }
    if (v.oom || v.n != expected_n) {
        free(v.actions);
        free(v.is_eof);
        return -1;
    }
    *actions_out = v.actions;
    *is_eof_out = v.is_eof;
    return 0;
}

/* ============================================================
** Rule-name collection
** ============================================================ */

typedef struct {
    char **names;
    int n;
    int cap;
    int oom;
} NameVec;

static int name_visit(const LimeLexRule *r, void *user) {
    NameVec *v = (NameVec *)user;
    if (v->n == v->cap) {
        int nc = v->cap ? v->cap * 2 : 16;
        char **nn = realloc(v->names, (size_t)nc * sizeof(*nn));
        if (!nn) {
            v->oom = 1;
            return -1;
        }
        v->names = nn;
        v->cap = nc;
    }
    v->names[v->n] = strdup(r->name ? r->name : "anon");
    if (!v->names[v->n]) {
        v->oom = 1;
        return -1;
    }
    sanitise_ident(v->names[v->n]);
    v->n++;
    return 0;
}

int lime_lex_collect_rule_names(const LimeLexSpec *spec, char ***names_out, int *n_rules_out) {
    if (!spec || !names_out || !n_rules_out) return -1;
    NameVec v = { 0 };
    if (lime_lex_walk_rules(spec, name_visit, &v) != 0) {
        for (int k = 0; k < v.n; k++)
            free(v.names[k]);
        free(v.names);
        return -1;
    }
    if (v.n == 0) {
        free(v.names);
        *names_out = NULL;
        *n_rules_out = 0;
        return 0;
    }
    *names_out = v.names;
    *n_rules_out = v.n;
    return 0;
}

/* ============================================================
** Header emission
** ============================================================ */

/* M4.3: per-rule test entry-point emission.  Walks the spec's
** rule list (including ruleset rules subject to %lexer_include
** filtering, via lime_lex_walk_rules) and emits per-non-EOF
** wrappers.  EOF rules are skipped because they are not
** dispatchable through Foo_match -- they fire from
** LexFeedBytes' auto-pop branch on a buffer-stack frame's EOF.
**
** State choice: the wrapper invokes Foo_match in the rule's
** FIRST state qualifier (or INITIAL if unqualified).  This is
** the simplest stable contract; multi-state rules are testable
** by hand against the appropriate constant via Foo_match
** directly. */
typedef struct {
    FILE *out;
    const char *prefix; /* "Foo" */
    char *PREFIX;       /* "FOO" -- borrowed, do not free */
    int emit_h;         /* 1 = decls only, 0 = bodies */
    int compiled_state_count;
    const LimeLexCompiledState *compiled_states;
    int err;
} TestWrapperCtx;

/* Look up a state name in the compiled state list.  Returns
** the state's index (suitable for FOO_STATE_<NAME> selection)
** or -1 if the name is not known.  We compare on the original
** state name, not the upper-cased variant, since c->states[]
** stores the source-form name. */
static int compiled_state_index(const TestWrapperCtx *ctx, const char *name) {
    for (int i = 0; i < ctx->compiled_state_count; i++) {
        if (strcmp(ctx->compiled_states[i].state_name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static int test_wrapper_visit(const LimeLexRule *r, void *user) {
    TestWrapperCtx *ctx = (TestWrapperCtx *)user;
    if (r->is_eof) return 0;
    if (!r->name) return 0;

    char *suffix = strdup(r->name);
    if (!suffix) {
        ctx->err = 1;
        return -1;
    }
    sanitise_ident(suffix);

    char *RULE_UPPER = upper_dup(suffix);
    if (!RULE_UPPER) {
        free(suffix);
        ctx->err = 1;
        return -1;
    }

    /* Pick the state for the wrapper.  Unqualified -> INITIAL.
    ** Qualified -> first state.  Defensive: if the named state
    ** is unknown (malformed grammar) skip the wrapper -- emitting
    ** a reference to FOO_STATE_<UNKNOWN> would break compilation. */
    const char *state_name = "INITIAL";
    if (r->n_states > 0 && r->states && r->states[0]) {
        state_name = r->states[0];
        if (compiled_state_index(ctx, state_name) < 0) {
            free(suffix);
            free(RULE_UPPER);
            return 0;
        }
    }
    char *STATE_UPPER = upper_dup(state_name);
    if (!STATE_UPPER) {
        free(suffix);
        free(RULE_UPPER);
        ctx->err = 1;
        return -1;
    }

    if (ctx->emit_h) {
        fprintf(ctx->out,
                "%sLexResult %s_test_rule_%s(const char *input, size_t n,\n"
                "                              size_t *out_consumed);\n",
                ctx->prefix, ctx->prefix, suffix);
    } else {
        fprintf(ctx->out,
                "%sLexResult %s_test_rule_%s(const char *input, size_t n,\n"
                "                              size_t *out_consumed) {\n"
                "    int rule = -1;\n"
                "    size_t consumed = 0;\n"
                "    int ok = %s_match(%s_STATE_%s, input, n,\n"
                "                      &rule, &consumed);\n"
                "    if (!ok || rule != %s_RULE_%s) return %s_LEX_ERROR;\n"
                "    if (out_consumed) *out_consumed = consumed;\n"
                "    return %s_LEX_OK;\n"
                "}\n\n",
                ctx->prefix, ctx->prefix, suffix, ctx->prefix, ctx->PREFIX, STATE_UPPER,
                ctx->PREFIX, RULE_UPPER, ctx->PREFIX, ctx->PREFIX);
    }
    free(suffix);
    free(RULE_UPPER);
    free(STATE_UPPER);
    return 0;
}

static int emit_test_wrappers(const LimeLexCompiled *c, const LimeLexSpec *spec, const char *prefix,
                              char *PREFIX, int emit_h, FILE *out) {
    if (!spec) return 0;
    TestWrapperCtx ctx;
    ctx.out = out;
    ctx.prefix = prefix;
    ctx.PREFIX = PREFIX;
    ctx.emit_h = emit_h;
    ctx.compiled_state_count = c ? c->n_states : 0;
    ctx.compiled_states = c ? c->states : NULL;
    ctx.err = 0;
    (void)lime_lex_walk_rules(spec, test_wrapper_visit, &ctx);
    return ctx.err ? -1 : 0;
}

int lime_lex_emit_h(const LimeLexCompiled *c, const LimeLexSpec *spec, const char *name_prefix,
                    const char *const *rule_names, int n_rules, FILE *out) {
    if (!c || !out) return -1;

    const char *prefix = eff_prefix(name_prefix);
    char *PREFIX = upper_prefix(name_prefix);
    if (!PREFIX) return -1;

    fprintf(out, "/* Generated by lime -X.  DO NOT EDIT. */\n");
    fprintf(out, "#ifndef %s_LEX_H\n", PREFIX);
    fprintf(out, "#define %s_LEX_H\n\n", PREFIX);
    fprintf(out, "#include <stddef.h>\n\n");

    /* State constants. */
    fprintf(out, "/* Lexer state constants. */\n");
    for (int i = 0; i < c->n_states; i++) {
        char *upper_state = upper_dup(c->states[i].state_name);
        if (!upper_state) {
            free(PREFIX);
            return -1;
        }
        fprintf(out, "#define %s_STATE_%-20s %d\n", PREFIX, upper_state, i);
        free(upper_state);
    }
    fprintf(out, "\n");

    /* Rule-id constants. */
    if (n_rules > 0) {
        fprintf(out, "/* Rule constants (declaration order). */\n");
        fprintf(out, "enum {\n");
        for (int i = 0; i < n_rules; i++) {
            char *upper_rule = upper_dup(rule_names[i]);
            if (!upper_rule) {
                free(PREFIX);
                return -1;
            }
            fprintf(out, "    %s_RULE_%-20s = %d%s\n", PREFIX, upper_rule, i,
                    i + 1 < n_rules ? "," : "");
            free(upper_rule);
        }
        fprintf(out, "};\n\n");
    }

    /* Rule names. */
    fprintf(out, "extern const char *const %sRuleNames[];\n\n", prefix);

    /* Match function. */
    fprintf(out, "/* Match the longest prefix of bytes[0..n) starting in `state`.\n");
    fprintf(out, "** On match: returns 1; *out_rule = matched rule id;\n");
    fprintf(out, "** *out_consumed = number of bytes consumed.\n");
    fprintf(out, "** On no match: returns 0; *out_rule and *out_consumed\n");
    fprintf(out, "** are not modified. */\n");
    fprintf(out, "int %s_match(int state, const char *bytes, size_t n,\n", prefix);
    fprintf(out, "             int *out_rule, size_t *out_consumed);\n\n");

    /* M3.3: push-driven runtime API (extended in M3.5 with the
    ** include buffer stack and pushback). */
    fprintf(out, "/* ===== Push-driven runtime API (M3.3 + M3.5) ===== */\n\n");
    fprintf(out,
            "/* Maximum nesting depth of LexInclude calls (counting the\n"
            "** initial LexFeedBytes buffer as the bottom frame).  64 is\n"
            "** well above any real-world include nesting; if exceeded,\n"
            "** LexInclude returns LEX_ERROR rather than overflowing. */\n"
            "#define %s_LEX_MAX_INCLUDE_DEPTH 64\n\n",
            PREFIX);
    fprintf(out, "typedef struct %sLexer %sLexer;\n\n", prefix, prefix);
    fprintf(out,
            "typedef enum {\n"
            "    %s_LEX_OK = 0,\n"
            "    %s_LEX_ERROR = 1\n"
            "} %sLexResult;\n\n",
            PREFIX, PREFIX, prefix);
    fprintf(out,
            "/* Emit callback: invoked once per recognised token.  user is the\n"
            "** opaque pointer the caller passed to LexFeedBytes; rule is the\n"
            "** matched rule id (one of %s_RULE_*); text/len point into the\n"
            "** caller's input buffer (NOT copied -- valid until the buffer\n"
            "** is freed). */\n"
            "typedef void (*%sEmitFn)(void *user, int rule,\n"
            "                         const char *text, size_t len);\n\n",
            PREFIX, prefix);
    fprintf(out,
            "%sLexer    *%sLexAlloc(void *(*mallocProc)(size_t));\n"
            "void          %sLexFree(%sLexer *yyl, void (*freeProc)(void *));\n",
            prefix, prefix, prefix, prefix);
    /* P0-NEW-12: %lexer_extra_argument threads a user-declared
    ** parameter through LexFeedBytes; the user's name is in scope
    ** inside every action body via standard C parameter binding. */
    const char *extra_arg = (spec && spec->extra_argument) ? spec->extra_argument : 0;
    /* If the type begins with `struct <tag>` or `union <tag>`, emit a
    ** file-scope forward declaration before the prototype.  Without
    ** it, a tag first mentioned inside the parameter list would have
    ** prototype scope, conflicting with the file-scope tag the user
    ** defines (typically inside %include) at the function-definition
    ** site.  Plain typedef / pointer-to-typedef parameters do not
    ** need this and pass through untouched. */
    if (extra_arg) {
        const char *p = extra_arg;
        while (*p == ' ' || *p == '\t' || *p == '\n')
            p++;
        const char *kw = 0;
        size_t kw_len = 0;
        if (strncmp(p, "struct", 6) == 0 && (p[6] == ' ' || p[6] == '\t')) {
            kw = "struct";
            kw_len = 6;
        } else if (strncmp(p, "union", 5) == 0 && (p[5] == ' ' || p[5] == '\t')) {
            kw = "union";
            kw_len = 5;
        }
        if (kw) {
            const char *q = p + kw_len;
            while (*q == ' ' || *q == '\t')
                q++;
            const char *tag_start = q;
            while ((*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z') ||
                   (*q >= '0' && *q <= '9') || *q == '_')
                q++;
            if (q > tag_start) {
                fprintf(out, "%s %.*s;\n\n", kw, (int)(q - tag_start), tag_start);
            }
        }
    }
    if (extra_arg) {
        fprintf(out,
                "%sLexResult  %sLexFeedBytes(%sLexer *yyl,\n"
                "                              const char *bytes, size_t n,\n"
                "                              %sEmitFn emit, void *user,\n"
                "                              %s);\n",
                prefix, prefix, prefix, prefix, extra_arg);
    } else {
        fprintf(out,
                "%sLexResult  %sLexFeedBytes(%sLexer *yyl,\n"
                "                              const char *bytes, size_t n,\n"
                "                              %sEmitFn emit, void *user);\n",
                prefix, prefix, prefix, prefix);
    }
    fprintf(out,
            "%sLexResult  %sLexFeedEOF(%sLexer *yyl,\n"
            "                            %sEmitFn emit, void *user);\n"
            "int           %sLexCurrentState(const %sLexer *yyl);\n"
            "void          %sLexSetState(%sLexer *yyl, int state);\n"
            "const char   *%sLexErrorMessage(const %sLexer *yyl);\n\n",
            prefix, prefix, prefix, prefix, prefix, prefix, prefix, prefix, prefix, prefix);

    fprintf(out,
            "/* Push a new input source onto the lexer's buffer stack.\n"
            "** The lexer continues from the new source until it hits EOF,\n"
            "** then automatically resumes the parent buffer.  The supplied\n"
            "** bytes are NOT copied -- the caller retains ownership and\n"
            "** must keep the buffer alive until the corresponding pop\n"
            "** (observable via LexIncludeDepth).  Typical use: from inside\n"
            "** an emit callback that just recognised an INCLUDE directive.\n"
            "** Returns LEX_ERROR if the lexer is NULL or the maximum\n"
            "** include depth has been reached. */\n"
            "%sLexResult  %sLexInclude(%sLexer *yyl,\n"
            "                            const char *bytes, size_t n);\n\n",
            prefix, prefix, prefix);
    fprintf(out,
            "/* Un-consume the last n bytes from the top-of-stack buffer.\n"
            "** Equivalent to flex's yyless(yyleng - n) when called outside\n"
            "** an action body.  Bounds-checked against the available\n"
            "** buffered prefix in the current frame: returns LEX_ERROR if\n"
            "** n exceeds the bytes consumed so far in that frame, or the\n"
            "** buffer stack is empty. */\n"
            "%sLexResult  %sLexPushback(%sLexer *yyl, size_t n);\n\n",
            prefix, prefix, prefix);
    fprintf(out,
            "/* Number of LexInclude levels currently active.  0 when no\n"
            "** include is on the stack (i.e. the lexer is processing the\n"
            "** original LexFeedBytes buffer or is idle). */\n"
            "int           %sLexIncludeDepth(const %sLexer *yyl);\n\n",
            prefix, prefix);

    /* M4.3: per-rule test entry-point declarations. */
    if (spec) {
        fprintf(out,
                "/* ===== Per-rule test entry points (M4.3) =====\n"
                "** One thin wrapper per non-EOF rule, exposing isolated\n"
                "** rule testing without spinning up the full push-driven\n"
                "** runtime.  Each wrapper invokes %s_match in the rule's\n"
                "** first declared state qualifier (or %s_STATE_INITIAL\n"
                "** for unqualified rules), and returns %s_LEX_OK only\n"
                "** when that specific rule matches.  *out_consumed is\n"
                "** written only on success; passing NULL is permitted. */\n",
                prefix, PREFIX, PREFIX);
        if (emit_test_wrappers(c, spec, prefix, PREFIX, 1, out) != 0) {
            free(PREFIX);
            return -1;
        }
        fprintf(out, "\n");
    }

    fprintf(out, "#endif /* %s_LEX_H */\n", PREFIX);
    free(PREFIX);
    return ferror(out) ? -1 : 0;
}

/* ============================================================
** Source emission
** ============================================================ */

/* Emit a single state's DFA tables as C arrays. */
static void emit_state_tables(const LimeLexCompiledState *cs, int compiled_state_index,
                              const char *prefix, FILE *out) {
    char *upper_state = upper_dup(cs->state_name);
    int ns = cs->dfa ? cs->dfa->n_states : 0;
    fprintf(out, "/* DFA tables for state %s (%d states, %d rules) */\n", cs->state_name, ns,
            cs->n_rules);
    if (ns == 0 || !cs->dfa) {
        fprintf(out, "static const int %s_dfa_%s_start = 0;\n\n", prefix, upper_state);
        free(upper_state);
        return;
    }
    fprintf(out, "static const int %s_dfa_%s_start = %d;\n", prefix, upper_state, cs->dfa->start);

    /* trans[N][256] */
    fprintf(out, "static const short %s_dfa_%s_trans[%d][256] = {\n", prefix, upper_state, ns);
    for (int s = 0; s < ns; s++) {
        fprintf(out, "    /* state %d */ {", s);
        for (int b = 0; b < 256; b++) {
            if (b % 16 == 0) fprintf(out, "\n        ");
            fprintf(out, "%4d%s", cs->dfa->states[s].trans[b], (b + 1 < 256) ? "," : "");
        }
        fprintf(out, "\n    }%s\n", (s + 1 < ns) ? "," : "");
    }
    fprintf(out, "};\n");

    /* accept[N] = global rule id or -1. */
    fprintf(out, "static const short %s_dfa_%s_accept[%d] = {", prefix, upper_state, ns);
    for (int s = 0; s < ns; s++) {
        if (s % 12 == 0) fprintf(out, "\n    ");
        int local_rule = cs->dfa->states[s].is_accept ? cs->dfa->states[s].accept_rule : -1;
        int global_rule = -1;
        if (local_rule >= 0 && local_rule < cs->n_rules) {
            global_rule = cs->rule_indices[local_rule];
        }
        fprintf(out, "%4d%s", global_rule, (s + 1 < ns) ? "," : "");
    }
    fprintf(out, "\n};\n\n");

    free(upper_state);
    (void)compiled_state_index; /* reserved for future state-id use */
}

/* ============================================================
** v0.8.10: SIMD fast-path emission (--lex-vectorize, default ON)
**
** Mirrors src/lex/emit_rust_lex.c's emit_simd_helpers chain:
**
**   1. Classify each DFA state for fast-path candidacy.  A
**      state qualifies (Pattern A in the Rust emit) when:
**        - >= 240 of 256 bytes self-loop (i.e. stay in the
**          state); typical of string-body / comment-body /
**          long-identifier scans.
**        - <= 3 distinct exit bytes lead to a different state
**          (n_exit in [1,3]).
**
**   2. For each candidate, emit three always_inline helpers:
**         <prefix>_scan_<NAME>_<S>_avx2(p, n, pos)   (x86_64)
**         <prefix>_scan_<NAME>_<S>_neon(p, n, pos)   (aarch64)
**         <prefix>_scan_<NAME>_<S>_scalar(p, n, pos) (always)
**      Each takes byte literals baked into its body.  AVX2 uses
**      _mm256_loadu_si256 / _mm256_cmpeq_epi8 / _mm256_movemask_
**      epi8.  NEON uses vld1q_u8 / vceqq_u8 / vshrn_n_u16.  All
**      return the byte index of the first exit byte (or n if
**      none).
**
**   3. Per-state-group dispatch helpers <prefix>_fast_path_<NAME>_
**      <kind>(dfa_state, p, n, pos) switch on dfa_state and call
**      the per-state scanner.  Three kinds: avx2 / neon / scalar.
**
**   4. <prefix>_match_<kind>(state, bytes, n, ...) is the per-kind
**      version of the public match function.  It is an exact copy
**      of the legacy match body PLUS a fast-path call inserted at
**      the top of every iteration of the per-byte loop.
**
**   5. <prefix>_match() is the public dispatcher:
**         x86_64 + gcc/clang: __builtin_cpu_supports("avx2") ?
**             match_avx2 : match_scalar
**         aarch64: match_neon (NEON is mandatory on aarch64)
**         everything else: match_scalar
**
**      The detection result is cached in a function-static int.
**
** Why this beats stdlib auto-vectorisation:
**   __attribute__((target("avx2"))) is a hard inlining barrier
**   in gcc/clang -- a function carrying it cannot be inlined into
**   a caller that doesn't.  By pushing the AVX2 attribute up to
**   match_avx2 (the entire match body), every always_inline
**   helper down the chain inherits the AVX2 target and inlines
**   without crossing a target_feature barrier.  Verified via
**   `objdump -d` -- vmovdqu/vpcmpeqb/vpmovmskb appear inline in
**   match_avx2's body.
** ============================================================ */

/* Classify one DFA state.  Returns 1 if the state is a Pattern A
** fast-path candidate, 0 otherwise.  On success, fills exit_set
** (up to 3 entries) and *n_exit. */
static int classify_fast_path_state(const LimeLexCompiledState *cs, int s,
                                    int exit_set[3], int *n_exit) {
    if (!cs || !cs->dfa) return 0;
    if (s < 0 || s >= cs->dfa->n_states) return 0;
    const int *tr = cs->dfa->states[s].trans;
    int self_count = 0;
    int exit[8];
    int ne = 0;
    int too_many = 0;
    for (int b = 0; b < 256; b++) {
        int t = tr[b];
        if (t == s) {
            self_count++;
        } else if (t >= 0) {
            int found = 0;
            for (int i = 0; i < ne; i++) {
                if (exit[i] == b) {
                    found = 1;
                    break;
                }
            }
            if (!found) {
                if (ne >= 8) {
                    too_many = 1;
                } else {
                    exit[ne++] = b;
                }
            }
        }
    }
    /* Same threshold as Rust emit: >= 240 self-loop, [1,3] exits.
    ** Rust permits up to 8-byte exit sets via memchr_iter; the C
    ** emit caps at 3 to keep the AVX2 cmpeq-or chain short and the
    ** scalar tail's branch-predicate compact.  Grammars with
    ** larger exit sets fall through to the unified DFA per-byte
    ** loop, which is still correct (just no SIMD acceleration). */
    if (too_many) return 0;
    if (ne < 1 || ne > 3) return 0;
    if (self_count < 240) return 0;
    *n_exit = ne;
    for (int i = 0; i < ne; i++) exit_set[i] = exit[i];
    return 1;
}

/* Emit one per-state scanner body for the given kind.  `kind` is
** "avx2", "neon", or "scalar".  The emitted function is
** static inline __attribute__((always_inline)).  AVX2 also carries
** target("avx2").  The caller is responsible for the surrounding
** #ifdef __x86_64__ / __aarch64__ guard.  byte literals for the
** exit set are baked into the body. */
static void emit_scan_helper(FILE *out, const char *prefix, const char *upper_state, int s,
                             int exit_set[3], int n_exit, const char *kind) {
    int is_avx2 = strcmp(kind, "avx2") == 0;
    int is_neon = strcmp(kind, "neon") == 0;

    if (is_avx2) {
        /* AVX2 path is gcc/clang-only (already #ifdef-guarded by the
        ** caller).  target("avx2") propagates through always_inline. */
        fprintf(out, "__attribute__((target(\"avx2\"), always_inline))\n");
        fprintf(out, "static inline size_t %s_scan_%s_%d_avx2(\n", prefix, upper_state, s);
    } else if (is_neon) {
        fprintf(out, "static LIME_LEX_ALWAYS_INLINE size_t %s_scan_%s_%d_neon(\n",
                prefix, upper_state, s);
    } else {
        fprintf(out, "static LIME_LEX_ALWAYS_INLINE size_t %s_scan_%s_%d_scalar(\n",
                prefix, upper_state, s);
    }
    fprintf(out, "        const unsigned char *p, size_t pos, size_t n) {\n");

    /* Bake the exit byte literals into the body. */
    if (is_avx2) {
        for (int i = 0; i < n_exit; i++) {
            fprintf(out, "    const __m256i v%d = _mm256_set1_epi8((char)%d);\n", i + 1,
                    exit_set[i]);
        }
        fprintf(out, "    while (pos + 32 <= n) {\n");
        fprintf(out, "        __m256i chunk = _mm256_loadu_si256(\n");
        fprintf(out, "            (const __m256i*)(p + pos));\n");
        if (n_exit == 1) {
            fprintf(out, "        __m256i any = _mm256_cmpeq_epi8(chunk, v1);\n");
        } else if (n_exit == 2) {
            fprintf(out,
                    "        __m256i any = _mm256_or_si256(\n"
                    "            _mm256_cmpeq_epi8(chunk, v1),\n"
                    "            _mm256_cmpeq_epi8(chunk, v2));\n");
        } else {
            fprintf(out,
                    "        __m256i any = _mm256_or_si256(\n"
                    "            _mm256_or_si256(\n"
                    "                _mm256_cmpeq_epi8(chunk, v1),\n"
                    "                _mm256_cmpeq_epi8(chunk, v2)),\n"
                    "            _mm256_cmpeq_epi8(chunk, v3));\n");
        }
        fprintf(out, "        unsigned mask = (unsigned)_mm256_movemask_epi8(any);\n");
        fprintf(out, "        if (mask) return pos + (size_t)__builtin_ctz(mask);\n");
        fprintf(out, "        pos += 32;\n");
        fprintf(out, "    }\n");
    } else if (is_neon) {
        for (int i = 0; i < n_exit; i++) {
            fprintf(out, "    const uint8x16_t v%d = vdupq_n_u8((uint8_t)%d);\n", i + 1,
                    exit_set[i]);
        }
        fprintf(out, "    while (pos + 16 <= n) {\n");
        fprintf(out,
                "        uint8x16_t chunk = vld1q_u8(p + pos);\n");
        if (n_exit == 1) {
            fprintf(out, "        uint8x16_t any = vceqq_u8(chunk, v1);\n");
        } else if (n_exit == 2) {
            fprintf(out,
                    "        uint8x16_t any = vorrq_u8(\n"
                    "            vceqq_u8(chunk, v1),\n"
                    "            vceqq_u8(chunk, v2));\n");
        } else {
            fprintf(out,
                    "        uint8x16_t any = vorrq_u8(\n"
                    "            vorrq_u8(vceqq_u8(chunk, v1),\n"
                    "                     vceqq_u8(chunk, v2)),\n"
                    "            vceqq_u8(chunk, v3));\n");
        }
        /* 16-byte mask -> 64-bit narrow-shift trick: each lane
        ** becomes 4 bits of mask.  ctz/4 -> match index. */
        fprintf(out,
                "        uint64_t mask = vget_lane_u64(\n"
                "            vreinterpret_u64_u8(\n"
                "                vshrn_n_u16(vreinterpretq_u16_u8(any), 4)),\n"
                "            0);\n");
        fprintf(out, "        if (mask) return pos + (size_t)(__builtin_ctzll(mask) / 4);\n");
        fprintf(out, "        pos += 16;\n");
        fprintf(out, "    }\n");
    }

    /* Scalar tail (or full body in the scalar variant).  Tight
    ** while-loop with the exit-byte predicate inlined; the
    ** compiler should auto-vectorise this on platforms without an
    ** explicit SIMD path. */
    fprintf(out, "    while (pos < n) {\n");
    fprintf(out, "        unsigned char b = p[pos];\n");
    fprintf(out, "        if (");
    for (int i = 0; i < n_exit; i++) {
        if (i > 0) fprintf(out, " || ");
        fprintf(out, "b == %d", exit_set[i]);
    }
    fprintf(out, ") return pos;\n");
    fprintf(out, "        pos++;\n");
    fprintf(out, "    }\n");
    fprintf(out, "    return n;\n");
    fprintf(out, "}\n\n");
}

/* Emit the per-state-group fast_path dispatcher for one kind.
** Switches on the active DFA state and calls the per-state scanner
** for that state, or returns pos unchanged if the state is not a
** fast-path candidate.  Caller emits #ifdef guards. */
static void emit_fast_path_dispatcher(FILE *out, const char *prefix,
                                      const LimeLexCompiledState *cs, const char *kind) {
    int is_avx2 = strcmp(kind, "avx2") == 0;
    char *upper_state = upper_dup(cs->state_name);
    if (!upper_state) return;

    if (is_avx2) {
        fprintf(out, "__attribute__((target(\"avx2\"), always_inline))\n");
        fprintf(out, "static inline size_t %s_fast_path_%s_%s(\n",
                prefix, upper_state, kind);
    } else {
        fprintf(out, "static LIME_LEX_ALWAYS_INLINE size_t %s_fast_path_%s_%s(\n",
                prefix, upper_state, kind);
    }
    fprintf(out, "        int s, const unsigned char *p, size_t pos, size_t n) {\n");

    if (cs->dfa && cs->dfa->n_states > 0) {
        int any = 0;
        for (int s = 0; s < cs->dfa->n_states; s++) {
            int exit_set[3];
            int n_exit = 0;
            if (!classify_fast_path_state(cs, s, exit_set, &n_exit)) continue;
            if (!any) {
                fprintf(out, "    switch (s) {\n");
                any = 1;
            }
            fprintf(out, "    case %d: return %s_scan_%s_%d_%s(p, pos, n);\n", s, prefix,
                    upper_state, s, kind);
        }
        if (any) {
            fprintf(out, "    default: break;\n");
            fprintf(out, "    }\n");
        }
    }
    fprintf(out, "    (void)s; (void)p; (void)n;\n");
    fprintf(out, "    return pos;\n");
    fprintf(out, "}\n\n");
    free(upper_state);
}

/* Emit per-state scanners + per-state-group dispatcher for one
** kind.  Wraps the whole block in #ifdef when needed. */
static void emit_scanners_for_kind(FILE *out, const char *prefix, const LimeLexCompiled *c,
                                   const char *kind) {
    int is_avx2 = strcmp(kind, "avx2") == 0;
    int is_neon = strcmp(kind, "neon") == 0;

    if (is_avx2) {
        fprintf(out, "#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))\n");
        fprintf(out, "#include <immintrin.h>\n\n");
    } else if (is_neon) {
        fprintf(out, "#if defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))\n");
        fprintf(out, "#include <arm_neon.h>\n\n");
    }

    /* Per-state scanners. */
    for (int g = 0; g < c->n_states; g++) {
        const LimeLexCompiledState *cs = &c->states[g];
        if (!cs->dfa) continue;
        char *upper_state = upper_dup(cs->state_name);
        if (!upper_state) continue;
        for (int s = 0; s < cs->dfa->n_states; s++) {
            int exit_set[3];
            int n_exit = 0;
            if (!classify_fast_path_state(cs, s, exit_set, &n_exit)) continue;
            emit_scan_helper(out, prefix, upper_state, s, exit_set, n_exit, kind);
        }
        free(upper_state);
    }

    /* Per-state-group fast-path dispatchers. */
    for (int g = 0; g < c->n_states; g++) {
        emit_fast_path_dispatcher(out, prefix, &c->states[g], kind);
    }

    if (is_avx2 || is_neon) {
        fprintf(out, "#endif /* %s */\n\n", is_avx2 ? "x86_64+gcc/clang" : "aarch64+gcc/clang");
    }
}

/* Emit one match function body for the given kind.  Identical to
** the legacy <prefix>_match body except:
**   - function name is <prefix>_match_<kind>
**   - if AVX2: carries __attribute__((target("avx2")))
**   - the per-byte loop calls <prefix>_fast_path_<NAME>_<kind>
**     before each byte fetch, and skips ahead when the scanner
**     advances past pos (with last_accept tracking on the way).
**
** When kind == "scalar" the body is identical in shape to the
** legacy single-function emit minus the public symbol -- the
** legacy emit is preserved for the --lex-no-vectorize path. */
static void emit_match_function_for_kind(FILE *out, const LimeLexCompiled *c, const char *prefix,
                                         const char *kind) {
    int is_avx2 = strcmp(kind, "avx2") == 0;
    if (is_avx2) {
        fprintf(out, "__attribute__((target(\"avx2\")))\n");
    }
    fprintf(out, "static int %s_match_%s(int state, const char *bytes, size_t n,\n", prefix,
            kind);
    fprintf(out, "        int *out_rule, size_t *out_consumed) {\n");
    fprintf(out, "    int start;\n");
    fprintf(out, "    int last_accept_rule = -1;\n");
    fprintf(out, "    size_t last_accept_pos = 0;\n");
    fprintf(out, "    size_t i;\n");
    fprintf(out, "    int s;\n");
    fprintf(out, "    const unsigned char *p = (const unsigned char *)bytes;\n");

    fprintf(out, "    switch (state) {\n");
    for (int i = 0; i < c->n_states; i++) {
        char *upper_state = upper_dup(c->states[i].state_name);
        fprintf(out, "    case %d: start = %s_dfa_%s_start; break;\n", i, prefix, upper_state);
        free(upper_state);
    }
    fprintf(out, "    default: return 0;\n");
    fprintf(out, "    }\n");
    fprintf(out, "    s = start;\n\n");

    /* Immediate-accept (empty match) check. */
    fprintf(out, "    switch (state) {\n");
    for (int i = 0; i < c->n_states; i++) {
        char *upper_state = upper_dup(c->states[i].state_name);
        fprintf(out, "    case %d: if (%s_dfa_%s_accept[s] >= 0) {\n", i, prefix, upper_state);
        fprintf(out, "        last_accept_rule = %s_dfa_%s_accept[s];\n", prefix, upper_state);
        fprintf(out, "        last_accept_pos = 0;\n");
        fprintf(out, "    } break;\n");
        free(upper_state);
    }
    fprintf(out, "    }\n\n");

    /* Main loop.  i is mutated inside via the fast-path branch. */
    fprintf(out, "    i = 0;\n");
    fprintf(out, "    while (i < n) {\n");
    /* Fast-path scan: route through per-kind dispatcher.  When the
    ** scanner advances pos, the run is necessarily on a self-loop
    ** state, so the active DFA state s is unchanged.  If the state
    ** is accepting, the longest match extends to new_pos. */
    fprintf(out, "        size_t new_pos;\n");
    fprintf(out, "        switch (state) {\n");
    for (int gi = 0; gi < c->n_states; gi++) {
        char *upper_state = upper_dup(c->states[gi].state_name);
        fprintf(out, "        case %d:\n", gi);
        fprintf(out, "            new_pos = %s_fast_path_%s_%s(s, p, i, n);\n", prefix,
                upper_state, kind);
        fprintf(out, "            break;\n");
        free(upper_state);
    }
    fprintf(out, "        default: new_pos = i; break;\n");
    fprintf(out, "        }\n");
    fprintf(out, "        if (new_pos > i) {\n");
    fprintf(out, "            switch (state) {\n");
    for (int gi = 0; gi < c->n_states; gi++) {
        char *upper_state = upper_dup(c->states[gi].state_name);
        fprintf(out, "            case %d:\n", gi);
        fprintf(out, "                if (%s_dfa_%s_accept[s] >= 0) {\n", prefix, upper_state);
        fprintf(out, "                    last_accept_rule = %s_dfa_%s_accept[s];\n", prefix,
                upper_state);
        fprintf(out, "                    last_accept_pos = new_pos;\n");
        fprintf(out, "                }\n");
        fprintf(out, "                break;\n");
        free(upper_state);
    }
    fprintf(out, "            default: break;\n");
    fprintf(out, "            }\n");
    fprintf(out, "            i = new_pos;\n");
    fprintf(out, "            if (i >= n) break;\n");
    fprintf(out, "        }\n");

    /* Standard step. */
    fprintf(out, "        int next;\n");
    fprintf(out, "        switch (state) {\n");
    for (int gi = 0; gi < c->n_states; gi++) {
        char *upper_state = upper_dup(c->states[gi].state_name);
        fprintf(out, "        case %d:\n", gi);
        fprintf(out, "            next = %s_dfa_%s_trans[s][p[i]];\n", prefix, upper_state);
        fprintf(out, "            break;\n");
        free(upper_state);
    }
    fprintf(out, "        default: return 0;\n");
    fprintf(out, "        }\n");
    fprintf(out, "        if (next < 0) break;\n");
    fprintf(out, "        s = next;\n");
    fprintf(out, "        switch (state) {\n");
    for (int gi = 0; gi < c->n_states; gi++) {
        char *upper_state = upper_dup(c->states[gi].state_name);
        fprintf(out, "        case %d:\n", gi);
        fprintf(out, "            if (%s_dfa_%s_accept[s] >= 0) {\n", prefix, upper_state);
        fprintf(out, "                last_accept_rule = %s_dfa_%s_accept[s];\n", prefix,
                upper_state);
        fprintf(out, "                last_accept_pos = i + 1;\n");
        fprintf(out, "            }\n");
        fprintf(out, "            break;\n");
        free(upper_state);
    }
    fprintf(out, "        default: break;\n");
    fprintf(out, "        }\n");
    fprintf(out, "        i++;\n");
    fprintf(out, "    }\n\n");

    fprintf(out, "    if (last_accept_rule < 0) return 0;\n");
    fprintf(out, "    *out_rule = last_accept_rule;\n");
    fprintf(out, "    *out_consumed = last_accept_pos;\n");
    fprintf(out, "    return 1;\n");
    fprintf(out, "}\n\n");
}

/* Emit the public <prefix>_match() dispatcher.  On x86_64 with
** gcc/clang, performs a one-time __builtin_cpu_supports("avx2")
** check (cached in a function-static int) and routes to either
** match_avx2 or match_scalar.  On aarch64 with gcc/clang, routes
** unconditionally to match_neon (NEON is mandatory on aarch64).
** Everything else falls through to match_scalar. */
static void emit_match_dispatcher(FILE *out, const char *prefix) {
    fprintf(out,
            "int %s_match(int state, const char *bytes, size_t n,\n"
            "             int *out_rule, size_t *out_consumed) {\n"
            "#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))\n"
            "    /* Cached AVX2 detection.  The torn-read race here is\n"
            "    ** benign: every thread computes the same value.  No\n"
            "    ** explicit memory barrier needed -- on x86_64 the int\n"
            "    ** loads/stores are atomic at the machine level. */\n"
            "    static int %s_cpu_avx2 = -1;\n"
            "    int avx2_known = %s_cpu_avx2;\n"
            "    if (avx2_known < 0) {\n"
            "        avx2_known = __builtin_cpu_supports(\"avx2\") ? 1 : 0;\n"
            "        %s_cpu_avx2 = avx2_known;\n"
            "    }\n"
            "    if (avx2_known) {\n"
            "        return %s_match_avx2(state, bytes, n, out_rule, out_consumed);\n"
            "    }\n"
            "    return %s_match_scalar(state, bytes, n, out_rule, out_consumed);\n"
            "#elif defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))\n"
            "    return %s_match_neon(state, bytes, n, out_rule, out_consumed);\n"
            "#else\n"
            "    return %s_match_scalar(state, bytes, n, out_rule, out_consumed);\n"
            "#endif\n"
            "}\n\n",
            prefix, prefix, prefix, prefix, prefix, prefix, prefix, prefix);
}

int lime_lex_emit_c(const LimeLexCompiled *c, const LimeLexSpec *spec, const char *name_prefix,
                    const char *header_basename, const char *const *rule_names, int n_rules,
                    FILE *out) {
    if (!c || !out) return -1;
    const char *prefix = eff_prefix(name_prefix);
    char *PREFIX = upper_prefix(name_prefix);
    if (!PREFIX) return -1;

    fprintf(out, "/* Generated by lime -X.  DO NOT EDIT. */\n");
    if (header_basename && *header_basename) {
        fprintf(out, "#include \"%s\"\n", header_basename);
    }
    fprintf(out, "#include <stddef.h>\n");
    /* M3.7: LEX_BUF_APPEND uses memcpy().  String.h is also a
    ** harmless include for grammars without literal buffers. */
    fprintf(out, "#include <string.h>\n\n");

    /* v0.8.10 portability macros for the SIMD/multiversion emit.
    ** gcc/clang have __attribute__((always_inline)); MSVC has
    ** __forceinline; everything else gets plain `inline`.  The
    ** target_feature path is gcc/clang-only (already guarded by
    ** #if defined(__GNUC__) || defined(__clang__) at the call
    ** sites), so no MSVC stub needed for that. */
    fprintf(out,
            "#if defined(__GNUC__) || defined(__clang__)\n"
            "# define LIME_LEX_ALWAYS_INLINE __attribute__((always_inline)) inline\n"
            "#elif defined(_MSC_VER)\n"
            "# define LIME_LEX_ALWAYS_INLINE __forceinline\n"
            "#else\n"
            "# define LIME_LEX_ALWAYS_INLINE inline\n"
            "#endif\n\n");

    /* P0-NEW-9: %include { ... } body emitted verbatim BEFORE
    ** any generated declarations so user typedefs / static
    ** helpers / extra #include lines are visible to every
    ** subsequent emission (DFA tables, action bodies, runtime
    ** macros).  The unwrapped body has no surrounding braces;
    ** the user wrote it in their preferred style. */
    if (spec && spec->include_block) {
        fprintf(out, "/* ===== %%include { ... } block (P0-NEW-9) ===== */\n");
        fprintf(out, "%s\n\n", spec->include_block);
    }

    /* M3.7: alloc-function presence guards.  A %literal_buffer
    ** declaration without alloc/realloc/free is unusable; emit
    ** #error so the failure surfaces at the user's `cc` step
    ** rather than as an unresolved symbol at link time. */
    emit_buf_alloc_guards(spec, out);

    /* Rule-name table. */
    fprintf(out, "const char *const %sRuleNames[] = {\n", prefix);
    for (int i = 0; i < n_rules; i++) {
        fprintf(out, "    \"%s\"%s\n", rule_names[i], (i + 1 < n_rules) ? "," : "");
    }
    if (n_rules == 0) fprintf(out, "    /* no rules */\n");
    fprintf(out, "    %s NULL\n", n_rules > 0 ? "," : "");
    fprintf(out, "};\n\n");

    /* DFA tables per state. */
    for (int i = 0; i < c->n_states; i++) {
        emit_state_tables(&c->states[i], i, prefix, out);
    }

    /* Match function: longest-match driver.
    **
    ** v0.8.10: when --lex-vectorize (default ON), emit the
    ** multiversion-at-tokenize SIMD architecture: per-state AVX2 /
    ** NEON / scalar fast-path scan helpers, per-kind match
    ** functions, and a public dispatcher that runtime-detects AVX2
    ** via __builtin_cpu_supports.  When --lex-no-vectorize, emit
    ** the legacy single-function scalar driver. */
    if (g_lime_lex_vectorize_flag) {
        /* SIMD scanners + per-kind match functions + dispatcher. */
        emit_scanners_for_kind(out, prefix, c, "avx2");
        emit_scanners_for_kind(out, prefix, c, "neon");
        emit_scanners_for_kind(out, prefix, c, "scalar");

        fprintf(out,
                "#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))\n");
        emit_match_function_for_kind(out, c, prefix, "avx2");
        fprintf(out, "#endif\n\n");

        fprintf(out,
                "#if defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))\n");
        emit_match_function_for_kind(out, c, prefix, "neon");
        fprintf(out, "#endif\n\n");

        emit_match_function_for_kind(out, c, prefix, "scalar");
        emit_match_dispatcher(out, prefix);
    } else {
        /* Legacy scalar single-function emit (--lex-no-vectorize).
        ** v0.8.x: per-byte loop hoists the per-state trans + accept
        ** table pointers ABOVE the loop instead of doing a
        ** switch(state) twice per byte (once for trans lookup, once
        ** for accept check).  Per .agent/notes/c-perf-audit.md
        ** item #2: state is loop-invariant inside the byte loop;
        ** the prior emit asked the compiler to dispatch on it
        ** 2*n times per call when once was sufficient. */
        fprintf(out, "int %s_match(int state, const char *bytes, size_t n,\n", prefix);
        fprintf(out, "             int *out_rule, size_t *out_consumed) {\n");
        fprintf(out, "    int start;\n");
        fprintf(out, "    int last_accept_rule = -1;\n");
        fprintf(out, "    size_t last_accept_pos = 0;\n");
        fprintf(out, "    size_t i;\n");
        fprintf(out, "    int s;\n");
        fprintf(out, "    /* Hoisted: pick per-state trans/accept pointers ONCE. */\n");
        fprintf(out, "    const short (*trans)[256] = NULL;\n");
        fprintf(out, "    const short *accept = NULL;\n");
        fprintf(out, "    switch (state) {\n");
        for (int i = 0; i < c->n_states; i++) {
            char *upper_state = upper_dup(c->states[i].state_name);
            fprintf(out, "    case %d:\n", i);
            fprintf(out, "        start  = %s_dfa_%s_start;\n", prefix, upper_state);
            fprintf(out, "        trans  = %s_dfa_%s_trans;\n", prefix, upper_state);
            fprintf(out, "        accept = %s_dfa_%s_accept;\n", prefix, upper_state);
            fprintf(out, "        break;\n");
            free(upper_state);
        }
        fprintf(out, "    default: return 0;\n");
        fprintf(out, "    }\n");
        fprintf(out, "    s = start;\n\n");

        fprintf(out, "    /* Check accept at position 0 (empty match). */\n");
        fprintf(out, "    if (accept[s] >= 0) {\n");
        fprintf(out, "        last_accept_rule = accept[s];\n");
        fprintf(out, "        last_accept_pos = 0;\n");
        fprintf(out, "    }\n\n");

        fprintf(out, "    for (i = 0; i < n; i++) {\n");
        fprintf(out, "        int next = trans[s][(unsigned char)bytes[i]];\n");
        fprintf(out, "        if (next < 0) break;\n");
        fprintf(out, "        s = next;\n");
        fprintf(out, "        if (accept[s] >= 0) {\n");
        fprintf(out, "            last_accept_rule = accept[s];\n");
        fprintf(out, "            last_accept_pos = i + 1;\n");
        fprintf(out, "        }\n");
        fprintf(out, "    }\n\n");

        fprintf(out, "    if (last_accept_rule < 0) return 0;\n");
        fprintf(out, "    *out_rule = last_accept_rule;\n");
        fprintf(out, "    *out_consumed = last_accept_pos;\n");
        fprintf(out, "    return 1;\n");
        fprintf(out, "}\n\n");
    }

    /* ===== M3.3 + M3.5: push-driven runtime ===== */
    fprintf(out, "/* ===== Push-driven runtime (M3.3 + M3.5) ===== */\n\n");
    fprintf(out,
            "/* Buffer stack frame.  Caller-supplied bytes are NOT copied\n"
            "** -- the caller retains ownership.  pos is the next-byte-to-\n"
            "** match offset; LexPushback rewinds it. */\n"
            "struct %sLexer {\n"
            "    int          state;\n"
            "    const char  *err_msg;\n"
            "    int          depth;       /* # frames currently on stack */\n"
            "    struct {\n"
            "        const char *bytes;\n"
            "        size_t      len;\n"
            "        size_t      pos;\n"
            "    } stack[%s_LEX_MAX_INCLUDE_DEPTH];\n",
            prefix, PREFIX);
    /* M3.7: per-buffer storage fields. */
    emit_buf_struct_fields(spec, out);
    fprintf(out, "};\n\n");

    /* M3.7: per-buffer grow + take static helpers (defined now
    ** because LexAlloc / LexFree don't reference them, but the
    ** LEX_BUF_* macros emitted before LexFeedBytes do). */
    emit_buf_helpers(spec, prefix, out);

    fprintf(out,
            "%sLexer *%sLexAlloc(void *(*mallocProc)(size_t)) {\n"
            "    if (!mallocProc) return 0;\n"
            "    %sLexer *yyl = (%sLexer*)mallocProc(sizeof(*yyl));\n"
            "    if (!yyl) return 0;\n"
            "    yyl->state = 0;          /* INITIAL */\n"
            "    yyl->err_msg = 0;\n"
            "    yyl->depth = 0;\n",
            prefix, prefix, prefix, prefix);
    /* M3.7: zero-initialise per-buffer storage. */
    emit_buf_alloc_init(spec, out);
    fprintf(out, "    return yyl;\n"
                 "}\n\n");
    fprintf(out,
            "void %sLexFree(%sLexer *yyl, void (*freeProc)(void *)) {\n"
            "    if (yyl) {\n",
            prefix, prefix);
    /* M3.7: walk every declared buffer and free anything still
    ** alive (e.g. accumulator filled but LEX_BUF_TAKE never run
    ** before the caller dropped the lexer).  Each buffer uses
    ** the user-declared <free> function paired with its alloc. */
    emit_buf_free_walk(spec, out);
    fprintf(out, "    }\n"
                 "    if (yyl && freeProc) freeProc(yyl);\n"
                 "}\n\n");

    /* M3.4: action-body inlining + LEX_* macros.
    **
    ** Collect each rule's action body in declaration order so
    ** we can emit one switch case per rule inside FeedBytes.
    ** EOF rules are kept in the parallel array for index
    ** alignment but skipped from the switch (their bodies fire
    ** at LexFeedEOF time, not yet implemented -- see M3.5+).
    ** When spec is NULL (legacy entry point, e.g. from a unit
    ** test that doesn't need actions), the switch degrades to
    ** the empty default branch and every match falls through
    ** to the auto-emit path -- the M3.3 contract verbatim. */
    const char **rule_actions = NULL;
    int *rule_is_eof = NULL;
    int have_actions = 0;
    if (spec) {
        if (collect_rule_actions(spec, n_rules, &rule_actions, &rule_is_eof) == 0) {
            have_actions = 1;
        }
    }

    /* M3.6: per-state EOF-rule lookup.  state_eof_rule[i] is
    ** the global rule index of the <<EOF>> rule that fires when
    ** the lexer hits end-of-input in state i, or -1 if no such
    ** rule applies.  Multi-state qualifiers (e.g. <a, b><<EOF>>)
    ** populate every listed state's slot with the same rule
    ** index.  If a state has more than one applicable EOF rule
    ** (overlapping multi-state qualifiers), the first one in
    ** declaration order wins. */
    short *state_eof_rule = NULL;
    int any_eof = 0;
    if (have_actions && c->n_states > 0) {
        state_eof_rule = malloc((size_t)c->n_states * sizeof(*state_eof_rule));
        if (!state_eof_rule) {
            free(rule_actions);
            free(rule_is_eof);
            free(PREFIX);
            return -1;
        }
        for (int si = 0; si < c->n_states; si++) {
            state_eof_rule[si] = -1;
            const LimeLexCompiledState *cs = &c->states[si];
            for (int k = 0; k < cs->n_rules; k++) {
                int gi = cs->rule_indices[k];
                if (gi >= 0 && gi < n_rules && rule_is_eof[gi]) {
                    state_eof_rule[si] = (short)gi;
                    any_eof = 1;
                    break;
                }
            }
        }
    }

    /* Macro definitions: scoped to LexFeedBytes by paired
    ** #define / #undef.  They reference the function-locals
    ** matched, matched_len, lex, state, _action_emitted,
    ** _terminate, _error, emit, user. */
    fprintf(out,
            "/* ===== Action-body primitives (M3.4) =====\n"
            "** Defined immediately before %sLexFeedBytes and undefined\n"
            "** immediately after.  They drive the per-iteration switch\n"
            "** that dispatches each match to its action body. */\n"
            "#define LEX_EMIT(rule_code) do {                              \\\n"
            "    if (emit) emit(user, (int)(rule_code), matched, matched_len); \\\n"
            "    _action_emitted = 1;                                      \\\n"
            "} while (0)\n"
            "#define LEX_SKIP() do { _action_emitted = 1; } while (0)\n"
            "#define LEX_TRANSITION(state_id) do {                         \\\n"
            "    int _new_state = (int)(state_id);                          \\\n"
            "    state = _new_state;                                        \\\n"
            "    lex->state = _new_state;                                   \\\n"
            "} while (0)\n"
            "#define LEX_TERMINATE() do { _terminate = 1; } while (0)\n"
            "#define LEX_ERROR_AT(msg) do {                                \\\n"
            "    _error = 1;                                               \\\n"
            "    lex->err_msg = (msg);                                     \\\n"
            "} while (0)\n"
            "#define LEX_PUSHBACK(n) (void)%sLexPushback(lex, (size_t)(n))\n",
            prefix, prefix);
    /* M3.7: literal-buffer macros.  Each one references the
    ** function-locals lex / _error / lex->err_msg in scope
    ** inside LexFeedBytes, plus the per-buffer grow/take
    ** static helpers emitted above.  All buffers share the
    ** same macro family -- the buffer-name token is the
    ** distinguishing identifier via paste.  Pairs with the
    ** #undef block after LexFeedBytes. */
    if (has_literal_buffers(spec)) {
        fprintf(out,
                "/* ===== Literal buffer primitives (M3.7) ===== */\n"
                "#define LEX_BUF_START(name) do {                              \\\n"
                "    lex->name##_len = 0;                                      \\\n"
                "} while (0)\n"
                "#define LEX_BUF_APPEND(name, ptr, n) do {                     \\\n"
                "    size_t _need = lex->name##_len + (size_t)(n);             \\\n"
                "    if (%s_buf_##name##_grow(lex, _need) == 0) {              \\\n"
                "        memcpy(lex->name##_buf + lex->name##_len,             \\\n"
                "               (ptr), (size_t)(n));                           \\\n"
                "        lex->name##_len = _need;                              \\\n"
                "    } else {                                                  \\\n"
                "        _error = 1;                                           \\\n"
                "        lex->err_msg = \"literal buffer alloc failed\";        \\\n"
                "    }                                                         \\\n"
                "} while (0)\n"
                "#define LEX_BUF_APPEND_CH(name, c) do {                       \\\n"
                "    if (%s_buf_##name##_grow(lex, lex->name##_len + 1) == 0) {\\\n"
                "        lex->name##_buf[lex->name##_len++] = (c);             \\\n"
                "    } else {                                                  \\\n"
                "        _error = 1;                                           \\\n"
                "        lex->err_msg = \"literal buffer alloc failed\";        \\\n"
                "    }                                                         \\\n"
                "} while (0)\n"
                "#define LEX_BUF_TAKE(name) (%s_buf_##name##_take(lex))\n"
                "#define LEX_BUF_LEN(name)  (lex->name##_len)\n"
                "#define LEX_BUF_PEEK(name) (lex->name##_buf)\n\n",
                prefix, prefix, prefix);
    } else {
        fprintf(out, "\n");
    }

    /* M3.6: emit the per-state EOF-rule lookup table.  The
    ** auto-pop branch of LexFeedBytes indexes this with the
    ** lexer's current state to find the rule whose action body
    ** to run before unwinding the buffer-stack frame. */
    if (any_eof) {
        fprintf(out,
                "/* M3.6: per-state <<EOF>> rule index (-1 = no rule). */\n"
                "static const short %s_eof_rule[%d] = {\n   ",
                prefix, c->n_states);
        for (int si = 0; si < c->n_states; si++) {
            fprintf(out, " %d%s", state_eof_rule[si], (si + 1 < c->n_states) ? "," : "");
            if ((si + 1) % 12 == 0 && si + 1 < c->n_states) {
                fprintf(out, "\n   ");
            }
        }
        fprintf(out, "\n};\n\n");
    }

    fprintf(out,
            "%sLexResult %sLexInclude(%sLexer *yyl,\n"
            "                            const char *bytes, size_t n) {\n"
            "    if (!yyl) return %s_LEX_ERROR;\n"
            "    if (yyl->depth >= %s_LEX_MAX_INCLUDE_DEPTH) {\n"
            "        yyl->err_msg = \"include depth exceeded\";\n"
            "        return %s_LEX_ERROR;\n"
            "    }\n"
            "    yyl->stack[yyl->depth].bytes = bytes;\n"
            "    yyl->stack[yyl->depth].len   = n;\n"
            "    yyl->stack[yyl->depth].pos   = 0;\n"
            "    yyl->depth++;\n"
            "    return %s_LEX_OK;\n"
            "}\n\n",
            prefix, prefix, prefix, PREFIX, PREFIX, PREFIX, PREFIX);
    fprintf(out,
            "%sLexResult %sLexPushback(%sLexer *yyl, size_t n) {\n"
            "    if (!yyl || yyl->depth <= 0) {\n"
            "        if (yyl) yyl->err_msg = \"pushback on empty stack\";\n"
            "        return %s_LEX_ERROR;\n"
            "    }\n"
            "    size_t pos = yyl->stack[yyl->depth - 1].pos;\n"
            "    if (n > pos) {\n"
            "        yyl->err_msg = \"pushback exceeds buffered prefix\";\n"
            "        return %s_LEX_ERROR;\n"
            "    }\n"
            "    yyl->stack[yyl->depth - 1].pos = pos - n;\n"
            "    return %s_LEX_OK;\n"
            "}\n\n",
            prefix, prefix, prefix, PREFIX, PREFIX, PREFIX);
    fprintf(out,
            "int %sLexIncludeDepth(const %sLexer *yyl) {\n"
            "    if (!yyl || yyl->depth <= 0) return 0;\n"
            "    return yyl->depth - 1;\n"
            "}\n\n",
            prefix, prefix);
    /* P0-NEW-12: optional %lexer_extra_argument parameter, threaded
    ** through to action bodies as a normal C parameter (the user's
    ** identifier is in scope in every action body switch case).
    ** P0-NEW-13: a goto-cleanup pattern below ensures every return
    ** path -- normal completion, LEX_TERMINATE, LEX_ERROR_AT,
    ** unmatched-input -- pops back to initial_depth so per-call
    ** frames never leak across LexFeedBytes invocations. */
    const char *extra_arg = (spec && spec->extra_argument) ? spec->extra_argument : 0;
    /* Identifier extraction (mirrors lime.c's %extra_argument trick):
    ** walk back skipping whitespace, then back-skip ALPHA/_/digits to
    ** find the trailing identifier.  Used only to suppress the
    ** -Wunused-parameter warning when no action body references it. */
    const char *extra_name = 0;
    if (extra_arg) {
        size_t L = strlen(extra_arg);
        while (L > 0 &&
               (extra_arg[L - 1] == ' ' || extra_arg[L - 1] == '\t' || extra_arg[L - 1] == '\n'))
            L--;
        size_t E = L;
        while (L > 0) {
            char ch = extra_arg[L - 1];
            if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
                ch == '_')
                L--;
            else
                break;
        }
        if (L < E) extra_name = extra_arg + L;
    }
    if (extra_arg) {
        fprintf(out,
                "%sLexResult %sLexFeedBytes(%sLexer *yyl,\n"
                "                              const char *bytes, size_t n,\n"
                "                              %sEmitFn emit, void *user,\n"
                "                              %s) {\n",
                prefix, prefix, prefix, prefix, extra_arg);
    } else {
        fprintf(out,
                "%sLexResult %sLexFeedBytes(%sLexer *yyl,\n"
                "                              const char *bytes, size_t n,\n"
                "                              %sEmitFn emit, void *user) {\n",
                prefix, prefix, prefix, prefix);
    }
    fprintf(out,
            "    if (!yyl) return %s_LEX_ERROR;\n"
            "    yyl->err_msg = 0;\n"
            "    /* M3.5: push the caller's buffer as a new bottom-of-this-call\n"
            "    ** frame, then drive top-of-stack until depth drops back to\n"
            "    ** where it was before the push.  Nested LexInclude calls\n"
            "    ** invoked from inside the user's action body raise the loop\n"
            "    ** bound; each auto-pop on EOF lowers it. */\n"
            "    if (yyl->depth >= %s_LEX_MAX_INCLUDE_DEPTH) {\n"
            "        yyl->err_msg = \"include depth exceeded\";\n"
            "        return %s_LEX_ERROR;\n"
            "    }\n"
            "    int initial_depth = yyl->depth;\n"
            "    yyl->stack[yyl->depth].bytes = bytes;\n"
            "    yyl->stack[yyl->depth].len   = n;\n"
            "    yyl->stack[yyl->depth].pos   = 0;\n"
            "    yyl->depth++;\n"
            "    /* M3.4: function-scope flags set by LEX_TERMINATE /\n"
            "    ** LEX_ERROR_AT inside an action body. */\n"
            "    int _terminate = 0;\n"
            "    int _error = 0;\n"
            "    /* P0-NEW-13: result accumulator + drain label.  Every\n"
            "    ** abnormal exit (_terminate, _error, unmatched input)\n"
            "    ** sets _result and jumps to _feed_done; the drain loop\n"
            "    ** there pops any frames pushed during this call so the\n"
            "    ** depth invariant on return is yyl->depth == initial_depth. */\n"
            "    %sLexResult _result = %s_LEX_OK;\n",
            PREFIX, PREFIX, PREFIX, prefix, PREFIX);
    if (extra_name) {
        /* Suppress -Wunused-parameter when no action body references
        ** the extra arg.  The user's identifier is already in scope
        ** via standard C parameter binding. */
        fprintf(out, "    (void)%s;\n", extra_name);
    }
    fprintf(out, "    while (yyl->depth > initial_depth) {\n"
                 "        int top = yyl->depth - 1;\n"
                 "        size_t fpos = yyl->stack[top].pos;\n"
                 "        size_t flen = yyl->stack[top].len;\n"
                 "        if (fpos >= flen) {\n");

    /* M3.6: EOF-rule dispatch inside the auto-pop branch.  When
    ** the top-of-stack frame is exhausted, look up the EOF rule
    ** for the current lexer state and run its action body before
    ** unwinding the frame.  An EOF action body sees matched
    ** pointing one past the last byte of the current frame's
    ** input, matched_len=0, and the same lex/state/_action_emitted
    ** locals as a normal-rule body, so LEX_EMIT/LEX_TRANSITION/
    ** LEX_ERROR_AT/LEX_TERMINATE all work.  No auto-emit on
    ** EOF: the rule's body is the source of truth.
    **
    ** Letter 13 / commit 4a740099a0c (PG migration team): the
    ** previous codegen used `matched = ""` (a static empty
    ** string with no relation to the caller's buffer).  Drivers
    ** that recover the consumed prefix via pointer arithmetic
    ** (`pos = matched + matched_len - bytes`) fall through to
    ** garbage when an EOF rule's action emits a token.  Pass
    ** `fbytes + flen` (one past the end of the current frame's
    ** input) so the arithmetic resolves cleanly: for the bottom
    ** frame `matched == bytes + n`; for include frames it is
    ** the EOF position in that frame's buffer. */
    if (any_eof) {
        fprintf(out,
                "            /* M3.6: fire <<EOF>> rule for the current state. */\n"
                "            int _eof_rule = -1;\n"
                "            if (yyl->state >= 0 && yyl->state < %d) {\n"
                "                _eof_rule = %s_eof_rule[yyl->state];\n"
                "            }\n"
                "            if (_eof_rule >= 0) {\n"
                "                const char *fbytes = yyl->stack[top].bytes;\n"
                "                const char *matched = fbytes + flen;\n"
                "                size_t matched_len = 0;\n"
                "                %sLexer *lex = yyl;\n"
                "                int state = lex->state;\n"
                "                int _action_emitted = 0;\n"
                "                switch (_eof_rule) {\n",
                c->n_states, prefix, prefix);
        for (int i = 0; i < n_rules; i++) {
            if (!rule_is_eof[i]) continue;
            const char *body = rule_actions[i] ? rule_actions[i] : "";
            fprintf(out,
                    "                case %d: {\n"
                    "%s\n"
                    "                } break;\n",
                    i, body);
        }
        fprintf(out,
                "                default: break;\n"
                "                }\n"
                "                lex->state = state;\n"
                "                (void)fbytes; (void)matched; (void)matched_len;\n"
                "                (void)_action_emitted;\n"
                "                if (_error) { _result = %s_LEX_ERROR; goto _feed_done; }\n"
                "                if (_terminate) { _result = %s_LEX_OK; goto _feed_done; }\n"
                "            }\n",
                PREFIX, PREFIX);
    }

    fprintf(out,
            "            yyl->depth--;\n"
            "            continue;\n"
            "        }\n"
            "        const char *fbytes = yyl->stack[top].bytes;\n"
            "        int rule = -1;\n"
            "        size_t consumed = 0;\n"
            "        int ok = %s_match(yyl->state, fbytes + fpos, flen - fpos,\n"
            "                          &rule, &consumed);\n"
            "        if (!ok || consumed == 0) {\n"
            "            yyl->err_msg = \"unmatched input\";\n"
            "            _result = %s_LEX_ERROR;\n"
            "            goto _feed_done;\n"
            "        }\n"
            "        /* M3.5 invariant: advance pos BEFORE the action body\n"
            "        ** so a LexInclude issued from inside the body lands\n"
            "        ** its new frame above already-consumed parent bytes,\n"
            "        ** and so LEX_PUSHBACK can rewind from the post-match\n"
            "        ** position. */\n"
            "        yyl->stack[top].pos = fpos + consumed;\n"
            "        /* M3.4: per-iteration locals + action body switch. */\n"
            "        const char *matched = fbytes + fpos;\n"
            "        size_t matched_len = consumed;\n"
            "        %sLexer *lex = yyl;\n"
            "        int state = lex->state;\n"
            "        int _action_emitted = 0;\n"
            "        switch (rule) {\n",
            prefix, PREFIX, prefix);

    if (have_actions) {
        for (int i = 0; i < n_rules; i++) {
            if (rule_is_eof[i]) continue;
            const char *body = rule_actions[i] ? rule_actions[i] : "";
            fprintf(out,
                    "        case %d: {\n"
                    "%s\n"
                    "        } break;\n",
                    i, body);
        }
    }

    fprintf(out,
            "        default: break;\n"
            "        }\n"
            "        lex->state = state;\n"
            "        if (!_action_emitted && !_error) {\n"
            "            if (emit) emit(user, rule, matched, matched_len);\n"
            "        }\n"
            "        if (_error) { _result = %s_LEX_ERROR; goto _feed_done; }\n"
            "        if (_terminate) { _result = %s_LEX_OK; goto _feed_done; }\n"
            "    }\n"
            "    _result = %s_LEX_OK;\n"
            "_feed_done:\n"
            "    /* P0-NEW-13: drain any frames pushed during this call.\n"
            "    ** The main loop's auto-pop already drains on normal\n"
            "    ** completion, but LEX_TERMINATE / LEX_ERROR_AT /\n"
            "    ** unmatched-input bail out with frames still live\n"
            "    ** (including the bottom frame LexFeedBytes itself\n"
            "    ** pushed).  After this drain, the depth invariant\n"
            "    ** yyl->depth == initial_depth holds on every return\n"
            "    ** path.  M3.6: stack[].bytes is caller-owned, so\n"
            "    ** popping is just a counter decrement. */\n"
            "    while (yyl->depth > initial_depth) {\n"
            "        yyl->depth--;\n"
            "    }\n"
            "    return _result;\n"
            "}\n\n",
            PREFIX, PREFIX, PREFIX);

    fprintf(out, "#undef LEX_EMIT\n"
                 "#undef LEX_SKIP\n"
                 "#undef LEX_TRANSITION\n"
                 "#undef LEX_TERMINATE\n"
                 "#undef LEX_ERROR_AT\n"
                 "#undef LEX_PUSHBACK\n");
    if (has_literal_buffers(spec)) {
        fprintf(out, "#undef LEX_BUF_START\n"
                     "#undef LEX_BUF_APPEND\n"
                     "#undef LEX_BUF_APPEND_CH\n"
                     "#undef LEX_BUF_TAKE\n"
                     "#undef LEX_BUF_LEN\n"
                     "#undef LEX_BUF_PEEK\n");
    }
    fprintf(out, "\n");

    free(rule_actions);
    free(rule_is_eof);
    free(state_eof_rule);
    fprintf(out,
            "%sLexResult %sLexFeedEOF(%sLexer *yyl,\n"
            "                            %sEmitFn emit, void *user) {\n"
            "    /* M3.6: <<EOF>> rule dispatch lives in the LexFeedBytes\n"
            "    ** auto-pop branch -- it fires once per buffer-stack\n"
            "    ** frame as that frame hits end-of-input, including the\n"
            "    ** bottom frame that LexFeedBytes itself pushes.  This\n"
            "    ** entry point therefore has nothing to do beyond signal\n"
            "    ** clean termination; calling it after LexFeedBytes\n"
            "    ** returned LEX_OK is a no-op. */\n"
            "    (void)yyl; (void)emit; (void)user;\n"
            "    return %s_LEX_OK;\n"
            "}\n\n",
            prefix, prefix, prefix, prefix, PREFIX);
    fprintf(out,
            "int %sLexCurrentState(const %sLexer *yyl) {\n"
            "    return yyl ? yyl->state : -1;\n"
            "}\n\n",
            prefix, prefix);
    fprintf(out,
            "void %sLexSetState(%sLexer *yyl, int state) {\n"
            "    if (yyl) yyl->state = state;\n"
            "}\n\n",
            prefix, prefix);
    fprintf(out,
            "const char *%sLexErrorMessage(const %sLexer *yyl) {\n"
            "    return yyl ? yyl->err_msg : 0;\n"
            "}\n",
            prefix, prefix);

    /* M4.3: per-rule test entry-point definitions. */
    if (spec) {
        fprintf(out, "\n/* ===== Per-rule test entry points (M4.3) ===== */\n");
        if (emit_test_wrappers(c, spec, prefix, PREFIX, 0, out) != 0) {
            free(PREFIX);
            return -1;
        }
    }

    free(PREFIX);
    return ferror(out) ? -1 : 0;
}
