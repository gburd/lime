/*
** tests/test_lex_per_rule_grammar.lex -- M4.3 per-rule test
** entry-point grammar.
**
** Mix:
**   - top-level INITIAL rules (plus / num / ident / ws)
**   - an EXPR-qualified rule (only fires in EXPR state) so the
**     test driver can verify the wrapper picks EXPR over INITIAL
**   - an <<EOF>> rule that MUST NOT receive a wrapper (verified
**     by checking the generated .c file at runtime)
**
** Action bodies are auto-emit (empty); the unit under test is
** the per-rule wrapper, not the runtime, so we keep the rules
** trivial and unambiguous.
*/
%name_prefix Pr.

%exclusive_state EXPR.

rule plus  matches /\+/      { /* */ }
rule num   matches /[0-9]+/  { /* */ }
rule ident matches /[a-z]+/  { /* */ }
rule ws    matches /[ \t]+/  { /* */ }

<EXPR> rule expr_plus matches /\+/ { /* */ }

rule end_of_input matches <<EOF>> { /* */ }
