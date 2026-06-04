/*
** src/emit_rust.c -- Rust output target for the Lime parser
** generator.  See docs/RUST_OUTPUT.md for the design.
**
** v0.8 status: stages 2 + 3 + 4 of 9.  Action tables, reduce
** callback dispatch, and the LALR runtime body all real.  Action
** body translation is literal-copy with $$/$N substitution; bodies
** that don't translate emit `unimplemented!()` with a TODO comment
** (user adds %rust_action in a future commit, or hand-edits the
** generated .rs).
**
** What this file emits
** --------------------
**
** A self-contained Rust module in `<basename>.rs`:
**
**   * Header with provenance + #![allow(...)] crate attrs
**   * pub const FIRST_TOKEN + token codes
**   * NSTATE / NRULE / NTERMINAL / NSYMBOL constants
**   * YY_MAX_SHIFT / YY_MIN_SHIFTREDUCE / etc. dispatch range
**     constants matching the C output
**   * YY_ACTION / YY_LOOKAHEAD / YY_SHIFT_OFST / YY_REDUCE_OFST /
**     YY_DEFAULT / YY_RULE_LHS / YY_RULE_NRHS const slices
**   * YY_FALLBACK slice when %fallback declared
**   * fn yy_rule_N(...) per rule, with action body literal-copied
**     and $$/$N substituted to slot variables
**   * pub static YY_RULE_REDUCE_FN: [fn(...); NRULE] dispatch table
**   * pub struct <Name>Parser + impl with new/push/finalize
**   * The LALR loop body in push() ports parse_engine.c::parse_token
**
** Code is no_std-compatible for the const-data tables; the parser
** struct uses Vec from std (a no_std feature is a future commit).
*/

#include "snapshot.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

struct lime;
struct symbol;
struct rule;

/* Forward decls of the bridge helpers in lime.c */

extern int  lime_emit_rust_get_nstate(const struct lime *lemp);
extern int  lime_emit_rust_get_nrule(const struct lime *lemp);
extern int  lime_emit_rust_get_nterminal(const struct lime *lemp);
extern int  lime_emit_rust_get_nsymbol(const struct lime *lemp);
extern int  lime_emit_rust_get_first_token(const struct lime *lemp);
extern const char *lime_emit_rust_get_name(const struct lime *lemp);
extern struct symbol *lime_emit_rust_symbol_at(const struct lime *lemp, int i);
extern const char *lime_emit_rust_symbol_name(const struct symbol *sp);

/* The big table struct + assembly + accessors come from lime.c.
** Mirror the layout here so we can read it.  Keep in sync with
** lime.c's `typedef struct LimeRustTables`. */
typedef struct LimeRustTables {
    uint16_t *yy_action;
    uint32_t  yy_action_count;
    uint16_t *yy_lookahead;
    uint32_t  yy_lookahead_count;
    int32_t  *yy_shift_ofst;
    int32_t  *yy_reduce_ofst;
    uint16_t *yy_default;
    uint32_t  nstate;
    int16_t  *yy_rule_lhs;
    int8_t   *yy_rule_nrhs;
    uint32_t  nrule;
    uint16_t *yy_fallback;
    uint32_t  nfallback;
    uint16_t  yy_max_shift;
    uint16_t  yy_min_shiftreduce;
    uint16_t  yy_max_shiftreduce;
    uint16_t  yy_error_action;
    uint16_t  yy_accept_action;
    uint16_t  yy_no_action;
    uint16_t  yy_min_reduce;
    uint32_t  nsymbol;
    uint32_t  nterminal;
    uint16_t  ntoken;
    uint16_t  first_token;
} LimeRustTables;

extern int  lime_emit_rust_assemble_tables(struct lime *lemp, LimeRustTables *out, char **error);
extern void lime_emit_rust_free_tables(LimeRustTables *t);
extern int  lime_emit_rust_rule_count(const struct lime *lemp);
extern int  lime_emit_rust_rule_info(const struct lime *lemp, int iRule,
                                     int *out_lhs_index, int *out_nrhs,
                                     const char **out_code,
                                     const char **out_lhs_alias,
                                     int *out_line, int *out_no_code);
extern int  lime_emit_rust_rule_rhs(const struct lime *lemp, int iRule, int i,
                                    const char **out_rhs_alias,
                                    const char **out_rhs_name);
extern const char *lime_emit_rust_rule_rust_code(const struct lime *lemp, int iRule);
extern const char *lime_emit_rust_get_rust_arg(const struct lime *lemp);
extern const char *lime_emit_rust_get_rust_value_type(const struct lime *lemp);
extern const char *lime_emit_rust_get_rust_error(const struct lime *lemp);
extern const char *lime_emit_rust_get_rust_accept(const struct lime *lemp);
extern const char *lime_emit_rust_get_rust_failure(const struct lime *lemp);
extern const char *lime_emit_rust_get_rust_overflow(const struct lime *lemp);

/* Per-rule action body classifier (see src/jit_inline.c).  Returns
** true when the action body is small/pure enough that the host
** compiler can safely inline it; returns false for bodies that
** contain function calls, control flow, or are otherwise too large
** to inline.
**
** For the Rust output we use this to emit `#[inline(always)]` on
** the corresponding yy_rule_N reducer so rustc inlines it into the
** parse loop.  The classifier itself has no LLVM dependency, so it
** is available even when LIME_NO_JIT is set. */
extern bool jit_can_inline_rule_text(const char *code, int no_code);

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

static void emit_section(FILE *out, const char *title) {
    fprintf(out, "\n// ============================================"
                 "==========================\n");
    fprintf(out, "// %s\n", title);
    fprintf(out, "// ============================================"
                 "==========================\n\n");
}

extern int g_lime_rust_no_std;

