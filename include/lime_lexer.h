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
**   // whenever extensions change (compose time)
**   publish_modified_snapshot(reg, base, &snap, NULL, &err);
**   char *e = lime_lex_compose(snap, base, mods, nmods);
**   if (e) { snapshot_release(snap); fail(e); }   // keep the old one
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

/* Free a lexicon.  Called by the snapshot that owns it; hosts never
** need it unless they detach one by hand. */
void lime_lexicon_free(LimeLexicon *lx);

/* ---------------- builders (liblime_compiler) ---------------- */

/* Build a lexicon from host-supplied keywords and operators and attach
** it to `snap` (replacing any existing one).  Every `token` must name a
** terminal of `snap`.  Call before `snap` is shared with other threads:
** this writes the snapshot.  Returns NULL on success, otherwise a
** malloc'd message (unknown token name, empty or duplicate spelling,
** out of memory) and leaves `snap` unchanged. */
char *lime_snapshot_set_lexicon(struct ParserSnapshot *snap, const LimeLexItem *keywords,
                                size_t nkeywords, const LimeLexItem *operators,
                                size_t noperators, unsigned flags);

/* Compose-time rebuild: give `composed` (freshly built by
** publish_modified_snapshot from `base` + `mods`, and not yet shared)
** the base's lexicon plus every MOD_ADD_TOKEN.lexeme in `mods`.
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
