/*
** src/lex/emit_rust_skin_logos.c -- logos-API-compatible skin over
** lime's emitted Rust lexer.  See docs/SKINS.md.
**
** Activated by `lime --target=rust:logos -X grammar.lex`, which (in
** addition to the standard <stem>_lex.rs that --target=rust -X already
** emits) also writes a sibling <stem>_lex_logos.rs with:
**
**   - `Token` enum (one unit variant per lex rule)
**   - `Lexer<'source>` struct that owns a lime_lex::Lexer
**   - `impl Iterator for Lexer` yielding `Result<Token, ()>`
**   - `Token::lexer(input: &str) -> Lexer<'_>` constructor
**   - `Lexer::span` / `Lexer::slice`
**
** Tokens carry no semantic payload (unit variants only) in v0.9.3;
** logos's Token::Number(i64)-style payloads are deferred.  Spans and
** slices are computed from the inner Token's `start` and `len`.
**
** Module-resolution model: the wrapper imports the sibling lexer
** module via `use super::<stem>_lex as lime_lex;`.  Both files are
** expected to be brought into the same parent module by the consumer
** (e.g. lib.rs declares `pub mod foo_lex; pub mod foo_lex_logos;`).
*/

#include "lex_ast.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *xstrdup_local(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char *out = (char *)malloc(n + 1);
    if (!out) return NULL;
    memcpy(out, s, n + 1);
    return out;
}

/* Convert a snake_case / lowercase rule name to PascalCase for use
** as a Rust enum variant.  Non-alnum chars are treated as separators.
** Caller frees. */
static char *to_pascal_case(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char *out = (char *)malloc(n + 2);
    if (!out) return NULL;
    size_t j = 0;
    int upper_next = 1;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (!isalnum(c) && c != '_') {
            upper_next = 1;
            continue;
        }
        if (c == '_') { upper_next = 1; continue; }
        if (upper_next) {
            out[j++] = (char)toupper(c);
            upper_next = 0;
        } else {
            out[j++] = (char)tolower(c);
        }
    }
    if (j == 0) {
        out[j++] = 'R';
    }
    out[j] = '\0';
    return out;
}

/* Uppercase ASCII duplicate; mirrors emit_rust_lex.c's helper so the
** RULE_<NAME> constants reference matches what emit_rust_lex.c emitted. */
static char *upper_ident_dup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char *out = (char *)malloc(n + 1);
    if (!out) return NULL;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (!isalnum(c) && c != '_') {
            out[i] = '_';
        } else {
            out[i] = (char)toupper(c);
        }
    }
    out[n] = '\0';
    return out;
}

static const char *eff_prefix(const char *p) {
    return (p && *p) ? p : "Lex";
}

/* Walk every rule and emit ONE callback per rule for `each(name, idx, ud)`.
** Order matches emit_rust_lex.c's emit_rule_constants(): top-level
** spec->rules first, then each ruleset's rules.  Skips unnamed rules
** (which emit_rule_constants also skips) but DOES advance the rule-id
** counter for them so id arithmetic stays consistent.  Returns the
** total number of rules walked. */
static int walk_rules(const LimeLexSpec *spec,
                      void (*each)(const char *name, int rule_id, void *ud),
                      void *ud) {
    int rule_id = 0;
    for (LimeLexRule *r = spec->rules; r; r = r->next, rule_id++) {
        if (!r->name) continue;
        each(r->name, rule_id, ud);
    }
    for (LimeLexRuleset *rs = spec->rulesets; rs; rs = rs->next) {
        for (LimeLexRule *r = rs->rules; r; r = r->next, rule_id++) {
            if (!r->name) continue;
            each(r->name, rule_id, ud);
        }
    }
    return rule_id;
}

struct emit_ctx {
    FILE *out;
    const char *prefix_upper;   /* e.g. "JSON" */
};

static void emit_enum_variant(const char *name, int rule_id, void *ud) {
    (void)rule_id;
    struct emit_ctx *c = (struct emit_ctx *)ud;
    char *pascal = to_pascal_case(name);
    if (!pascal) return;
    fprintf(c->out, "    /// matches lex rule `%s` (id %d)\n", name, rule_id);
    fprintf(c->out, "    %s,\n", pascal);
    free(pascal);
}