static void emit_header(FILE *out, const struct lime *lemp,
                        const char *grammar_path) {
    const char *name = lime_emit_rust_get_name(lemp);
    fprintf(out,
            "// SPDX-License-Identifier: <inherited from input grammar>\n"
            "//\n"
            "// Generated by lime -- DO NOT EDIT BY HAND.\n"
            "//\n"
            "// Source grammar: %s\n"
            "// Parser name:    %s\n"
            "// To regenerate:  lime --rust %s\n"
            "//\n"
            "\n"
            "#![allow(dead_code)]\n"
            "#![allow(unused_assignments)]\n"
            "#![allow(unused_variables)]\n"
            "#![allow(unused_mut)]\n"
            "#![allow(unused_parens)]\n"
            "#![allow(non_snake_case)]\n"
            "#![allow(non_upper_case_globals)]\n"
            "#![allow(clippy::all)]\n",
            grammar_path ? grammar_path : "(unknown)",
            name ? name : "Parse",
            grammar_path ? grammar_path : "<grammar>");
    if (g_lime_rust_no_std) {
        fprintf(out, "// --rustnostd: parser.rs uses alloc::vec::Vec.\n");
        fprintf(out, "// User's lib.rs / main.rs should declare #![no_std]\n");
        fprintf(out, "// at the crate root if needed.\n");
        fprintf(out, "extern crate alloc;\n");
        fprintf(out, "use alloc::vec::Vec;\n\n");
    }
}

static void emit_token_constants(FILE *out, const struct lime *lemp) {
    emit_section(out, "Token codes (external = internal + FIRST_TOKEN)");
    int nterm = lime_emit_rust_get_nterminal(lemp);
    int first_token = lime_emit_rust_get_first_token(lemp);
    fprintf(out, "pub const FIRST_TOKEN: u16 = %d;\n\n", first_token);
    /* Skip slot 0 (lemon's $end / EOF). */
    for (int i = 1; i < nterm; i++) {
        struct symbol *sp = lime_emit_rust_symbol_at(lemp, i);
        if (!sp) continue;
        const char *name = lime_emit_rust_symbol_name(sp);
        if (!name || name[0] == '$') continue;
        /* Skip identifiers that aren't valid Rust idents.  Lemon
        ** allows token names with non-Rust-friendly chars in rare
        ** cases (e.g. '{default}'); emit them as a string-named
        ** constant comment instead so the rest of the output still
        ** compiles. */
        int ok = 1;
        for (const char *p = name; *p; p++) {
            if (!isalnum((unsigned char)*p) && *p != '_') { ok = 0; break; }
        }
        if (!ok) {
            fprintf(out, "// non-rust-ident token: \"%s\" = %d\n",
                    name, i + first_token);
            continue;
        }
        fprintf(out, "pub const %s: u16 = %d;\n", name, i + first_token);
    }
    fprintf(out, "\n");
}

static void emit_dispatch_constants(FILE *out, const LimeRustTables *t) {
    emit_section(out, "Dispatch range constants");
    fprintf(out, "pub const NSTATE:    u32 = %u;\n", t->nstate);
    fprintf(out, "pub const NRULE:     u32 = %u;\n", t->nrule);
    fprintf(out, "pub const NTERMINAL: u32 = %u;\n", t->nterminal);
    fprintf(out, "pub const NSYMBOL:   u32 = %u;\n", t->nsymbol);
    fprintf(out, "pub const NTOKEN:    u16 = %u;\n", t->ntoken);
    fprintf(out, "pub const YY_MAX_SHIFT:        u16 = %u;\n", t->yy_max_shift);
    fprintf(out, "pub const YY_MIN_SHIFTREDUCE:  u16 = %u;\n", t->yy_min_shiftreduce);
    fprintf(out, "pub const YY_MAX_SHIFTREDUCE:  u16 = %u;\n", t->yy_max_shiftreduce);
    fprintf(out, "pub const YY_ERROR_ACTION:     u16 = %u;\n", t->yy_error_action);
    fprintf(out, "pub const YY_ACCEPT_ACTION:    u16 = %u;\n", t->yy_accept_action);
    fprintf(out, "pub const YY_NO_ACTION:        u16 = %u;\n", t->yy_no_action);
    fprintf(out, "pub const YY_MIN_REDUCE:       u16 = %u;\n", t->yy_min_reduce);
    fprintf(out, "\n");
}

static void emit_u16_array(FILE *out, const char *name,
                           const uint16_t *arr, uint32_t n) {
    fprintf(out, "pub static %s: &[u16] = &[", name);
    if (arr == NULL || n == 0) { fprintf(out, "];\n"); return; }
    fprintf(out, "\n   ");
    for (uint32_t i = 0; i < n; i++) {
        fprintf(out, " %5u,", (unsigned)arr[i]);
        if ((i + 1) % 10 == 0) fprintf(out, "\n   ");
    }
    fprintf(out, "\n];\n");
}

static void emit_i32_array(FILE *out, const char *name,
                           const int32_t *arr, uint32_t n) {
    fprintf(out, "pub static %s: &[i32] = &[", name);
    if (arr == NULL || n == 0) { fprintf(out, "];\n"); return; }
    fprintf(out, "\n   ");
    for (uint32_t i = 0; i < n; i++) {
        fprintf(out, " %6d,", (int)arr[i]);
        if ((i + 1) % 10 == 0) fprintf(out, "\n   ");
    }
    fprintf(out, "\n];\n");
}

static void emit_i16_array(FILE *out, const char *name,
                           const int16_t *arr, uint32_t n) {
    fprintf(out, "pub static %s: &[i16] = &[", name);
    if (arr == NULL || n == 0) { fprintf(out, "];\n"); return; }
    fprintf(out, "\n   ");
    for (uint32_t i = 0; i < n; i++) {
        fprintf(out, " %5d,", (int)arr[i]);
        if ((i + 1) % 12 == 0) fprintf(out, "\n   ");
    }
    fprintf(out, "\n];\n");
}

static void emit_i8_array(FILE *out, const char *name,
                          const int8_t *arr, uint32_t n) {
    fprintf(out, "pub static %s: &[i8] = &[", name);
    if (arr == NULL || n == 0) { fprintf(out, "];\n"); return; }
    fprintf(out, "\n   ");
    for (uint32_t i = 0; i < n; i++) {
        fprintf(out, " %4d,", (int)arr[i]);
        if ((i + 1) % 16 == 0) fprintf(out, "\n   ");
    }
    fprintf(out, "\n];\n");
}

