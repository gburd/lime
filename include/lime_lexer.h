/*
** include/lime_lexer.h -- the runtime lexicon: keywords and operators
** that live in a ParserSnapshot and change when extensions compose.
**
** A flex scanner hard-codes its keywords (scan.l's ScanKeywordLookup)
** and its operators (`"<="  return LESS_EQUALS;`).  A Lime scanner can
** instead ask the snapshot it is feeding: the snapshot carries an
** immutable keyword table and operator trie, rebuilt whenever an
** extension composes, so a keyword or operator added by an extension at
** runtime resolves with no regenerated scanner, no compiler and no
** dlopen.  Token codes and the lexemes that produce them live in the
** same object, so they swap together and can never disagree.
**
** Two libraries, split by when the code runs:
**   liblime_parser    per-token READERS (this header's "lookup" half):
**                     lime_kw_lookup, lime_op_match, lime_snapshot_lexicon,
**                     lime_lexicon_free.  Lock-free; a reader holds a
**                     snapshot reference and the lexicon cannot change.
**   liblime_compiler  compose-time BUILDERS: lime_snapshot_set_lexicon,
**                     lime_lex_compose.
**
** Typical host:
**
**   // once, on the base snapshot (codes resolved by token NAME)
**   static const LimeLexItem kw[] = {{"select","SELECT"},{"from","FROM"}};
**   static const LimeLexItem op[] = {{"<=","LESS_EQUALS"},{"::","TYPECAST"}};
**   lime_snapshot_set_lexicon(base, kw, 2, op, 2, LIME_LEX_FOLD_CASE);
**
**   // whenever extensions change (compose time); this also rebuilds
**   // the lexicon (lime_lex_compose), and fails if that fails
**   if (!publish_modified_snapshot(reg, base, &snap, NULL, &err))
**       fail(err);                                 // keep the old one
**
**   // per token (parse time; snap held by this parse)
**   const LimeLexicon *lx = lime_snapshot_lexicon(snap);
**   int code = lime_kw_lookup(lx, text, len);      // -1 = identifier
**   size_t n = lime_op_match(lx, text, len, &code); // longest operator
**
** MOD_ADD_TOKEN.lexeme feeds the lexicon: a lexeme made only of
** identifier bytes ([A-Za-z0-9_] and bytes >= 0x80) is a keyword,
** anything else is an operator.  A soft keyword (add_token.fallback_to)
** is a keyword here too; the parser's %fallback decides where it is a
** keyword and where it is an identifier, so the lexer needs no context.
*/
#ifndef LIME_LEXER_H
#define LIME_LEXER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ParserSnapshot;
struct GrammarModification;

typedef struct LimeLexicon LimeLexicon;

/* Lexicon flags. */
#define LIME_LEX_FOLD_CASE 0x1u /* keywords match ASCII case-insensitively (SQL) */
#define LIME_LEX_OP_WHOLE 0x2u  /* lime_lexicon_classify: an operator-class match is a
                                ** known operator only if it is one AS A WHOLE (PG: the
                                ** run "<=>" is one Op, not "<=" ">"); default is the
                                ** longest-prefix split (C: ">>=" after ">>=" ...). */

/* One keyword or operator: its spelling and the NAME of the terminal it
** produces.  Names, not codes, because codes are reassigned by every
** recompile; the builder resolves them against the snapshot. */
typedef struct LimeLexItem {
    const char *text;
    const char *token;
} LimeLexItem;

/* ---------------- readers (liblime_parser) ---------------- */

/* The lexicon attached to `snap`, or NULL when none was set.  Valid for
** as long as the caller holds `snap`. */
const LimeLexicon *lime_snapshot_lexicon(const struct ParserSnapshot *snap);

/* Keyword lookup.  Returns the external token code for `s[0..n)`, or -1
** when it is not a keyword (or lx is NULL).  Folds ASCII case when the
** lexicon was built with LIME_LEX_FOLD_CASE.  No lock, no allocation. */
int lime_kw_lookup(const LimeLexicon *lx, const char *s, size_t n);

/* Longest operator that is a prefix of `s[0..n)`.  Returns its length
** and stores its token code in *code; returns 0 (and leaves *code
** alone) when no operator is a prefix.  A run like `->>=` splits into
** the longest known operator; the caller lexes the rest next. */
size_t lime_op_match(const LimeLexicon *lx, const char *s, size_t n, int *code);

/* Counts, for diagnostics and tests. */
uint32_t lime_lexicon_keyword_count(const LimeLexicon *lx);
uint32_t lime_lexicon_operator_count(const LimeLexicon *lx);

/* Classifier for scanners generated with %keyword_class / %operator_class
** (docs/LEXER_DESIGN.md): pass it with the lexicon as `cls` to the
** generated <P>LexSetClassifier.  kind is LIME_LEX_CLASS_KEYWORD or
** LIME_LEX_CLASS_OPERATOR.  Returns the token code and sets *out_len to
** the bytes it claims (the whole text for a keyword; the longest
** operator prefix, or the whole text under LIME_LEX_OP_WHOLE, for an
** operator), or returns -1 to let the rule's action run. */
#define LIME_LEX_CLASS_KEYWORD 0
#define LIME_LEX_CLASS_OPERATOR 1
int lime_lexicon_classify(const void *lexicon, int kind, const char *text, size_t len,
                          size_t *out_len);

