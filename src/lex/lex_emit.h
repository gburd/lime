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
** Returns 0 on success, non-zero on write error. */
int lime_lex_emit_h(const LimeLexCompiled *c,
                    const char *name_prefix,
                    const char *const *rule_names,
                    int n_rules,
                    FILE *out);

/* Emit the `.c` content.  `header_basename` is the relative
** include path the .c file uses to find its own .h (typically
** "foo_lex.h"). */
int lime_lex_emit_c(const LimeLexCompiled *c,
                    const char *name_prefix,
                    const char *header_basename,
                    const char *const *rule_names,
                    int n_rules,
                    FILE *out);

/* Convenience: for a parsed spec, derive the rule-name array
** in global compile order (top-level rules first, then ruleset
** rules in their declaration order).  Caller-owned: free the
** returned `*names_out` array AND each string with `free`. */
int lime_lex_collect_rule_names(const LimeLexSpec *spec,
                                char ***names_out,
                                int *n_rules_out);

#endif /* LIME_LEX_EMIT_H */