static void emit_action_tables(FILE *out, const LimeRustTables *t) {
    emit_section(out, "Action tables");
    emit_u16_array(out, "YY_ACTION",     t->yy_action,    t->yy_action_count);
    emit_u16_array(out, "YY_LOOKAHEAD",  t->yy_lookahead, t->yy_lookahead_count);
    emit_i32_array(out, "YY_SHIFT_OFST", t->yy_shift_ofst, t->nstate);
    emit_i32_array(out, "YY_REDUCE_OFST",t->yy_reduce_ofst,t->nstate);
    emit_u16_array(out, "YY_DEFAULT",    t->yy_default,   t->nstate);
    emit_i16_array(out, "YY_RULE_LHS",   t->yy_rule_lhs,  t->nrule);
    emit_i8_array (out, "YY_RULE_NRHS",  t->yy_rule_nrhs, t->nrule);
    if (t->yy_fallback != NULL && t->nfallback > 0) {
        fprintf(out, "\npub const HAS_FALLBACK: bool = true;\n");
        emit_u16_array(out, "YY_FALLBACK", t->yy_fallback, t->nfallback);
    } else {
        fprintf(out, "\npub const HAS_FALLBACK: bool = false;\n");
        fprintf(out, "pub static YY_FALLBACK: &[u16] = &[];\n");
    }
    fprintf(out, "\n");
}

/* ------------------------------------------------------------------ */
/*  Reduce callbacks                                                   */
/* ------------------------------------------------------------------ */

/*
** Substitute lemon-style $$/$N references in a user-provided action
** body with Rust slot variables `lhs` and `rhs0`/`rhs1`/...  Writes
** the result to `out`.  Best-effort: the substitution is purely
** lexical, doesn't try to understand C operators or expressions.
**
** v0.8.0 expectation: the user writes action bodies that are valid
** Rust (or at least valid C-syntax-compatible Rust subset --
** arithmetic, assignments, simple expressions).  Bodies that call
** C functions or use C-specific syntax produce uncompilable Rust;
** the user adds a `%rust_action { ... }` directive (lands in a
** subsequent commit on this branch) to override per-rule.
*/
static void emit_action_body_substituted(FILE *out, const char *code,
                                          int nrhs,
                                          const char *lhs_alias,
                                          const struct lime *lemp,
                                          int iRule) {
    /* The body span we receive starts AT the byte AFTER `{`.  For
    ** the conventional multi-line form
    **
    **      stmt(A) ::= ... . {
    **          A = B + C;
    **      }
    **
    ** code[0] is `\n`, NOT the first character of the action.
    ** Pre-v0.12 we bailed on (code[0]=='\n') and emitted
    ** `// empty action`, silently dropping every multi-line body.
    ** Reported in /tmp/lime-rust-target-repro/README.md (Issue 1).
    **
    ** Walk past leading whitespace (space/tab/CR/LF) and only
    ** treat the body as empty when nothing non-whitespace remains.
    ** This matches the lint_is_trivial_action() empty-check on the
    ** C-emitter side. */
    if (!code) {
        fprintf(out, "    // empty action\n");
        return;
    }
    {
        const char *q = code;
        while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') q++;
        if (*q == 0) {
            fprintf(out, "    // empty action\n");
            return;
        }
    }
    const char *lhs_name = (lhs_alias && lhs_alias[0]) ? lhs_alias : "lhs";
    fprintf(out, "    // user action body (copied verbatim with $$/$N substitution; "
                 "override per-rule with %%action_rust { ... } if you need "
                 "different code on the C and Rust targets)\n");
    /* Indent two spaces beyond "    " (call site is already indented 4). */
    fprintf(out, "    ");
    for (const char *p = code; *p; p++) {
        if (*p == '$') {
            if (p[1] == '$') {
                fprintf(out, "%s", lhs_name);
                p++;
                continue;
            }
            if (isdigit((unsigned char)p[1])) {
                /* $N -- find which RHS slot.  Lemon $1 .. $N maps to
                ** rhs0 .. rhs(N-1) (Copy values, not refs). */
                int n = 0;
                p++;
                while (isdigit((unsigned char)*p)) {
                    n = n * 10 + (*p - '0');
                    p++;
                }
                if (n >= 1 && n <= nrhs) {
                    fprintf(out, "rhs%d", n - 1);
                } else {
                    /* Out-of-range $N -- emit verbatim as a hint to
                    ** the user that something's off in their grammar. */
                    fprintf(out, "/* $%d (out of range) */", n);
                }
                p--; /* loop ++ rewinds */
                continue;
            }
        }
        fputc(*p, out);
        if (*p == '\n') fprintf(out, "    ");
    }
    fprintf(out, "\n");
    (void)lhs_alias;
    (void)lemp;
    (void)iRule;
}

