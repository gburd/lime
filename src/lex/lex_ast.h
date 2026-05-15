/*
** src/lex/lex_ast.h -- AST node types for Lime lexer source files.
**
** Match the v0.2 design (docs/LEXER_DESIGN.md): a .lex source file
** parses into a LimeLexSpec containing typed declarations for
** patterns, states, keyword tables, literal buffers, rulesets,
** and rules.  The AST is a parse-time intermediate; it does NOT
** carry compiled DFAs, regex trees, or runtime state.  Those are
** the M2/M3 outputs constructed FROM the AST.
**
** Memory model: all AST node strings (names, regex sources,
** action C code) are owned by the AST; lifetime is the parse
** session.  free_lex_spec() releases the entire tree.
**
** This header is internal to the lex compiler subsystem; it is
** NOT installed and NOT part of the runtime ABI.  Public lexer
** runtime types (yyLexer, LexResult, etc., for grammars
** consuming generated lexers) live in include/ and ship with
** the runtime library.  The two are unrelated by design.
*/
#ifndef LIME_LEX_AST_H
#define LIME_LEX_AST_H

#include <stddef.h>

/* ----- Pattern declarations: %pattern name /regex/. ----- */
typedef struct LimeLexPattern LimeLexPattern;
struct LimeLexPattern {
    char            *name;        /* pattern fragment name */
    char            *regex;       /* raw regex source, no /.../ delimiters */
    char            *expanded_regex;  /* M1.3: regex with {name} references
                                       ** substituted; populated by
                                       ** lime_lex_resolve_patterns. */
    int              line;        /* source line number */
    int              _resolve_visit; /* transient visitor state for cycle
                                      ** detection: 0 unvisited, 1 in
                                      ** progress, 2 done. */
    LimeLexPattern  *next;        /* singly-linked declaration list */
};

/* ----- State declarations: %state, %exclusive_state, with optional
** typed local data and destructor. ----- */
typedef struct LimeLexState LimeLexState;
struct LimeLexState {
    char            *name;        /* state name */
    int              exclusive;   /* 1 if %exclusive_state, 0 if %state */
    char            *local_body;  /* C struct body for state-local data,
                                   ** NULL if no local data */
    char            *destructor;  /* %state_destructor body, NULL if none */
    int              line;
    LimeLexState    *next;
};

/* ----- Keyword tables. ----- */
typedef struct LimeLexKeywordTable LimeLexKeywordTable;
struct LimeLexKeywordTable {
    char                  *name;
    int                    case_insensitive;  /* 0 or 1 */
    char                  *prefix;            /* token-name prefix; NULL if none */
    char                 **keywords;          /* array of strings */
    int                    n_keywords;
    int                    line;
    LimeLexKeywordTable   *next;
};

/* ----- Literal buffer declarations: %literal_buffer NAME { ... }. ----- */
typedef struct LimeLexLiteralBuffer LimeLexLiteralBuffer;
struct LimeLexLiteralBuffer {
    char                   *name;
    char                   *element_type;     /* C type, default "char" */
    int                     initial_capacity; /* default 64 */
    char                   *grow_policy;      /* e.g. "*2" or "+1024" */
    char                   *alloc_fn;         /* C function names */
    char                   *realloc_fn;
    char                   *free_fn;
    int                     line;
    LimeLexLiteralBuffer   *next;
};

/* ----- Rules: <STATES> rule_name matches /pattern/ { action } or
** <STATES> rule_name matches <<EOF>> { action }. ----- */
typedef struct LimeLexRule LimeLexRule;
struct LimeLexRule {
    char            *name;        /* rule_name */
    char           **states;      /* state names this rule fires in;
                                   ** NULL means INITIAL */
    int              n_states;
    int              is_eof;      /* 1 if <<EOF>> rule, 0 otherwise */
    char            *pattern;     /* regex source (pre-expansion);
                                   ** NULL when is_eof */
    char            *expanded_pattern;  /* M1.3: pattern with {name}
                                         ** references substituted from
                                         ** the patterns table; NULL when
                                         ** is_eof or before resolve. */
    char            *action;      /* action body C code, includes braces */
    int              line;
    LimeLexRule     *next;
};

/* ----- Rulesets: %ruleset name { rules ... }. ----- */
typedef struct LimeLexRuleset LimeLexRuleset;
struct LimeLexRuleset {
    char            *name;
    LimeLexRule     *rules;       /* rules belonging to this ruleset */
    int              line;
    LimeLexRuleset  *next;
};

/* ----- Top-level spec: one per .lex file. ----- */
typedef struct LimeLexSpec LimeLexSpec;
struct LimeLexSpec {
    /* Prelude directives (NULL if not declared). */
    char *name_prefix;            /* %name_prefix */
    char *token_prefix;           /* %token_prefix */
    char *token_type;             /* %token_type {C type} */
    char *location_type;          /* %location_type {C type} */
    char *extra_argument;         /* %lexer_extra_argument {C type *name} */
    char *include_block;          /* %include { C code } */

    /* Declarations.  Each list is in declaration order. */
    LimeLexPattern        *patterns;
    LimeLexState          *states;
    LimeLexKeywordTable   *keyword_tables;
    LimeLexLiteralBuffer  *literal_buffers;
    LimeLexRuleset        *rulesets;
    LimeLexRule           *rules;          /* top-level rules (not in
                                            ** any ruleset) */

    /* Composition: %lexer_include in order of declaration. */
    char                 **lexer_includes;
    int                    n_lexer_includes;

    /* Source-file metadata. */
    const char  *filename;
    int          line_count;     /* total lines in source */
    int          error_count;    /* parse errors encountered */
};

/* AST allocation / release.  Every alloc returns a zeroed node
** linked into the spec on success; ownership transfers to the
** spec immediately.  All strings stored in nodes must be heap-
** allocated (the AST owns them and frees on lime_lex_spec_free).
*/
LimeLexSpec *lime_lex_spec_new(const char *filename);
void         lime_lex_spec_free(LimeLexSpec *spec);

#endif /* LIME_LEX_AST_H */