static void emit_match_arm(const char *name, int rule_id, void *ud) {
    (void)rule_id;
    struct emit_ctx *c = (struct emit_ctx *)ud;
    char *pascal = to_pascal_case(name);
    char *upper  = upper_ident_dup(name);
    if (!pascal || !upper) { free(pascal); free(upper); return; }
    fprintf(c->out, "        lime_lex::%s_RULE_%s => Ok(Token::%s),\n",
            c->prefix_upper, upper, pascal);
    free(pascal);
    free(upper);
}

/*
** lime_emit_rust_skin_logos -- write <out_path> with a logos-API-compatible
** wrapper around the sibling Rust lexer module identified by
** <lex_module_name>.  Returns 0 on success, non-zero on failure with
** *error set to a heap-allocated diagnostic (caller frees).
*/
int lime_emit_rust_skin_logos(const LimeLexSpec *spec,
                              const char *src_path,
                              const char *out_path,
                              const char *lex_module_name,
                              char **error) {
    if (!spec || !out_path || !lex_module_name) {
        if (error) *error = xstrdup_local("emit_rust_skin_logos: bad arguments");
        return -1;
    }
    if (error) *error = NULL;

    FILE *out = fopen(out_path, "wb");
    if (!out) {
        if (error) {
            char buf[512];
            snprintf(buf, sizeof(buf),
                     "emit_rust_skin_logos: can't open %s for writing", out_path);
            *error = xstrdup_local(buf);
        }
        return -1;
    }

    const char *prefix = eff_prefix(spec->name_prefix);
    char *prefix_upper = upper_ident_dup(prefix);
    if (!prefix_upper) {
        fclose(out);
        if (error) *error = xstrdup_local("emit_rust_skin_logos: alloc");
        return -1;
    }

    /* ---- Header ------------------------------------------------------ */
    fprintf(out,
        "// SPDX-License-Identifier: <inherited from input grammar>\n"
        "//\n"
        "// Generated by lime --target=rust:logos -- DO NOT EDIT BY HAND.\n"
        "//\n"
        "// Source grammar: %s\n"
        "// Lexer prefix:   %s\n"
        "// Sibling module: %s (the standard <stem>_lex.rs)\n"
        "// To regenerate:  lime --target=rust:logos -X %s\n"
        "//\n"
        "//! logos-API-compatible skin over Lime's %s lexer.\n"
        "//! Drop-in for code that imports `logos::Logos` and uses\n"
        "//! `T::lexer(&input)`.\n"
        "//!\n"
        "//! Limitations (v0.9.3):\n"
        "//!   - Token variants carry no semantic payload (unit variants only).\n"
        "//!   - Single-buffer input only; spans index into `input.as_bytes()`.\n"
        "//!   - On lex error, yields `Some(Err(()))` once then `None`\n"
        "//!     (logos resyncs and continues; we don't yet).\n"
        "\n"
        "#![allow(dead_code)]\n"
        "#![allow(non_snake_case)]\n"
        "#![allow(non_camel_case_types)]\n"
        "#![allow(non_upper_case_globals)]\n"
        "#![allow(clippy::all)]\n"
        "\n"
        "use super::%s as lime_lex;\n"
        "\n",
        src_path ? src_path : "(unknown)",
        prefix,
        lex_module_name,
        src_path ? src_path : "<lex>",
        prefix,
        lex_module_name);

    /* ---- Token enum -------------------------------------------------- */
    fprintf(out,
        "/// Logos-style token enum.  One unit variant per lex rule plus an\n"
        "/// `Error` sentinel for unrecognised input.\n"
        "#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]\n"
        "pub enum Token {\n");
    struct emit_ctx ctx = { out, prefix_upper };
    int n_rules = walk_rules(spec, emit_enum_variant, &ctx);
    fprintf(out,
        "    /// no-match sentinel; mirrors logos's Result::Err pattern\n"
        "    Error,\n"
        "}\n"
        "\n"
        "impl Token {\n"
        "    /// Construct a logos-style lexer.  Equivalent to\n"
        "    /// `<T as logos::Logos>::lexer(&input)`.\n"
        "    pub fn lexer<'source>(input: &'source str) -> Lexer<'source> {\n"
        "        Lexer::new(input)\n"
        "    }\n"
        "}\n"
        "\n");

    /* ---- Lexer struct + impls --------------------------------------- */
    fprintf(out,
        "/// Logos-API-compatible lexer.  Drives the inner lime lexer in\n"
        "/// batch mode (one `tokenize()` call up front, then iterate over\n"
        "/// the resulting Vec).  Self-referential lifetimes are avoided by\n"
        "/// materialising the token stream eagerly.\n"
        "pub struct Lexer<'source> {\n"
        "    inner: lime_lex::Lexer,\n"
        "    input: &'source str,\n"
        "    tokens: Vec<lime_lex::Token>,\n"
        "    idx: usize,\n"
        "    last_span: core::ops::Range<usize>,\n"
        "    last_slice: &'source str,\n"
        "    /// `Some(off)` if the inner tokenize() returned NoMatch at byte\n"
        "    /// offset `off`.  Surfaced as one `Some(Err(()))` then None.\n"
        "    err_at: Option<usize>,\n"
        "}\n"
        "\n"
        "impl<'source> Lexer<'source> {\n"
        "    pub fn new(input: &'source str) -> Self {\n"
        "        let mut inner = lime_lex::Lexer::new();\n"
        "        let (tokens, err_at) = match inner.tokenize(input) {\n"
        "            Ok(v) => (v, None),\n"
        "            Err(lime_lex::LexError::NoMatch { offset, .. }) => (Vec::new(), Some(offset)),\n"
        "            Err(_) => (Vec::new(), Some(0)),\n"
        "        };\n"
        "        Self {\n"
        "            inner,\n"
        "            input,\n"
        "            tokens,\n"
        "            idx: 0,\n"
        "            last_span: 0..0,\n"
        "            last_slice: \"\",\n"
        "            err_at,\n"
        "        }\n"
        "    }\n"
        "\n"
        "    /// Byte span of the most recently yielded token.  Mirrors\n"
        "    /// `logos::Lexer::span()`.\n"
        "    pub fn span(&self) -> core::ops::Range<usize> { self.last_span.clone() }\n"
        "\n"
        "    /// Source slice of the most recently yielded token.  Mirrors\n"
        "    /// `logos::Lexer::slice()`.\n"
        "    pub fn slice(&self) -> &'source str { self.last_slice }\n"
        "\n"
        "    /// Full source input the lexer was constructed with.  Mirrors\n"
        "    /// `logos::Lexer::source()`.\n"
        "    pub fn source(&self) -> &'source str { self.input }\n"
        "}\n"
        "\n"
        "impl<'source> Iterator for Lexer<'source> {\n"
        "    type Item = Result<Token, ()>;\n"
        "    fn next(&mut self) -> Option<Self::Item> {\n"
        "        if self.idx < self.tokens.len() {\n"
        "            let tok = &self.tokens[self.idx];\n"
        "            self.idx += 1;\n"
        "            let start = tok.start;\n"
        "            let end = tok.start + tok.len;\n"
        "            self.last_span = start..end;\n"
        "            self.last_slice = &self.input[start..end];\n"
        "            return Some(map_lime_to_logos(tok.rule_id));\n"
        "        }\n"
        "        if let Some(off) = self.err_at.take() {\n"
        "            self.last_span = off..off;\n"
        "            self.last_slice = \"\";\n"
        "            return Some(Err(()));\n"
        "        }\n"
        "        None\n"
        "    }\n"
        "}\n"
        "\n");

    /* ---- map_lime_to_logos ------------------------------------------ */
    fprintf(out,
        "/// Map an inner lime rule id to the logos-skin Token enum.\n"
        "/// The match table is dense (one arm per rule) so LLVM lowers it\n"
        "/// to a jump table.\n"
        "fn map_lime_to_logos(rule_id: u16) -> Result<Token, ()> {\n"
        "    match rule_id {\n");
    walk_rules(spec, emit_match_arm, &ctx);
    fprintf(out,
        "        _ => Err(()),\n"
        "    }\n"
        "}\n"
        "\n"
        "/// Total rule count (mirrors lime_lex::%s_NRULES).  Useful for\n"
        "/// sanity-checking generated tables in tests.\n"
        "pub const N_RULES: u16 = %d;\n",
        prefix_upper, n_rules);

    free(prefix_upper);
    fclose(out);
    return 0;
}