static void emit_reduce_callbacks(FILE *out, struct lime *lemp,
                                  const LimeRustTables *t) {
    emit_section(out, "Per-rule reduce callbacks");

    /* Emit UserArg type before ReduceCtx so it's in scope. */
    const char *rust_arg = lime_emit_rust_get_rust_arg(lemp);
    if (rust_arg && rust_arg[0]) {
        fprintf(out, "/// %%rust_extra_argument type.\n");
        fprintf(out, "pub type UserArg = %s;\n\n", rust_arg);
    } else {
        fprintf(out, "pub type UserArg = ();\n\n");
    }

    fprintf(out,
            "/// Context passed to each rule's reduce callback.\n"
            "/// Rules read RHS values via the `rhs` slice, write LHS via\n"
            "/// `lhs`, and access the user arg (if any) via `user`.\n"
            "pub struct ReduceCtx<'a> {\n"
            "    pub lhs:  &'a mut Value,\n"
            "    pub rhs:  &'a mut [Value],\n"
            "    pub user: &'a mut UserArg,\n"
            "}\n\n");

    /* %rust_value_type override; defaults to i64 when absent. */
    const char *rust_value_type = lime_emit_rust_get_rust_value_type(lemp);
    if (rust_value_type && rust_value_type[0]) {
        fprintf(out, "/// Semantic Value type (from %%rust_value_type).\n");
        fprintf(out, "pub type Value = %s;\n\n", rust_value_type);
    } else {
        fprintf(out,
                "/// Default semantic Value type.  Override via\n"
                "/// %%rust_value_type {T} in the grammar to use a\n"
                "/// custom type (String, struct, Box<dyn Any>, etc.).\n"
                "pub type Value = i64;\n\n");
    }

    int nrule = t->nrule;
    for (int r = 0; r < nrule; r++) {
        int lhs_index = -1, nrhs = 0, line = 0, no_code = 1;
        const char *code = NULL;
        const char *lhs_alias = NULL;
        if (lime_emit_rust_rule_info(lemp, r, &lhs_index, &nrhs, &code,
                                     &lhs_alias, &line, &no_code) != 0) {
            continue;
        }
        /* Mark the reducer #[inline(always)] when the action body
        ** is classified as inlinable -- empty, passthrough, or a
        ** single small expression with no calls/control flow.
        ** Rustc honours the hint aggressively, which lets the
        ** parse loop's match arm collapse into the action body
        ** for trivial reducers (the common case in lemon-style
        ** grammars).  See docs/RUST_OUTPUT.md for the impact on
        ** typical grammars.
        **
        ** When the user supplies a per-rule %rust_action override,
        ** trust the user's body verbatim: we don't see what's in
        ** it (no $-substitution, no classification), so default to
        ** not annotating and let rustc decide. */
        const char *rust_override_for_inline =
            lime_emit_rust_rule_rust_code(lemp, r);
        bool inlinable =
            (rust_override_for_inline == NULL || rust_override_for_inline[0] == '\0')
            && jit_can_inline_rule_text(code, no_code);
        fprintf(out, "/// Rule %d: nrhs=%d, lhs symbol index=%d (line %d)\n",
                r, nrhs, lhs_index, line);
        if (inlinable) {
            fprintf(out, "#[inline(always)]\n");
        }
        fprintf(out, "fn yy_rule_%d(ctx: &mut ReduceCtx) {\n", r);
        /* Rebind RHS slots to local mutable refs for the action body. */
        /* Bind slots by alias when the grammar declared one
        ** (lemon's `lhs(A) ::= rhs(B) ...` syntax); fall back to
        ** lhs / rhsN otherwise.  The action body refers to whichever
        ** name was declared, so renaming the binding is the cleanest
        ** way to support alias-driven user action bodies without a
        ** full identifier-rewriting pass. */
        const char *lhs_name = (lhs_alias && lhs_alias[0]) ? lhs_alias : "lhs";
        /* LHS bound as a local mutable scalar (matches C semantics:
        ** the user writes `A = expr;` where A is the value).  We
        ** write it back to ctx.lhs at the end of the function so
        ** the parent reduce sees the new value. */
        fprintf(out, "    let mut %s: Value = Value::default();\n", lhs_name);
        for (int i = 0; i < nrhs; i++) {
            const char *rhs_alias = NULL;
            const char *rhs_name = NULL;
            (void)lime_emit_rust_rule_rhs(lemp, r, i, &rhs_alias, &rhs_name);
            const char *binding = (rhs_alias && rhs_alias[0]) ? rhs_alias
                                                              : NULL;
            char buf[64];
            if (!binding) {
                snprintf(buf, sizeof(buf), "rhs%d", i);
                binding = buf;
            }
            /* Underscore-prefix when the name doesn't appear in the
            ** action body to suppress unused-variable warnings.  Cheap
            ** lexical check.  Fallback names always get the underscore
            ** since they're slot defaults that the user didn't ask for. */
            int used = 0;
            /* Default action emits `lhs = rhs0;` when no user body
            ** is present and nrhs > 0; that consumes the rhs0
            ** binding even though no user-written text references it. */
            if ((no_code || !code) && i == 0 && nrhs > 0) {
                used = 1;
            }
            if (!used && code) {
                size_t blen = strlen(binding);
                for (const char *p = code; (p = strstr(p, binding)); ) {
                    int prev_ok = (p == code) || !(isalnum((unsigned char)p[-1]) || p[-1] == '_');
                    int next_ok = !(isalnum((unsigned char)p[blen]) || p[blen] == '_');
                    if (prev_ok && next_ok) { used = 1; break; }
                    p++;
                }
            }
            fprintf(out, "    let %s%s: Value = ctx.rhs[%d].clone();\n",
                    used ? "" : "_",
                    binding, i);
        }
        /* feat/rust-output: prefer the per-rule %rust_action body
        ** when present.  Emit verbatim (no $-substitution; user is
        ** expected to write actual Rust referencing the bound aliases
        ** or the fallback lhs/rhsN bindings). */
        const char *rust_override = lime_emit_rust_rule_rust_code(lemp, r);
        if (rust_override && rust_override[0]) {
            fprintf(out, "    // user %%rust_action body (verbatim)\n");
            fprintf(out, "    %s\n", rust_override);
            fprintf(out, "    *ctx.lhs = %s;\n", lhs_name);
            fprintf(out, "}\n\n");
            continue;
        }
        if (no_code || !code) {
            if (nrhs > 0) {
                /* Default action: $$ = $1.  rhs0 may have been
                ** underscore-prefixed; recompute the binding name
                ** for the assignment. */
                const char *r0_alias = NULL, *r0_name = NULL;
                (void)lime_emit_rust_rule_rhs(lemp, r, 0, &r0_alias, &r0_name);
                const char *r0_binding = (r0_alias && r0_alias[0]) ? r0_alias : "rhs0";
                fprintf(out, "    %s = %s%s;  // default: $$ = $1\n",
                        lhs_name, r0_binding[0] == '_' ? "" : "", r0_binding);
            } else {
                fprintf(out, "    // no RHS, no body: lhs unchanged\n");
            }
        } else {
            emit_action_body_substituted(out, code, nrhs, lhs_alias, lemp, r);
        }
        /* Write LHS back through ctx.lhs so the reducer's parent
        ** sees the value the action body computed. */
        fprintf(out, "    *ctx.lhs = %s;\n", lhs_name);
        fprintf(out, "}\n\n");
    }

    /* Dispatch table */
    fprintf(out, "/// Dispatch table: ruleno -> reduce-callback function.\n");
    fprintf(out, "pub static YY_RULE_REDUCE_FN: [fn(&mut ReduceCtx); NRULE as usize] = [\n");
    for (int r = 0; r < nrule; r++) {
        fprintf(out, "    yy_rule_%d,\n", r);
    }
    fprintf(out, "];\n");
}

