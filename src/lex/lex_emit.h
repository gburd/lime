/*
** src/lex/lex_emit.h -- code generator for the Lime .lex
** runtime (M3.1).
**
** Takes a fully-compiled LimeLexCompiled (output of M2.5b) and
** writes a `.c` + `.h` pair that callers can compile and link.
** Emitted code is self-contained: depends only on <stddef.h>
** and <string.h>, no Lime runtime library required.
**
** Surface (foo_lex.h, where Foo is the spec's %name_prefix):
**
**   #define FOO_STATE_INITIAL 0
**   #define FOO_STATE_<NAME>  1   (one per declared state)
**
**   enum { FOO_RULE_<RULE_NAME> = N, ... };
**
**   extern const char *const FooRuleNames[];
**
**   int Foo_match(int state, const char *bytes, size_t n,
**                 int *out_rule, size_t *out_consumed);
**
** Foo_match implements longest-match: scan forward, track the
** most-recent accept position, on a dead end rewind to that
** accept point and report the accepted rule.  Returns 1 on
** match, 0 if no rule matched at the current position (the
** caller decides whether that's an error or end-of-input).
**
** This is M3.1 -- the foundation.  M3.2 wires LexAlloc /
** LexFeedBytes / LexFeedEOF / LexInclude / LexSetState / etc.
** on top.
*/
#ifndef LIME_LEX_EMIT_H
#define LIME_LEX_EMIT_H

#include "lex_compile.h"

#include <stdio.h>

/* Emit the `.h` content for the compiled lexer to `out`.
** `name_prefix` is the user-facing prefix (e.g. "Foo" -> Foo_match,
** FooRuleNames, FOO_STATE_*, FOO_RULE_*).  When NULL or empty,
** falls back to "Lex".  The compiled spec's `LimeLexSpec`
** name_prefix should be passed if present.
**
** `spec` carries the parsed source rules.  When non-NULL it is
** used to emit the M4.3 per-rule test entry-point declarations
** (one prototype per non-EOF rule).  When NULL the test entries
** are suppressed -- legacy callers that only want the runtime
** API surface keep working.
**
** Returns 0 on success, non-zero on write error. */
int lime_lex_emit_h(const LimeLexCompiled *c, const LimeLexSpec *spec, const char *name_prefix,
                    const char *const *rule_names, int n_rules, FILE *out);

/* Emit the `.c` content.  `header_basename` is the relative
** include path the .c file uses to find its own .h (typically
** "foo_lex.h").  `spec` is the parsed (and pattern-resolved)
** lexer spec; its rules drive M3.4 action-body inlining inside
** the generated `LexFeedBytes`.  Pass NULL to fall back to the
** M3.3 behavior (auto-emit the matched rule for every match,
** ignoring action bodies); the auto-emit fallback also fires
** per-rule when an action body neither calls LEX_EMIT nor
** LEX_SKIP, so existing M3.3 grammars with empty action bodies
** keep working. */
int lime_lex_emit_c(const LimeLexCompiled *c, const LimeLexSpec *spec, const char *name_prefix,
                    const char *header_basename, const char *const *rule_names, int n_rules,
                    FILE *out);

/* Convenience: for a parsed spec, derive the rule-name array
** in global compile order (top-level rules first, then ruleset
** rules in their declaration order).  Caller-owned: free the
** returned `*names_out` array AND each string with `free`. */
int lime_lex_collect_rule_names(const LimeLexSpec *spec, char ***names_out, int *n_rules_out);

/* ============================================================
** B3: runtime classification hook (%keyword_class / %operator_class)
** ============================================================
**
** A rule named by `%keyword_class RULE.` or `%operator_class RULE.`
** offers each match to a host-installed classifier before its action
** body runs:
**
**   typedef int (*<P>ClassifyFn)(const void *cls, int kind,
**                                const char *text, size_t len,
**                                size_t *out_len);
**     kind: <P>_CLASS_KEYWORD (0) or <P>_CLASS_OPERATOR (1)
**     return a token code >= 0 to claim the first *out_len bytes
**     (1 <= *out_len <= len), or -1 to decline.
**
** include/lime_lexer.h's lime_lexicon_classify has exactly this
** signature and answers from a snapshot lexicon, so a host writes
**   <P>LexSetClassifier(lex, lime_lexicon_classify,
**                       lime_snapshot_lexicon(snap));
** and the same generated scanner resolves keywords and operators that
** extensions compose in later, with no regeneration.
**
** On a claim the scanner emits the token code over text[0..*out_len),
** pushes back the rest of the match, and skips the action body.  On a
** decline (or with no classifier installed) the action runs as before.
**
** Hook for emitters and skins.  When lime_lex_has_classes(spec) is
** zero nothing below is emitted and the output is byte-identical to a
** scanner without the feature.  Otherwise <stem>_lex.h declares
** <P>ClassifyFn, <P>_CLASS_*, <P>LexSetClassifier and
**
**   int <P>_classify(<P>ClassifyFn fn, const void *cls, int rule,
**                    const char *text, size_t len, size_t *out_len);
**
** and <stem>_lex.c defines them.  <P>_classify is the whole decision
** (is `rule` a class rule? ask fn; validate *out_len) and is what a
** skin calls after <P>_match: e.g. the flex skin keeps a global
** fn/cls pair set by a `yy_set_classifier(fn, cls)`, and in yylex()
** after a match does
**
**   size_t n; int code = <P>_classify(fn, cls, rule, yytext, consumed, &n);
**   if (code >= 0) { consumed = n; ...advance by n...; return code; }
*/
int lime_lex_has_classes(const LimeLexSpec *spec);

/* Emit the .h part (typedef, constants, prototypes) / the .c part
** (rule-class table + <P>_classify).  Both emit nothing and return 0
** when lime_lex_has_classes(spec) is zero.  lime_lex_emit_h/_c call
** these; skins that write their own files may call them too. */
int lime_lex_emit_classify_h(const LimeLexSpec *spec, const char *prefix, FILE *out);
int lime_lex_emit_classify_c(const LimeLexSpec *spec, const char *prefix, int n_rules,
                             FILE *out);

/* ============================================================
** B5: runtime extension rules (%runtime_rules.)
** ============================================================
**
** With `%runtime_rules.` the scanner gets
**
**   typedef int (*<P>RuntimeRulesFn)(const void *cls, int ext_state,
**                                    const char *text, size_t len,
**                                    size_t base_len, size_t *out_len);
**   typedef int (*<P>RuntimeStateFn)(const void *cls, const char *state,
**                                    const unsigned char **lead);
**   void <P>LexSetRuntimeRules(<P>Lexer *, <P>RuntimeRulesFn,
**                              <P>RuntimeStateFn, const void *cls);
**
** The setter resolves, once per scanner state, an extension-state index
** and a 256-byte leading-byte table.  LexFeedBytes then tests
** lead[byte] inline before each token (one load when nothing can start
** there) and only on a hit calls fn for a STRICTLY longer match than the
** base scanner's.  fn returns a token code (emitted over *out_len bytes,
** base action skipped), -2 (consume, emit nothing) or -1 (keep the base
** match).  include/lime_lexer.h's lime_lexicon_ext_match /
** lime_lexicon_ext_resolve have these signatures.  Without the directive
** nothing is emitted (byte-identical output). */
int lime_lex_emit_runtime_rules_h(const LimeLexSpec *spec, const char *prefix, FILE *out);

#endif /* LIME_LEX_EMIT_H */
