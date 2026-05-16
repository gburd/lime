/*
** examples/pg_bootscanner/bootscanner.lex -- Lime port of
** PostgreSQL's src/backend/bootstrap/bootscanner.l.
**
** This file demonstrates the Lime .lex source language as a
** drop-in replacement for a real PG flex scanner.  The original
** is 165 lines including the flex options block, %top{ }, the
** yyalloc/yyrealloc/yyfree shims, and the prologue/epilogue C
** code.  The Lime equivalent is roughly 70 lines of
** declarations and rules.
**
** SCOPE.  The source-level rules and recognition behaviour
** mirror the flex scanner exactly.  What is intentionally
** *not* preserved here:
**
**   - yylineno bookkeeping (the design moves line/column
**     tracking to %location_advance; the test driver in this
**     directory does not need line numbers).
**   - palloc/repalloc/pfree shims.  The Lime runtime takes
**     malloc-shaped function pointers at LexAlloc time, so
**     the caller plugs in palloc trivially without rewriting
**     the lexer.
**   - The flex `%option` declarations (`reentrant`,
**     `bison-bridge`, `8bit`, `never-interactive`, `nodefault`,
**     `noinput`, `nounput`, `noyywrap`, ...) are all implicit
**     in Lime's runtime model and need no analogue.
**   - DeescapeQuotedString() and the kw/str union plumbing.
**     The Lime runtime hands `matched`/`matched_len` to the
**     emit callback; user-side post-processing belongs in the
**     callback, not the lexer.
**
** ONE INTENTIONAL DEVIATION.  The original uses `^\#[^\n]*` to
** match comments only at the start of a line.  Lime's regex
** anchors (`^`/`$`) bind to start/end of input, not start/end
** of line, by deliberate spec choice (see LEXER_DESIGN.md
** "Pattern language").  The port uses the unanchored
** `#[^\n]*`, which is faithful for real BKI input where `#`
** never appears outside of comments.  If a future grammar
** needs strict line-start semantics, the canonical pattern is
** a state machine: a `<NEWLINE>` exclusive state entered on
** every `\n` and exited by any non-`#` character.
*/

%name_prefix Boot.

%pattern id    /[-A-Za-z0-9_]+/.
%pattern sid   /'([^']|'')*'/.

/* ===== whitespace, newlines, comments =====
**
** Empty action bodies in Lime auto-emit the matched rule.
** That's wrong for whitespace -- we want it consumed but
** silent.  LEX_SKIP() consumes the bytes and suppresses the
** emit, the direct analogue of an empty `;` action body in
** flex (`[\r\t ] ;`).  Newline gets the same treatment; line
** bookkeeping is the caller's job via %location_advance, not
** part of this scanner. */
rule whitespace matches /[\r\t ]+/  { LEX_SKIP(); }
rule newline    matches /\n/        { LEX_SKIP(); }
rule comment    matches /#[^\n]*/   { LEX_SKIP(); }

/* ===== punctuation =====
**
** Flex returns single character codes ('+', '(', etc.) via
** `return ',';`.  Lime's emit callback is rule-id based, so
** we expose each as a named rule and the test driver maps
** rule-id -> token-code (or in a real parser, the action
** body would call LEX_EMIT(BOOT_TOK_COMMA) to remap). */
rule comma   matches /,/   { /* auto-emit */ }
rule equals  matches /=/   { /* auto-emit */ }
rule lparen  matches /\(/  { /* auto-emit */ }
rule rparen  matches /\)/  { /* auto-emit */ }

/* ===== reserved word with leading underscore =====
**
** _null_ is the one BKI keyword that doesn't follow the
** normal "keyword_text -> kw type" mapping (yylval->kw is
** not set; it's a true reserved word).  Listed before {id}
** so the longest-match-then-declaration-order rule fires
** this one on input "_null_" instead of the catch-all
** identifier rule. */
rule null_literal matches /_null_/  { /* auto-emit NULL_LITERAL */ }

/* ===== keywords =====
**
** Each keyword is its own rule.  Per the Lime disambiguation
** rules (LEXER_DESIGN.md "Match disambiguation"): longest
** match wins; on length ties, declaration order wins.  Both
** the keyword rule (e.g. `bootstrap`, 9 chars) and the
** generic identifier rule (`{id}`, also 9 chars on the same
** input) accept exactly 9 bytes, so declaration order
** decides -- which is why every keyword is declared *before*
** the catch-all `ident` rule below.
**
** The flex original returned a kw-text constant in
** yylval->kw.  In a real Lime parser-driver shim, the
** action body would do `LEX_EMIT(BOOT_TOK_OPEN)` and pass
** the matched text in a parser-side payload struct.  Here
** we use auto-emit and rely on the rule id to identify the
** keyword. */
rule kw_open            matches /open/             { /* auto-emit */ }
rule kw_close           matches /close/            { /* auto-emit */ }
rule kw_create          matches /create/           { /* auto-emit */ }
rule kw_OID             matches /OID/              { /* auto-emit */ }
rule kw_bootstrap       matches /bootstrap/        { /* auto-emit */ }
rule kw_shared_relation matches /shared_relation/  { /* auto-emit */ }
rule kw_rowtype_oid     matches /rowtype_oid/      { /* auto-emit */ }
rule kw_insert          matches /insert/           { /* auto-emit */ }
rule kw_declare         matches /declare/          { /* auto-emit */ }
rule kw_build           matches /build/            { /* auto-emit */ }
rule kw_indices         matches /indices/          { /* auto-emit */ }
rule kw_unique          matches /unique/           { /* auto-emit */ }
rule kw_index           matches /index/            { /* auto-emit */ }
rule kw_on              matches /on/               { /* auto-emit */ }
rule kw_using           matches /using/            { /* auto-emit */ }
rule kw_toast           matches /toast/            { /* auto-emit */ }
rule kw_FORCE           matches /FORCE/            { /* auto-emit */ }
rule kw_NOT             matches /NOT/              { /* auto-emit */ }
rule kw_NULL            matches /NULL/             { /* auto-emit */ }

/* ===== generic identifier =====
**
** Catch-all for identifiers that aren't any of the keywords
** above.  Order matters (see keyword block).  The action
** body in the flex original was
**   yylval->str = pstrdup(yytext); return ID;
** -- which is just "emit ID with the text as value".
** Lime's emit callback already passes (text, len), so the
** auto-emit form does the same work. */
rule ident matches /{id}/ { /* auto-emit */ }

/* ===== single-quoted string =====
**
** Flex source: 'sid \'([^']|\'\')*\'' i.e. quote, then any
** non-quote byte OR a doubled quote (escape), repeated, then
** closing quote.  The action body in the original called
** DeescapeQuotedString(yytext) before returning ID.  Here we
** emit the raw matched span; the user's emit callback can
** post-process. */
rule sqstring matches /{sid}/ { /* auto-emit */ }

/* ===== unexpected character =====
**
** Flex's `.` rule -- catch-all for any byte that no other
** rule matched.  Original called elog(ERROR, ...).  Lime's
** equivalent is LEX_ERROR_AT, which causes the current
** LexFeedBytes call to return LEX_ERROR with a stable
** diagnostic string.  Without this rule, an unmatched byte
** would also fail (the runtime's "unmatched input" default
** message), but with it we get a domain-specific error. */
rule unexpected matches /./ {
    LEX_ERROR_AT("syntax error: unexpected character");
}