/* ------------------------------------------------------------------ */
/*  Parser struct + LALR runtime                                       */
/* ------------------------------------------------------------------ */

static void emit_parser_runtime(FILE *out, const struct lime *lemp) {
    const char *name = lime_emit_rust_get_name(lemp);
    if (!name) name = "Parse";
    const char *rust_arg = lime_emit_rust_get_rust_arg(lemp);
    /* Thread user-arg through the parser.  When grammar declares
    ** %rust_extra_argument {T}, T becomes UserArg.  Otherwise UserArg = ().
    ** Action bodies read/write via ctx.user.  Parser struct stores a
    ** mutable user value the caller injects via push_with_user(). */
    int has_user_arg = (rust_arg && rust_arg[0]);

    emit_section(out, "Parser API + LALR runtime");

    /* UserArg type already emitted by emit_reduce_callbacks. */
    (void)rust_arg; (void)has_user_arg;

    /* Error type. */
    fprintf(out,
            "#[derive(Debug, Clone, PartialEq, Eq)]\n"
            "pub enum ParseError {\n"
            "    /// Token had no shift action at the current state.\n"
            "    SyntaxError { token: u16, state: u16 },\n"
            "    /// External token was outside [FIRST_TOKEN, FIRST_TOKEN+NTOKEN).\n"
            "    OutOfRange { token: u16 },\n"
            "    /// Reduce action attempted on rule index >= NRULE.\n"
            "    BadReduce { rule: u32 },\n"
            "    /// Stack underflow during reduce (table corruption).\n"
            "    StackUnderflow,\n"
            "}\n\n");

    /* Stack frame. */
    fprintf(out,
            "#[derive(Debug, Clone, Default)]\n"
            "struct Frame {\n"
            "    state: u16,\n"
            "    major: u16,\n"
            "    value: Value,\n"
            "}\n\n");

    /* Parser struct + impl. */
    fprintf(out, "/// %s parser.\n", name);
    fprintf(out, "pub struct %sParser {\n", name);
    fprintf(out, "    stack: Vec<Frame>,\n");
    fprintf(out, "    accepted: bool,\n");
    fprintf(out, "    errored: bool,\n");
    fprintf(out, "    pub final_value: Value,\n");
    fprintf(out, "    /// User argument threaded into ReduceCtx for\n");
    fprintf(out, "    /// every reduce callback.  Populated from\n");
    fprintf(out, "    /// %%rust_extra_argument; () when unset.\n");
    fprintf(out, "    pub user: UserArg,\n");
    fprintf(out, "    /// Scratch buffer for rhs values, reused across\n");
    fprintf(out, "    /// reduces.  Avoids per-reduce Vec allocation\n");
    fprintf(out, "    /// in the LALR loop hot path.\n");
    fprintf(out, "    rhs_scratch: Vec<Value>,\n");
    fprintf(out, "}\n\n");

    fprintf(out, "impl %sParser {\n", name);
    /* new */
    fprintf(out,
            "    /// Construct a parser with a user-supplied UserArg.\n"
            "    /// When UserArg is () use new() instead.\n"
            "    pub fn new_with_user(user: UserArg) -> Self {\n"
            "        let mut stack = Vec::with_capacity(64);\n"
            "        stack.push(Frame::default());\n"
            "        Self {\n"
            "            stack, accepted: false, errored: false,\n"
            "            final_value: Value::default(),\n"
            "            user,\n"
            "            rhs_scratch: Vec::with_capacity(16),\n"
            "        }\n"
            "    }\n\n"
            "    /// Construct a fresh parser at state 0 with the\n"
            "    /// UserArg's Default value.\n"
            "    pub fn new() -> Self where UserArg: Default {\n"
            "        Self::new_with_user(UserArg::default())\n"
            "    }\n\n");

    /* push -- the LALR loop. */
    fprintf(out,
            "    /// Feed an external token code (with semantic value).\n"
            "    /// Use 0 as the token code to indicate end-of-input.\n"
            "    /// Returns Ok on accept-pending or accept; Err on syntax\n"
            "    /// error or unrecoverable state corruption.\n"
            "    pub fn push(&mut self, token_code: u16, value: Value)\n"
            "        -> Result<bool, ParseError>\n"
            "    {\n"
            "        if self.accepted { return Ok(true); }\n"
            "        if self.errored  { return Err(ParseError::SyntaxError {\n"
            "            token: token_code,\n"
            "            state: self.top_state(),\n"
            "        }); }\n\n"
            "        // Convert external -> internal: external == internal + FIRST_TOKEN.\n"
            "        // EOF (0) is preserved; out-of-range yields OutOfRange.\n"
            "        let mut major: i32 = if token_code == 0 {\n"
            "            0\n"
            "        } else {\n"
            "            (token_code as i32) - (FIRST_TOKEN as i32)\n"
            "        };\n"
            "        if major < 0 || major >= (NTOKEN as i32) {\n"
            "            self.errored = true;\n"
            "            return Err(ParseError::OutOfRange { token: token_code });\n"
            "        }\n\n"
            "        let mut retried_with_fallback = false;\n"
            "        loop {\n"
            "            // Top state.\n"
            "            let cur_state = self.top_state();\n"
            "            // Pending shift-reduce: cur_state encoded as a reduce.\n"
            "            if cur_state >= YY_MIN_REDUCE {\n"
            "                let ruleno = (cur_state - YY_MIN_REDUCE) as u32;\n"
            "                match self.reduce(ruleno)? {\n"
            "                    ReduceOutcome::Continue => continue,\n"
            "                    ReduceOutcome::Accept => {\n"
            "                        self.accepted = true;\n"
            "                        return Ok(true);\n"
            "                    }\n"
            "                }\n"
            "            }\n\n"
            "            let action = Self::find_shift_action(cur_state, major as u16);\n\n"
            "            // Plain shift: push (action, major) and consume.\n"
            "            if action <= YY_MAX_SHIFT {\n"
            "                if token_code == 0 {\n"
            "                    self.accepted = true;\n"
            "                    self.on_parse_accept();\n"
            "                    return Ok(true);\n"
            "                }\n"
            "                self.stack.push(Frame {\n"
            "                    state: action,\n"
            "                    major: major as u16,\n"
            "                    value,\n"
            "                });\n"
            "                return Ok(false);\n"
            "            }\n\n"
            "            // Shift-reduce: encode the action as a pending reduce\n"
            "            // (action + MIN_REDUCE - MIN_SHIFTREDUCE).  Next call\n"
            "            // sees the encoded state and falls into reduce.\n"
            "            if action <= YY_MAX_SHIFTREDUCE {\n"
            "                let encoded = action + (YY_MIN_REDUCE - YY_MIN_SHIFTREDUCE);\n"
            "                self.stack.push(Frame {\n"
            "                    state: encoded,\n"
            "                    major: major as u16,\n"
            "                    value,\n"
            "                });\n"
            "                return Ok(false);\n"
            "            }\n\n"
            "            if action == YY_ACCEPT_ACTION {\n"
            "                self.accepted = true;\n"
            "                self.on_parse_accept();\n"
            "                return Ok(true);\n"
            "            }\n\n"
            "            // Reduce.\n"
            "            if action >= YY_MIN_REDUCE\n"
            "                && action != YY_ERROR_ACTION\n"
            "                && action != YY_NO_ACTION\n"
            "            {\n"
            "                let ruleno = (action - YY_MIN_REDUCE) as u32;\n"
            "                match self.reduce(ruleno)? {\n"
            "                    ReduceOutcome::Continue => continue,\n"
            "                    ReduceOutcome::Accept => {\n"
            "                        self.accepted = true;\n"
            "                        return if token_code == 0 { Ok(true) } else { Ok(false) };\n"
            "                    }\n"
            "                }\n"
            "            }\n\n"
            "            // Try fallback once.\n"
            "            if HAS_FALLBACK\n"
            "                && !retried_with_fallback\n"
            "                && major >= 0\n"
            "                && (major as usize) < YY_FALLBACK.len()\n"
            "                && YY_FALLBACK[major as usize] != 0\n"
            "            {\n"
            "                major = YY_FALLBACK[major as usize] as i32;\n"
            "                retried_with_fallback = true;\n"
            "                continue;\n"
            "            }\n\n"
            "            // No action.\n"
            "            self.errored = true;\n"
            "            return Err(ParseError::SyntaxError {\n"
            "                token: token_code,\n"
            "                state: cur_state,\n"
            "            });\n"
            "        }\n"
            "    }\n\n");

    /* finalize. */
    fprintf(out,
            "    /// Feed end-of-input.  Equivalent to push(0, 0).\n"
            "    pub fn finalize(&mut self) -> Result<bool, ParseError> {\n"
            "        self.push(0, Value::default())\n"
            "    }\n\n");

    /* feat/rust-output: emit hook methods.  When grammar declares
    ** %rust_syntax_error / %rust_parse_accept / %rust_parse_failure
    ** / %rust_stack_overflow with a brace body, emit the body verbatim
    ** in a method on the parser; otherwise emit a no-op default. */
    const char *h_err = lime_emit_rust_get_rust_error(lemp);
    const char *h_acc = lime_emit_rust_get_rust_accept(lemp);
    const char *h_fail= lime_emit_rust_get_rust_failure(lemp);
    const char *h_ovf = lime_emit_rust_get_rust_overflow(lemp);

    fprintf(out,
        "    /// Hook fired on syntax error.  Override the body via\n"
        "    /// %%rust_syntax_error in the grammar; default is no-op.\n"
        "    pub fn on_syntax_error(&mut self, _token: u16, _state: u16) {\n"
        "        %s\n"
        "    }\n\n",
        (h_err && h_err[0]) ? h_err : "/* no-op */");
    fprintf(out,
        "    /// Hook fired on parse accept.  Override via\n"
        "    /// %%rust_parse_accept; default is no-op.\n"
        "    pub fn on_parse_accept(&mut self) {\n"
        "        %s\n"
        "    }\n\n",
        (h_acc && h_acc[0]) ? h_acc : "/* no-op */");
    fprintf(out,
        "    /// Hook fired on parse failure (post-error, no recovery).\n"
        "    /// Override via %%rust_parse_failure; default is no-op.\n"
        "    pub fn on_parse_failure(&mut self) {\n"
        "        %s\n"
        "    }\n\n",
        (h_fail && h_fail[0]) ? h_fail : "/* no-op */");
    fprintf(out,
        "    /// Hook fired on stack overflow.  In Rust the Vec stack\n"
        "    /// grows automatically; this is mostly informational.\n"
        "    /// Override via %%rust_stack_overflow; default is no-op.\n"
        "    pub fn on_stack_overflow(&mut self) {\n"
        "        %s\n"
        "    }\n\n",
        (h_ovf && h_ovf[0]) ? h_ovf : "/* no-op */");


    /* Helpers. */
    fprintf(out,
            "    #[inline(always)]\n"
            "    fn top_state(&self) -> u16 {\n"
            "        self.stack.last().map(|f| f.state).unwrap_or(0)\n"
            "    }\n\n"
            "    #[inline(always)]\n"
            "    fn find_shift_action(state: u16, lookahead: u16) -> u16 {\n"
            "        // state passthrough: states above YY_MAX_SHIFT mean\n"
            "        // pending reduce; caller has already handled those.\n"
            "        if state as u32 >= NSTATE {\n"
            "            return state;\n"
            "        }\n"
            "        let ofst = YY_SHIFT_OFST[state as usize];\n"
            "        let idx = ofst + (lookahead as i32);\n"
            "        if idx < 0 || (idx as usize) >= YY_LOOKAHEAD.len() {\n"
            "            return YY_DEFAULT[state as usize];\n"
            "        }\n"
            "        if YY_LOOKAHEAD[idx as usize] != lookahead {\n"
            "            return YY_DEFAULT[state as usize];\n"
            "        }\n"
            "        YY_ACTION[idx as usize]\n"
            "    }\n\n");

    /* reduce(ruleno) -- pops nrhs, runs callback, pushes LHS+goto. */
    fprintf(out,
            "    fn reduce(&mut self, ruleno: u32) -> Result<ReduceOutcome, ParseError> {\n"
            "        if ruleno >= NRULE {\n"
            "            self.errored = true;\n"
            "            return Err(ParseError::BadReduce { rule: ruleno });\n"
            "        }\n"
            "        let nrhs = -(YY_RULE_NRHS[ruleno as usize] as i32);\n"
            "        let lhs_sym = YY_RULE_LHS[ruleno as usize] as i32;\n"
            "\n"
            "        // Snapshot RHS values, build a transient ReduceCtx,\n"
            "        // call the callback, then mutate the stack.\n"
            "        // Need at least nrhs frames above the bottom marker.\n"
            "        if (self.stack.len() as i32) < nrhs + 1 {\n"
            "            self.errored = true;\n"
            "            return Err(ParseError::StackUnderflow);\n"
            "        }\n"
            "        let split = self.stack.len() - (nrhs as usize);\n"
            "        let mut lhs_value = Value::default();\n"
            "        // Reuse the per-parser scratch Vec to avoid an alloc\n"
            "        // per reduce.  Hot path: amortises to zero.\n"
            "        self.rhs_scratch.clear();\n"
            "        self.rhs_scratch.extend(\n"
            "            self.stack[split..].iter().map(|f| f.value.clone()));\n"
            "        let cb = YY_RULE_REDUCE_FN[ruleno as usize];\n"
            "        // Borrow-split: rhs_scratch and user are disjoint named\n"
            "        // fields of self.  NLL allows disjoint &mut borrows in\n"
            "        // a single struct-literal expression -- each field\n"
            "        // expression is an rvalue evaluated independently before\n"
            "        // the literal is constructed, and the projections do\n"
            "        // not overlap by language definition.  See the\n"
            "        // Rustonomicon 'Splitting Borrows' chapter.\n"
            "        cb(&mut ReduceCtx {\n"
            "            lhs: &mut lhs_value,\n"
            "            rhs: &mut self.rhs_scratch,\n"
            "            user: &mut self.user,\n"
            "        });\n"
            "\n"
            "        // Pop nrhs frames.\n"
            "        self.stack.truncate(split);\n"
            "\n"
            "        // Compute goto state for the LHS non-terminal at the\n"
            "        // parent state.  When the goto is YY_ACCEPT_ACTION,\n"
            "        // the start rule has reduced to its bottom-of-stack\n"
            "        // and we accept.  Mirrors src/parse_engine.c.\n"
            "        let parent_state = self.top_state();\n"
            "        let goto = Self::find_goto(parent_state, lhs_sym as u16);\n"
            "        if goto == YY_ACCEPT_ACTION {\n"
            "            self.final_value = lhs_value;\n"
            "            self.on_parse_accept();\n"
            "            return Ok(ReduceOutcome::Accept);\n"
            "        }\n"
            "        if goto == YY_ERROR_ACTION || goto == YY_NO_ACTION {\n"
            "            self.errored = true;\n"
            "            self.on_syntax_error(lhs_sym as u16, parent_state);\n"
            "            return Err(ParseError::SyntaxError {\n"
            "                token: lhs_sym as u16,\n"
            "                state: parent_state,\n"
            "            });\n"
            "        }\n"
            "        // Encode shift-reduce range as pending reduce on push.\n"
            "        let pushed = if goto > YY_MAX_SHIFT && goto <= YY_MAX_SHIFTREDUCE {\n"
            "            goto + (YY_MIN_REDUCE - YY_MIN_SHIFTREDUCE)\n"
            "        } else {\n"
            "            goto\n"
            "        };\n"
            "        self.stack.push(Frame {\n"
            "            state: pushed,\n"
            "            major: lhs_sym as u16,\n"
            "            value: lhs_value,\n"
            "        });\n"
            "        Ok(ReduceOutcome::Continue)\n"
            "    }\n\n"
            "    #[inline(always)]\n"
            "    fn find_goto(state: u16, lhs: u16) -> u16 {\n"
            "        if (state as u32) >= NSTATE {\n"
            "            return state;\n"
            "        }\n"
            "        let ofst = YY_REDUCE_OFST[state as usize];\n"
            "        let idx = ofst + (lhs as i32);\n"
            "        if idx < 0 || (idx as usize) >= YY_LOOKAHEAD.len() {\n"
            "            return YY_DEFAULT[state as usize];\n"
            "        }\n"
            "        if YY_LOOKAHEAD[idx as usize] != lhs {\n"
            "            return YY_DEFAULT[state as usize];\n"
            "        }\n"
            "        YY_ACTION[idx as usize]\n"
            "    }\n");

    fprintf(out, "}\n\n");
    fprintf(out, "impl Default for %sParser {\n", name);
    fprintf(out, "    fn default() -> Self { Self::new() }\n");
    fprintf(out, "}\n\n");

    fprintf(out,
            "enum ReduceOutcome { Continue, Accept }\n");
}

