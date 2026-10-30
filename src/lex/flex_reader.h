/*
** src/lex/flex_reader.h -- lex/flex (.l) front end for the Lime lexer.
**
** Reads a POSIX lex / flex 2.6 scanner and builds the same LimeLexSpec
** the .lex reader builds, so both go through one DFA compiler
** (lime_lex_compile).  Everything flex has that LimeLexSpec has no slot
** for (start-condition numbering, %option values, the code blocks of
** sections 1-3, per-rule ^ / $ flags, REJECT and yymore use) lives in the
** LimeFlexSpec side structure.  The flex-mode C emitter
** (emit_c_skin_flex.c, lime_emit_flex_scanner) reads both.
**
** How flex constructs map onto LimeLexSpec:
**   - Each rule becomes a top-level LimeLexRule, in file order (so the
**     DFA's "lowest rule index wins on a length tie" is flex's "earliest
**     rule wins").  Patterns are rewritten into canonical Lime regex text
**     (every byte that is not [A-Za-z0-9] written as \xHH), with {name}
**     definitions, "quoted" strings, POSIX classes, case folding and
**     posix-compat repetition already expanded.
**   - A flex start condition S becomes the exclusive Lime state S (and
**     S__bol when some rule uses ^).  Every rule is qualified with the
**     explicit list of Lime states it fires in, so %s/%x inclusiveness is
**     resolved here, not by lime_lex_compile.
**   - `r/s` becomes `rs`; with |s| fixed, trail = |s| (yyleng = match - trail),
**     else with |r| fixed, head = |r| + 1 (yyleng = head - 1).  `r$` is
**     `r/\n`.  Both sides variable is rejected by name.
**   - <<EOF>> rules are LimeLexRules with is_eof = 1; the per-start-
**     condition winner (flex: first <<EOF>> for a condition wins) is in
**     LimeFlexSpec.sc_eof_rule.
*/
#ifndef LIME_LEX_FLEX_READER_H
#define LIME_LEX_FLEX_READER_H

#include "lex_ast.h"

#include <stddef.h>

/* A verbatim code segment and the .l line its first byte came from. */
typedef struct {
    int line;
    char *text;
} LimeFlexCode;

/* Per-rule flex data, indexed like the spec's top-level rule list. */
typedef struct {
    int line;          /* .l line of the pattern */
    int is_eof;        /* <<EOF>> rule */
    int bol;           /* pattern began with ^ */
    int trail;         /* r/s with |s| fixed: bytes to give back ($ = 1), else 0 */
    int head;          /* r/s with |r| fixed and |s| variable: |r| + 1, else 0 */
    int action_rule;   /* rule whose action runs: self, or the next rule
                       ** with a real action for a `|` rule */
    int action_line;   /* .l line where the action text starts */
    int is_default;    /* the implicit last rule (ECHO, or jam with nodefault) */
    int can_match_eol; /* the pattern can consume '\n' (yylineno update) */
    char *action;      /* action text (NULL for a `|` rule) */
    char *pre_code;    /* %{ %} block written just before this rule */
    unsigned char *sc; /* sc[i] = 1 if the rule fires in start condition i */
} LimeFlexRule;

typedef struct {
    /* Booleans are -1 when not given, else 0/1 (so "noyywrap" and
    ** "yywrap" can both be told apart from "unspecified"). */
    int reentrant, bison_bridge, bison_locations, yylineno, yywrap;
    int nodefault, caseless, stack, main_fn, posix_compat, debug;
    int never_interactive, always_interactive, noline;
    int unput, input, yy_push_state, yy_pop_state, yy_top_state;
    int yyalloc, yyrealloc, yyfree, unistd, stdinit, reject, yymore;
    int warn, eightbit, backup;
    /* yyget_x / yyset_x suppressions are accepted and ignored. */
    char *prefix;      /* default "yy" */
    char *outfile;     /* %option outfile=, NULL if absent */
    char *header_file; /* %option header-file=, NULL if absent */
    char *extra_type;  /* %option extra-type=, NULL = void * */
} LimeFlexOptions;

typedef struct {
    LimeLexSpec *spec;   /* shared AST; owned */
    const char *filename;

    /* Start conditions: index 0 is INITIAL. */
    char **sc_names;
    unsigned char *sc_excl;
    int n_sc;
    int *sc_eof_rule;    /* per sc: rule index of its <<EOF>> rule, or -1 */

    LimeFlexRule *rules; /* parallel to spec->rules; the last is the default rule */
    int n_rules;
    int n_user_rules;    /* rules written in the file (n_rules - 1) */

    LimeFlexCode *top_code;   /* %top{ } blocks */
    int n_top_code;
    LimeFlexCode *defs_code;  /* section 1: %{ %}, indented lines, comments */
    int n_defs_code;
    LimeFlexCode *prolog_code;/* section 2 before the first rule */
    int n_prolog_code;
    LimeFlexCode user_code;   /* section 3 (text NULL when absent) */

    LimeFlexOptions opt;

    /* Derived feature use. */
    int bol_needed;   /* some rule uses ^ */
    int has_trail;    /* some rule uses $ or r/s */
    int uses_reject;  /* REJECT in an action, or %option reject */
    int uses_yymore;  /* yymore in an action, or %option yymore */

    int error_count;
} LimeFlexSpec;

/* Return 1 if `src` looks like lex/flex input: some line is exactly
** `%%` at column 0 (optionally followed by blanks or other text, which
** flex allows), 0 otherwise.  Lime .lex files never contain one. */
int lime_flex_detect(const char *src, size_t len);

/* Options the command line imposes on the scanner (flex's -i, -P, -s,
** -X ...).  Command-line values override %option. */
typedef struct {
    int caseless;      /* -i */
    int posix_compat;  /* -X / --posix */
    int nodefault;     /* -s */
    int debug;         /* -d */
    int reentrant;     /* -R / --reentrant */
    int bison_bridge;  /* --bison-bridge */
    int bison_locations;
    int yylineno;      /* --yylineno */
    int noyywrap;      /* --noyywrap */
    int never_interactive; /* -B */
    int always_interactive; /* -I */
    int noline;        /* -L */
    int nounistd;      /* --nounistd */
    const char *prefix;/* -P */
} LimeFlexCliOptions;

/* Parse `src` (the whole .l file).  Never returns NULL except on
** allocation failure; check ->error_count.  Diagnostics go to stderr
** as `file:line: message`.  `cli` may be NULL. */
LimeFlexSpec *lime_flex_read(const char *filename, const char *src, size_t len,
                             const LimeFlexCliOptions *cli);

void lime_flex_spec_free(LimeFlexSpec *fs);

/* Compiled-state index of (start condition sc, at beginning of line
** bol) in the LimeLexCompiled produced from fs->spec. */
int lime_flex_state_index(const LimeFlexSpec *fs, int sc, int bol);

#endif /* LIME_LEX_FLEX_READER_H */