/* Extension lexical rules (MOD_ADD_LEX_RULE, B5), for scanners generated
** with `%runtime_rules.`: hand both functions and the lexicon to
**   <P>LexSetRuntimeRules(lex, lime_lexicon_ext_match,
**                         lime_lexicon_ext_resolve, lime_snapshot_lexicon(snap));
** The setter calls ext_resolve once per scanner state: it returns that
** state's index among the extension states (or -1: no extension rule
** there) and its 256-entry leading-byte table, which the scanner tests
** inline before every token.  Only when the byte can start an extension
** match does the scanner call ext_match, which returns the token code
** and *out_len when an extension rule matches STRICTLY more than
** base_len bytes (LIME_LEX_EXT_SKIP when the rule's action said skip),
** else -1.  The lead table belongs to the lexicon: keep the snapshot
** for as long as the scanner uses it. */
#define LIME_LEX_EXT_SKIP (-2)
int lime_lexicon_ext_resolve(const void *lexicon, const char *state, const unsigned char **lead);
int lime_lexicon_ext_match(const void *lexicon, int ext_state, const char *text, size_t len,
                           size_t base_len, size_t *out_len);
uint32_t lime_lexicon_ext_rule_count(const LimeLexicon *lx);

/* Free a lexicon.  Called by the snapshot that owns it; hosts never
** need it unless they detach one by hand. */
void lime_lexicon_free(LimeLexicon *lx);

/* ---------------- runtime DFA interpreter (B4) ----------------
**
** A LimeLexProgram is a .lex file compiled in-process to plain data
** (lime_lex_program_compile, liblime_compiler): the same per-state
** minimised DFAs `lime -X` would turn into C.  The interpreter below
** (liblime_parser) runs them with flex semantics: longest match, ties
** to the earlier rule, per-state DFAs.  It does not run C action
** bodies (there is no compiler at runtime); the caller acts on the
** returned rule id, exactly as a hand-written driver of the generated
** <P>_match() does. */
typedef struct LimeLexProgram LimeLexProgram;

/* Longest match of `s[0..n)` in lexer state `state`.  Returns 1 and
** sets *rule (global compile-order rule id) and *len; returns 0 when no
** rule matches.  Same contract as the generated <P>_match(). */
int lime_lex_program_match(const LimeLexProgram *p, int state, const char *s, size_t n,
                           int *rule, size_t *len);

/* Introspection: -1 / NULL when out of range or unknown. */
int lime_lex_program_rule_count(const LimeLexProgram *p);
int lime_lex_program_state_count(const LimeLexProgram *p);
const char *lime_lex_program_rule_name(const LimeLexProgram *p, int rule);
int lime_lex_program_rule_by_name(const LimeLexProgram *p, const char *name);
int lime_lex_program_state_by_name(const LimeLexProgram *p, const char *name);
int lime_lex_program_rule_is_eof(const LimeLexProgram *p, int rule);
/* The <<EOF>> rule that fires in `state`, or -1. */
int lime_lex_program_eof_rule(const LimeLexProgram *p, int state);

void lime_lex_program_free(LimeLexProgram *p);

/* ---------------- builders (liblime_compiler) ---------------- */

/* Compile .lex source text in-process (parse, resolve %pattern, per-
** state NFA -> DFA -> minimise; no fork, no file, no compiler).
** Returns NULL with a malloc'd *error on a syntax / regex error.
** Action bodies are parsed but ignored. */
LimeLexProgram *lime_lex_program_compile(const char *lex_source, size_t len, char **error);


/* Build a lexicon from host-supplied keywords and operators and attach
** it to `snap` (replacing any existing one).  Every `token` must name a
** terminal of `snap`.  Call before `snap` is shared with other threads:
** this writes the snapshot.  Returns NULL on success, otherwise a
** malloc'd message (unknown token name, empty or duplicate spelling,
** out of memory) and leaves `snap` unchanged. */
char *lime_snapshot_set_lexicon(struct ParserSnapshot *snap, const LimeLexItem *keywords,
                                size_t nkeywords, const LimeLexItem *operators,
                                size_t noperators, unsigned flags);

/* Compose-time rebuild: give `composed` (freshly built from `base` +
** `mods`, and not yet shared) the base's lexicon plus every
** MOD_ADD_TOKEN.lexeme and MOD_ADD_LEX_RULE in `mods`.
** publish_modified_snapshot calls this itself, on the modifications
** that survived the compose policy, and fails the publish on an error;
** call it directly only for a snapshot you composed yourself.
**
** Returns NULL on success.  On failure returns a malloc'd message, and
** the caller must fail the publish (release `composed`, keep serving
** the old snapshot).  Failures:
**   - a base keyword/operator's token code differs in `composed`
**     (base terminal codes must be stable across composes);
**   - one spelling claimed by two different tokens (base vs extension,
**     or two extensions); the same spelling for the same token is fine;
**   - a MOD_ADD_TOKEN's name is not a terminal of `composed`;
**   - out of memory.
** The result does not depend on the order of `mods`.  A base without a
** lexicon composes to an extension-only, case-sensitive lexicon (or none
** at all when no MOD_ADD_TOKEN carries a lexeme). */
char *lime_lex_compose(struct ParserSnapshot *composed, const struct ParserSnapshot *base,
                       const struct GrammarModification *mods, uint32_t nmods);

#ifdef __cplusplus
}
#endif

#endif /* LIME_LEXER_H */