/* ------------------------------------------------------------------ */
/*  Entry point                                                        */
/* ------------------------------------------------------------------ */

int emit_rust_parser(struct lime *lemp, const char *out_path,
                     const char *grammar_path, char **error) {
    if (lemp == NULL || out_path == NULL) {
        if (error) *error = strdup("emit_rust_parser: bad arguments");
        return -1;
    }
    if (error) *error = NULL;

    LimeRustTables tables;
    char *asm_err = NULL;
    if (lime_emit_rust_assemble_tables(lemp, &tables, &asm_err) != 0) {
        if (error) *error = asm_err ? asm_err : strdup("assemble failed");
        else free(asm_err);
        return -1;
    }

    FILE *out = fopen(out_path, "wb");
    if (out == NULL) {
        lime_emit_rust_free_tables(&tables);
        if (error) {
            char buf[512];
            snprintf(buf, sizeof(buf),
                     "emit_rust_parser: can't open %s for writing", out_path);
            *error = strdup(buf);
        }
        return -1;
    }

    emit_header(out, lemp, grammar_path);
    emit_token_constants(out, lemp);
    emit_dispatch_constants(out, &tables);
    emit_action_tables(out, &tables);
    emit_reduce_callbacks(out, lemp, &tables);
    emit_parser_runtime(out, lemp);

    fclose(out);
    lime_emit_rust_free_tables(&tables);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Cargo crate emission (--rust-crate)                                */
/* ------------------------------------------------------------------ */
/*
** When --rust-crate is set in addition to --rust, emit a minimal
** Cargo crate skeleton next to the parser.rs:
**
**   <basename>_crate/
**     Cargo.toml
**     src/lib.rs    (re-exports the parser module)
**     src/parser.rs (the .rs we just wrote, moved into place)
**
** The user's downstream Rust project then declares:
**
**   [dependencies]
**   <basename> = { path = "<basename>_crate" }
**
** And imports via `use <basename>::Parser;`.
*/

static char *str_dup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char *o = (char *)malloc(n + 1);
    if (!o) return NULL;
    memcpy(o, s, n + 1);
    return o;
}

