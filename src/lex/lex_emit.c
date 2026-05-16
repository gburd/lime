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
** Action-body emission helpers (M3.4)
** ============================================================ */

typedef struct {
    const char **actions;
    int         *is_eof;
    int          n;
    int          cap;
    int          oom;
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
static int collect_rule_actions(const LimeLexSpec *spec,
                                int expected_n,
                                const char ***actions_out,
                                int **is_eof_out) {
    *actions_out = NULL;
    *is_eof_out = NULL;
    if (!spec) return -1;
    ActionVec v = {0};
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
    int    n;
    int    cap;
    int    oom;
} NameVec;

static int name_visit(const LimeLexRule *r, void *user) {
    NameVec *v = (NameVec *)user;
    if (v->n == v->cap) {
        int nc = v->cap ? v->cap * 2 : 16;
        char **nn = realloc(v->names, (size_t)nc * sizeof(*nn));
        if (!nn) { v->oom = 1; return -1; }
        v->names = nn;
        v->cap = nc;
    }
    v->names[v->n] = strdup(r->name ? r->name : "anon");
    if (!v->names[v->n]) { v->oom = 1; return -1; }
    sanitise_ident(v->names[v->n]);
    v->n++;
    return 0;
}

int lime_lex_collect_rule_names(const LimeLexSpec *spec,
                                char ***names_out,
                                int *n_rules_out) {
    if (!spec || !names_out || !n_rules_out) return -1;
    NameVec v = {0};
    if (lime_lex_walk_rules(spec, name_visit, &v) != 0) {
        for (int k = 0; k < v.n; k++) free(v.names[k]);
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
    FILE       *out;
    const char *prefix;        /* "Foo" */
    char       *PREFIX;        /* "FOO" -- borrowed, do not free */
    int         emit_h;        /* 1 = decls only, 0 = bodies */
    int         compiled_state_count;
    const LimeLexCompiledState *compiled_states;
    int         err;
} TestWrapperCtx;

/* Look up a state name in the compiled state list.  Returns
** the state's index (suitable for FOO_STATE_<NAME> selection)
** or -1 if the name is not known.  We compare on the original
** state name, not the upper-cased variant, since c->states[]
** stores the source-form name. */
static int compiled_state_index(const TestWrapperCtx *ctx,
                                const char *name) {
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
    if (!suffix) { ctx->err = 1; return -1; }
    sanitise_ident(suffix);

    char *RULE_UPPER = upper_dup(suffix);
    if (!RULE_UPPER) { free(suffix); ctx->err = 1; return -1; }

    /* Pick the state for the wrapper.  Unqualified -> INITIAL.
    ** Qualified -> first state.  Defensive: if the named state
    ** is unknown (malformed grammar) skip the wrapper -- emitting
    ** a reference to FOO_STATE_<UNKNOWN> would break compilation. */
    const char *state_name = "INITIAL";
    if (r->n_states > 0 && r->states && r->states[0]) {
        state_name = r->states[0];
        if (compiled_state_index(ctx, state_name) < 0) {
            free(suffix); free(RULE_UPPER);
            return 0;
        }
    }
    char *STATE_UPPER = upper_dup(state_name);
    if (!STATE_UPPER) {
        free(suffix); free(RULE_UPPER);
        ctx->err = 1; return -1;
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
            ctx->prefix, ctx->prefix, suffix,
            ctx->prefix, ctx->PREFIX, STATE_UPPER,
            ctx->PREFIX, RULE_UPPER, ctx->PREFIX,
            ctx->PREFIX);
    }
    free(suffix);
    free(RULE_UPPER);
    free(STATE_UPPER);
    return 0;
}

static int emit_test_wrappers(const LimeLexCompiled *c,
                              const LimeLexSpec *spec,
                              const char *prefix,
                              char *PREFIX,
                              int emit_h,
                              FILE *out) {
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

int lime_lex_emit_h(const LimeLexCompiled *c,
                    const LimeLexSpec *spec,
                    const char *name_prefix,
                    const char *const *rule_names,
                    int n_rules,
                    FILE *out) {
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
        if (!upper_state) { free(PREFIX); return -1; }
        fprintf(out, "#define %s_STATE_%-20s %d\n",
                PREFIX, upper_state, i);
        free(upper_state);
    }
    fprintf(out, "\n");

    /* Rule-id constants. */
    if (n_rules > 0) {
        fprintf(out, "/* Rule constants (declaration order). */\n");
        fprintf(out, "enum {\n");
        for (int i = 0; i < n_rules; i++) {
            char *upper_rule = upper_dup(rule_names[i]);
            if (!upper_rule) { free(PREFIX); return -1; }
            fprintf(out, "    %s_RULE_%-20s = %d%s\n",
                    PREFIX, upper_rule, i,
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
    fprintf(out, "int %s_match(int state, const char *bytes, size_t n,\n",
            prefix);
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
        "void          %sLexFree(%sLexer *yyl, void (*freeProc)(void *));\n"
        "%sLexResult  %sLexFeedBytes(%sLexer *yyl,\n"
        "                              const char *bytes, size_t n,\n"
        "                              %sEmitFn emit, void *user);\n"
        "%sLexResult  %sLexFeedEOF(%sLexer *yyl,\n"
        "                            %sEmitFn emit, void *user);\n"
        "int           %sLexCurrentState(const %sLexer *yyl);\n"
        "void          %sLexSetState(%sLexer *yyl, int state);\n"
        "const char   *%sLexErrorMessage(const %sLexer *yyl);\n\n",
        prefix, prefix,
        prefix, prefix,
        prefix, prefix, prefix,
        prefix,
        prefix, prefix, prefix,
        prefix,
        prefix, prefix,
        prefix, prefix,
        prefix, prefix);

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
static void emit_state_tables(const LimeLexCompiledState *cs,
                              int compiled_state_index,
                              const char *prefix,
                              FILE *out) {
    char *upper_state = upper_dup(cs->state_name);
    int ns = cs->dfa ? cs->dfa->n_states : 0;
    fprintf(out, "/* DFA tables for state %s (%d states, %d rules) */\n",
            cs->state_name, ns, cs->n_rules);
    if (ns == 0 || !cs->dfa) {
        fprintf(out,
                "static const int %s_dfa_%s_start = 0;\n\n",
                prefix, upper_state);
        free(upper_state);
        return;
    }
    fprintf(out, "static const int %s_dfa_%s_start = %d;\n",
            prefix, upper_state, cs->dfa->start);

    /* trans[N][256] */
    fprintf(out, "static const short %s_dfa_%s_trans[%d][256] = {\n",
            prefix, upper_state, ns);
    for (int s = 0; s < ns; s++) {
        fprintf(out, "    /* state %d */ {", s);
        for (int b = 0; b < 256; b++) {
            if (b % 16 == 0) fprintf(out, "\n        ");
            fprintf(out, "%4d%s", cs->dfa->states[s].trans[b],
                    (b + 1 < 256) ? "," : "");
        }
        fprintf(out, "\n    }%s\n", (s + 1 < ns) ? "," : "");
    }
    fprintf(out, "};\n");

    /* accept[N] = global rule id or -1. */
    fprintf(out, "static const short %s_dfa_%s_accept[%d] = {",
            prefix, upper_state, ns);
    for (int s = 0; s < ns; s++) {
        if (s % 12 == 0) fprintf(out, "\n    ");
        int local_rule = cs->dfa->states[s].is_accept
            ? cs->dfa->states[s].accept_rule : -1;
        int global_rule = -1;
        if (local_rule >= 0 && local_rule < cs->n_rules) {
            global_rule = cs->rule_indices[local_rule];
        }
        fprintf(out, "%4d%s", global_rule,
                (s + 1 < ns) ? "," : "");
    }
    fprintf(out, "\n};\n\n");

    free(upper_state);
    (void)compiled_state_index;   /* reserved for future state-id use */
}

int lime_lex_emit_c(const LimeLexCompiled *c,
                    const LimeLexSpec *spec,
                    const char *name_prefix,
                    const char *header_basename,
                    const char *const *rule_names,
                    int n_rules,
                    FILE *out) {
    if (!c || !out) return -1;
    const char *prefix = eff_prefix(name_prefix);
    char *PREFIX = upper_prefix(name_prefix);
    if (!PREFIX) return -1;

    fprintf(out, "/* Generated by lime -X.  DO NOT EDIT. */\n");
    if (header_basename && *header_basename) {
        fprintf(out, "#include \"%s\"\n", header_basename);
    }
    fprintf(out, "#include <stddef.h>\n\n");

    /* Rule-name table. */
    fprintf(out, "const char *const %sRuleNames[] = {\n", prefix);
    for (int i = 0; i < n_rules; i++) {
        fprintf(out, "    \"%s\"%s\n", rule_names[i],
                (i + 1 < n_rules) ? "," : "");
    }
    if (n_rules == 0) fprintf(out, "    /* no rules */\n");
    fprintf(out, "    %s NULL\n", n_rules > 0 ? "," : "");
    fprintf(out, "};\n\n");

    /* DFA tables per state. */
    for (int i = 0; i < c->n_states; i++) {
        emit_state_tables(&c->states[i], i, prefix, out);
    }

    /* Match function: longest-match driver. */
    fprintf(out, "int %s_match(int state, const char *bytes, size_t n,\n",
            prefix);
    fprintf(out, "             int *out_rule, size_t *out_consumed) {\n");
    fprintf(out, "    int start;\n");
    fprintf(out, "    int last_accept_rule = -1;\n");
    fprintf(out, "    size_t last_accept_pos = 0;\n");
    fprintf(out, "    size_t i;\n");
    fprintf(out, "    int s;\n");

    fprintf(out, "    switch (state) {\n");
    for (int i = 0; i < c->n_states; i++) {
        char *upper_state = upper_dup(c->states[i].state_name);
        fprintf(out, "    case %d: start = %s_dfa_%s_start; break;\n",
                i, prefix, upper_state);
        free(upper_state);
    }
    fprintf(out, "    default: return 0;\n");
    fprintf(out, "    }\n");
    fprintf(out, "    s = start;\n\n");

    /* Check immediate accept: state's start could itself be
    ** accepting (e.g. a*  matches empty). */
    fprintf(out, "    /* Check accept at position 0 (empty match). */\n");
    fprintf(out, "    switch (state) {\n");
    for (int i = 0; i < c->n_states; i++) {
        char *upper_state = upper_dup(c->states[i].state_name);
        fprintf(out, "    case %d: if (%s_dfa_%s_accept[s] >= 0) {\n",
                i, prefix, upper_state);
        fprintf(out, "        last_accept_rule = %s_dfa_%s_accept[s];\n",
                prefix, upper_state);
        fprintf(out, "        last_accept_pos = 0;\n");
        fprintf(out, "    } break;\n");
        free(upper_state);
    }
    fprintf(out, "    }\n\n");

    fprintf(out, "    for (i = 0; i < n; i++) {\n");
    fprintf(out, "        int next;\n");
    fprintf(out, "        switch (state) {\n");
    for (int i = 0; i < c->n_states; i++) {
        char *upper_state = upper_dup(c->states[i].state_name);
        fprintf(out, "        case %d:\n", i);
        fprintf(out, "            next = %s_dfa_%s_trans[s][(unsigned char)bytes[i]];\n",
                prefix, upper_state);
        fprintf(out, "            break;\n");
        free(upper_state);
    }
    fprintf(out, "        default: return 0;\n");
    fprintf(out, "        }\n");
    fprintf(out, "        if (next < 0) break;\n");
    fprintf(out, "        s = next;\n");
    fprintf(out, "        switch (state) {\n");
    for (int i = 0; i < c->n_states; i++) {
        char *upper_state = upper_dup(c->states[i].state_name);
        fprintf(out, "        case %d:\n", i);
        fprintf(out, "            if (%s_dfa_%s_accept[s] >= 0) {\n",
                prefix, upper_state);
        fprintf(out, "                last_accept_rule = %s_dfa_%s_accept[s];\n",
                prefix, upper_state);
        fprintf(out, "                last_accept_pos = i + 1;\n");
        fprintf(out, "            }\n");
        fprintf(out, "            break;\n");
        free(upper_state);
    }
    fprintf(out, "        }\n");
    fprintf(out, "    }\n\n");

    fprintf(out, "    if (last_accept_rule < 0) return 0;\n");
    fprintf(out, "    *out_rule = last_accept_rule;\n");
    fprintf(out, "    *out_consumed = last_accept_pos;\n");
    fprintf(out, "    return 1;\n");
    fprintf(out, "}\n\n");

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
        "    } stack[%s_LEX_MAX_INCLUDE_DEPTH];\n"
        "};\n\n",
        prefix, PREFIX);
    fprintf(out,
        "%sLexer *%sLexAlloc(void *(*mallocProc)(size_t)) {\n"
        "    if (!mallocProc) return 0;\n"
        "    %sLexer *yyl = (%sLexer*)mallocProc(sizeof(*yyl));\n"
        "    if (!yyl) return 0;\n"
        "    yyl->state = 0;          /* INITIAL */\n"
        "    yyl->err_msg = 0;\n"
        "    yyl->depth = 0;\n"
        "    return yyl;\n"
        "}\n\n",
        prefix, prefix, prefix, prefix);
    fprintf(out,
        "void %sLexFree(%sLexer *yyl, void (*freeProc)(void *)) {\n"
        "    if (yyl && freeProc) freeProc(yyl);\n"
        "}\n\n",
        prefix, prefix);

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
        if (collect_rule_actions(spec, n_rules,
                                 &rule_actions, &rule_is_eof) == 0) {
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
            free(rule_actions); free(rule_is_eof);
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
        "#define LEX_PUSHBACK(n) (void)%sLexPushback(lex, (size_t)(n))\n\n",
        prefix, prefix);

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
            fprintf(out, " %d%s", state_eof_rule[si],
                    (si + 1 < c->n_states) ? "," : "");
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
    fprintf(out,
        "%sLexResult %sLexFeedBytes(%sLexer *yyl,\n"
        "                              const char *bytes, size_t n,\n"
        "                              %sEmitFn emit, void *user) {\n"
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
        "    while (yyl->depth > initial_depth) {\n"
        "        int top = yyl->depth - 1;\n"
        "        size_t fpos = yyl->stack[top].pos;\n"
        "        size_t flen = yyl->stack[top].len;\n"
        "        if (fpos >= flen) {\n",
        prefix, prefix, prefix, prefix,
        PREFIX, PREFIX, PREFIX);

    /* M3.6: EOF-rule dispatch inside the auto-pop branch.  When
    ** the top-of-stack frame is exhausted, look up the EOF rule
    ** for the current lexer state and run its action body before
    ** unwinding the frame.  An EOF action body sees matched="",
    ** matched_len=0, and the same lex/state/_action_emitted
    ** locals as a normal-rule body, so LEX_EMIT/LEX_TRANSITION/
    ** LEX_ERROR_AT/LEX_TERMINATE all work.  No auto-emit on
    ** EOF: the rule's body is the source of truth. */
    if (any_eof) {
        fprintf(out,
            "            /* M3.6: fire <<EOF>> rule for the current state. */\n"
            "            int _eof_rule = -1;\n"
            "            if (yyl->state >= 0 && yyl->state < %d) {\n"
            "                _eof_rule = %s_eof_rule[yyl->state];\n"
            "            }\n"
            "            if (_eof_rule >= 0) {\n"
            "                const char *matched = \"\";\n"
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
            "                (void)matched; (void)matched_len;\n"
            "                (void)_action_emitted;\n"
            "                if (_error) return %s_LEX_ERROR;\n"
            "                if (_terminate) return %s_LEX_OK;\n"
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
        "            return %s_LEX_ERROR;\n"
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
        "        if (_error) return %s_LEX_ERROR;\n"
        "        if (_terminate) return %s_LEX_OK;\n"
        "    }\n"
        "    return %s_LEX_OK;\n"
        "}\n\n",
        PREFIX, PREFIX, PREFIX);

    fprintf(out,
        "#undef LEX_EMIT\n"
        "#undef LEX_SKIP\n"
        "#undef LEX_TRANSITION\n"
        "#undef LEX_TERMINATE\n"
        "#undef LEX_ERROR_AT\n"
        "#undef LEX_PUSHBACK\n\n");

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
        fprintf(out,
            "\n/* ===== Per-rule test entry points (M4.3) ===== */\n");
        if (emit_test_wrappers(c, spec, prefix, PREFIX, 0, out) != 0) {
            free(PREFIX);
            return -1;
        }
    }

    free(PREFIX);
    return ferror(out) ? -1 : 0;
}