int emit_rust_crate(struct lime *lemp, const char *rs_path, char **error) {
    if (!lemp || !rs_path) {
        if (error) *error = str_dup("emit_rust_crate: bad arguments");
        return -1;
    }
    if (error) *error = NULL;

    /* Derive crate dir from rs_path: <stem>.rs -> <stem>_crate/ */
    const char *slash = strrchr(rs_path, '/');
    const char *base  = slash ? slash + 1 : rs_path;
    const char *dot   = strrchr(base, '.');
    size_t base_len   = dot ? (size_t)(dot - base) : strlen(base);

    char dir[1024];
    int n;
    if (slash) {
        size_t prefix = (size_t)(slash + 1 - rs_path);
        if (prefix + base_len + 7 >= sizeof(dir)) {
            if (error) *error = str_dup("emit_rust_crate: path too long");
            return -1;
        }
        n = snprintf(dir, sizeof(dir), "%.*s%.*s_crate",
                     (int)prefix, rs_path, (int)base_len, base);
    } else {
        n = snprintf(dir, sizeof(dir), "%.*s_crate",
                     (int)base_len, base);
    }
    if (n < 0 || (size_t)n >= sizeof(dir)) {
        if (error) *error = str_dup("emit_rust_crate: path too long");
        return -1;
    }

    char src_dir[1100];
    snprintf(src_dir, sizeof(src_dir), "%s/src", dir);

    /* mkdir -p the crate's src/ */
    char mkdir_cmd[2200];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s", src_dir);
    if (system(mkdir_cmd) != 0) {
        if (error) {
            char buf[256];
            snprintf(buf, sizeof(buf),
                     "emit_rust_crate: mkdir -p %s failed", src_dir);
            *error = str_dup(buf);
        }
        return -1;
    }

    /* Move parser.rs into <dir>/src/parser.rs */
    char target_rs[1200];
    snprintf(target_rs, sizeof(target_rs), "%s/parser.rs", src_dir);
    if (rename(rs_path, target_rs) != 0) {
        if (error) {
            char buf[512];
            snprintf(buf, sizeof(buf),
                     "emit_rust_crate: rename %s -> %s failed",
                     rs_path, target_rs);
            *error = str_dup(buf);
        }
        return -1;
    }

    /* Cargo.toml */
    char cargo_path[1200];
    snprintf(cargo_path, sizeof(cargo_path), "%s/Cargo.toml", dir);
    FILE *cargo = fopen(cargo_path, "wb");
    if (!cargo) {
        if (error) {
            char buf[512];
            snprintf(buf, sizeof(buf),
                     "emit_rust_crate: open %s failed", cargo_path);
            *error = str_dup(buf);
        }
        return -1;
    }
    /* Use the grammar's name as the crate name (lowercased basename). */
    char crate_name[256];
    snprintf(crate_name, sizeof(crate_name), "%.*s", (int)base_len, base);
    for (char *p = crate_name; *p; p++) {
        if (*p >= 'A' && *p <= 'Z') *p = (char)(*p + 32);
        if (*p == '-' || *p == '.') *p = '_';
    }
    fprintf(cargo,
            "# Generated by lime --rust-crate -- safe to commit, edit,\n"
            "# or replace.  Regenerating overwrites.\n"
            "[package]\n"
            "name = \"%s\"\n"
            "version = \"0.1.0\"\n"
            "edition = \"2021\"\n"
            "publish = false\n\n"
            "[lib]\n"
            "name = \"%s\"\n"
            "path = \"src/lib.rs\"\n",
            crate_name, crate_name);
    /* If --rustlex-memchr was passed, emit a memchr dep so the
    ** generated lexer's fast-path scans link cleanly. */
    extern int g_lime_rustlex_memchr_flag;
    if (g_lime_rustlex_memchr_flag) {
        fprintf(cargo,
                "\n[dependencies]\n"
                "# --rustlex-memchr: SIMD-accelerated fast-path byte\n"
                "# search inside the lexer.  Removing this dep means\n"
                "# the .rs output won't compile -- regenerate without\n"
                "# --rustlex-memchr to drop the dependency.\n"
                "memchr = \"2\"\n");
    }
    fclose(cargo);

    /* src/lib.rs: re-export everything from parser.rs */
    char lib_path[1300];
    snprintf(lib_path, sizeof(lib_path), "%s/lib.rs", src_dir);
    FILE *lib = fopen(lib_path, "wb");
    if (!lib) {
        if (error) {
            char buf[512];
            snprintf(buf, sizeof(buf),
                     "emit_rust_crate: open %s failed", lib_path);
            *error = str_dup(buf);
        }
        return -1;
    }
    fprintf(lib,
            "// Generated by lime --rust-crate -- safe to commit + edit.\n"
            "// Regenerating only overwrites parser.rs; this lib.rs is\n"
            "// preserved across regenerations once you add custom code.\n\n"
            "%s"
            "mod parser;\n\n"
            "pub use parser::*;\n",
            g_lime_rust_no_std
                ? "#![no_std]\nextern crate alloc;\n\n"
                : "");
    fclose(lib);
    return 0;
}
